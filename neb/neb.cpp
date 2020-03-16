#include <future>
#include <limits>

#include <dory/conn/exchanger.hpp>
#include <dory/conn/wr_builder.hpp>
#include <dory/shared/bench.hpp>
#include <dory/store.hpp>
#include "consts.hpp"
#include "neb.hpp"

inline uint64_t unpack_msg_id(uint64_t wr_id) { return (wr_id << 32) >> 32; }
inline int unpack_replay_id(uint64_t wr_id) { return wr_id >> 48; }
inline int unpack_origin_id(uint64_t wr_id) { return (wr_id << 16) >> 48; }
inline uint64_t pack_read_id(int replayer, int origin, uint64_t msg_id) {
  return uint64_t(replayer) << 48 | uint64_t(origin) << 32 | msg_id;
}

inline int unpack_receiver_id(uint64_t wr_id) { return wr_id >> 48; }
inline uint64_t pack_write_id(int receiver, uint64_t msg_id) {
  return (uint64_t(receiver) << 48) | msg_id;
}

namespace dory {

using namespace neb;

NonEquivocatingBroadcast::~NonEquivocatingBroadcast() {
  exit_signal.set_value();

  if (poller_running) poller_thread.join();
  logger->info("Poller thread finished");

  // if (reader_running) replay_reader_thread.join();
  // logger->info("Replay thread finished");
}

NonEquivocatingBroadcast::NonEquivocatingBroadcast(int self_id,
                                                   std::vector<int> remote_ids,
                                                   dory::ControlBlock &cb,
                                                   deliver_callback deliver)
    : self_id(self_id),
      remote_ids{remote_ids},
      deliver(deliver),
      cb(cb),
      logger(std_out_logger("NEB")),
      own_next(1),
      pending_reads(0) {
  write_wcs.resize(dory::ControlBlock::CQDepth);
  read_wcs.resize(dory::ControlBlock::CQDepth);

  logger->set_level(spdlog::level::info);
  logger->info("Creating instance");

  auto proc_ids = remote_ids;
  proc_ids.push_back(self_id);

  for (auto &id : proc_ids) {
    next.insert(std::pair<int, MessageTracker>(id, MessageTracker()));
  }

  auto r_mr_w = cb.mr(REPLAY_W_NAME);
  replay_w_buf =
      std::make_unique<ReplayBufferWriter>(r_mr_w.addr, r_mr_w.size, proc_ids);

  auto r_mr_r = cb.mr(REPLAY_R_NAME);
  replay_r_buf = std::make_unique<ReplayBufferReader>(r_mr_r.addr, r_mr_r.size,
                                                      r_mr_r.lkey, proc_ids);

  // Insert a buffer which will be used as scatter when post sending writes
  auto b_mr = cb.mr(BCAST_W_NAME);
  bcast_bufs.insert(std::pair<int, BroadcastBuffer>(
      self_id, BroadcastBuffer(b_mr.addr, b_mr.size, b_mr.lkey)));
}

void NonEquivocatingBroadcast::set_connections(ConnectionExchanger &b_ce,
                                               ConnectionExchanger &r_ce) {
  if (b_ce.connections().size() != remote_ids.size()) {
    throw std::runtime_error(
        "number of broadcast QPs does not match remote processes");
  }

  if (r_ce.connections().size() != remote_ids.size()) {
    throw std::runtime_error(
        "number of broadcast QPs does not match remote processes");
  }

  bcast_conn.merge(b_ce.connections());
  replay_conn.merge(r_ce.connections());

  for (auto &[pid, rc] : bcast_conn) {
    auto &mr = rc.get_mr();
    bcast_bufs.insert(std::pair<int, BroadcastBuffer>(
        pid, BroadcastBuffer(mr.addr, mr.size, mr.lkey)));
  }

  connected = true;
}

void NonEquivocatingBroadcast::start() {
  if (started) return;

  if (!connected) {
    throw std::runtime_error(
        "Not connected to remote QPs, cannot start serving");
  }

  auto sf = std::shared_future(exit_signal.get_future());

  poller_thread = std::thread([=] { start_replayer(sf); });
  // replay_reader_thread = std::thread([=] { start_reader(sf); });

  started = true;
  logger->info("Started");
}

void NonEquivocatingBroadcast::broadcast(uint64_t k, Broadcastable &msg) {
  if (msg.size() > MSG_PAYLOAD_SIZE) {
    throw std::runtime_error("Allowed message size exceeded");
  }

  // empty the write CQ once it's full
  if (((own_next * remote_ids.size()) % dory::ControlBlock::CQDepth -
       remote_ids.size()) <= 0 &&
      own_next != 1) {
    logger->info("Emptying the write CQ");

    auto &cq = cb.cq(BCAST_CQ_NAME);
    cb.pollCqIsOK(cq, write_wcs);
    write_wcs.resize(ControlBlock::CQDepth);
  }

  auto bcast_buf = bcast_bufs.find(self_id)->second;
  auto msg_size = bcast_buf.write(own_next, k, msg);
  auto entry = bcast_buf.get_entry(own_next);

  for (auto &[id, rc] : bcast_conn) {
    auto ret = rc.postSendSingle(
        ReliableConnection::RdmaWrite, pack_write_id(id, own_next),
        reinterpret_cast<void *>(entry->addr()), msg_size + MSG_HEADER_SIZE,
        bcast_buf.lkey, rc.remoteBuf() + bcast_buf.get_byte_offset(own_next));

    if (!ret) {
      logger->warn("Write post for {} at {} returned {}", own_next, id, ret);
    }
  }

  // increase the message counter
  own_next++;
  // deliver to ourself
  deliver(k, entry->content(), self_id);
}

void NonEquivocatingBroadcast::start_replayer(std::shared_future<void> f) {
  if (poller_running) return;

  logger->info("Replayer thread running");

  poller_running = true;

  auto &cq = cb.cq(REPLAY_CQ_NAME);

  while (f.wait_for(std::chrono::seconds(0)) == std::future_status::timeout) {
    poll_bcast_bufs();
    consume(cq);
  }
}

inline void NonEquivocatingBroadcast::poll_bcast_bufs() {
  for (int &origin : remote_ids) {
    auto &next_msg = next[origin];

    auto bcast_entry =
        bcast_bufs.find(origin)->second.get_entry(next_msg.idx());

    // TODO(Kristian): eventually check for matching signature
    if (bcast_entry->id() == 0 || bcast_entry->id() != next_msg.idx() ||
        next_msg.state() == MessageTracker::State::Replayed ||
        pending_reads >= dory::ControlBlock::CQDepth)
      continue;

    logger->debug(
        "Bcast from {} at index {} with id {}", origin, next_msg.idx(),
        bcast_entry->id(),
        *reinterpret_cast<volatile const uint64_t *>(bcast_entry->content()));

    auto replay_entry_w = replay_w_buf->get_entry(origin, next_msg.idx());

    bcast_entry->copy_to(*replay_entry_w);

    next_msg.transition(MessageTracker::State::Replayed);

    for (auto &[replayer, rc] : replay_conn) {
      if (replayer == origin) continue;

      auto replay_entry_r =
          replay_r_buf->get_entry(origin, replayer, next_msg.idx());

      auto ret =
          rc.postSendSingle(ReliableConnection::RdmaRead,
                            pack_read_id(replayer, origin, next_msg.idx()),
                            reinterpret_cast<void *>(replay_entry_r->addr()),
                            BUFFER_ENTRY_SIZE, replay_r_buf->lkey,
                            rc.remoteBuf() + replay_w_buf->get_byte_offset(
                                                 origin, next_msg.idx()));

      if (!ret) {
        logger->warn("Read post of {} at {} returned {}", next_msg.idx(),
                     origin, ret);
      } else {
        pending_reads++;
      }
    }
  }
}

inline void NonEquivocatingBroadcast::consume(
    dory::deleted_unique_ptr<ibv_cq> &cq) {
  read_wcs.resize(pending_reads);

  if (cb.pollCqIsOK(cq, read_wcs)) {
    logger->debug("CQ polled, size: {}!", read_wcs.size());
    pending_reads -= read_wcs.size();

    for (auto const &wc : read_wcs) {
      int r_id = unpack_replay_id(wc.wr_id);
      int o_id = unpack_origin_id(wc.wr_id);

      uint64_t next_idx = unpack_msg_id(wc.wr_id);

      logger->debug("WC for READ at {} for ({},{}) ", r_id, o_id, next_idx);

      auto &next_msg = next[o_id];

      if (next_msg.state() == MessageTracker::State::Initial) {
        logger->warn("Initial state should never exist at this point!");
      }

      auto r_entry = replay_r_buf->get_entry(o_id, r_id, next_idx);
      auto o_entry = bcast_bufs.find(o_id)->second.get_entry(next_idx);

      // got no response, this may happen when the remote is not ready yet
      // and didn't initialize it's memory buffer or is disconnected
      if (r_entry->id() == 0) {
        logger->warn("Slot was not successfully read");
        // TODO(Kristian): can we safely make this replayer part of the quorum?
        next_msg.add_to_quorum(r_id);
      }
      // got responde but nothing is replayed
      else if (r_entry->id() == std::numeric_limits<uint64_t>::max()) {
        logger->debug("Nothing replayed");
        // empty reads can be add this as an ack to the quorum
        next_msg.add_to_quorum(r_id);
      }
      // got something replayed
      else {
        bool is_matching = r_entry->same_content_as(*o_entry);
        logger->debug("Slots match: {}", is_matching);

        if (is_matching) {
          next_msg.add_to_quorum(r_id);
        } else {
          next_msg.transition(MessageTracker::State::Conflict);
        }
      }

      auto entry = bcast_bufs.find(o_id)->second.get_entry(next_msg.idx());

      if (next_msg.has_quorum_of(remote_ids.size() - 1)) {
        deliver(entry->id(), entry->content(), o_id);

        next_msg.move_forward();
      }
    }
  }
}

// void NonEquivocatingBroadcast::start_reader(std::shared_future<void> f) {
//   if (reader_running) return;

//   logger->info("Replay reader thread running");

//   reader_running = true;

//   auto &cq = cb.cq(REPLAY_CQ_NAME);

//   while (f.wait_for(std::chrono::seconds(0)) == std::future_status::timeout)
//   {
//     replay_read(cq);
//   }
// }

}  // namespace dory
