#include <future>
#include <limits>

#include <dory/conn/exchanger.hpp>
#include <dory/conn/wr-builder.hpp>
#include <dory/shared/bench.hpp>
#include <dory/store.hpp>

#include "consts.hpp"
#include "neb.hpp"

// TODO(Kristian): use uint32_t for message id
inline uint64_t unpack_msg_id(uint64_t wr_id) { return (wr_id << 32) >> 32; }
inline std::tuple<int, int, uint64_t> unpack_read_id(uint64_t wr_id) {
  return {wr_id >> 48, (wr_id << 16) >> 48, unpack_msg_id(wr_id)};
}
inline uint64_t pack_read_id(int replayer, int origin, uint64_t msg_id) {
  return uint64_t(replayer) << 48 | uint64_t(origin) << 32 | msg_id;
}
inline int unpack_receiver_id(uint64_t wr_id) { return wr_id >> 48; }
inline std::pair<int, uint64_t> unpack_write_id(uint64_t wr_id) {
  return {unpack_receiver_id(wr_id), unpack_msg_id(wr_id)};
}
inline uint64_t pack_write_id(int receiver, uint64_t msg_id) {
  return (uint64_t(receiver) << 48) | msg_id;
}

namespace dory {

using namespace neb;

NonEquivocatingBroadcast::~NonEquivocatingBroadcast() {
  exit_signal.set_value();

  if (bcast_poller_running) poller_thread.join();
  LOGGER_INFO(logger, "Bcast poller thread finished");

  if (cq_poller_running) cq_poller_thread.join();
  LOGGER_INFO(logger, "CQ poller thread finished");
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
      pending_reads(0),
      pending_writes(0) {
  logger->set_level(LOG_LEVEL);
  LOGGER_INFO(logger, "Creating instance");

  write_wcs.resize(dory::ControlBlock::CQDepth);
  read_wcs.resize(dory::ControlBlock::CQDepth);

  auto proc_ids = remote_ids;
  proc_ids.push_back(self_id);

  for (auto &id : proc_ids) {
    next.insert(std::pair<int, uint64_t>(id, 1));
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

void NonEquivocatingBroadcast::set_remote_keys(
    std::map<int, dory::crypto::pub_key> &keys) {
  remote_keys.merge(keys);
}

void NonEquivocatingBroadcast::set_remote_keys(
    std::map<int, dory::crypto::pub_key> &&keys) {
  remote_keys.merge(keys);
}

void NonEquivocatingBroadcast::start() {
  if (started) return;

  if (!connected) {
    throw std::runtime_error(
        "Not connected to remote QPs, cannot start serving");
  }

  auto sf = std::shared_future(exit_signal.get_future());

  poller_thread = std::thread([=] { start_replayer(sf); });
  cq_poller_thread = std::thread([=] { start_cq_poller(sf); });

  started = true;
  LOGGER_INFO(logger, "Started");
}

void NonEquivocatingBroadcast::end() {
  // TODO(Kristian): use this to write micro benchmark results into a file
}

void NonEquivocatingBroadcast::broadcast(uint64_t k, Broadcastable &msg) {
  if (msg.size() > MSG_PAYLOAD_SIZE) {
    throw std::runtime_error("Allowed message size exceeded");
  }

  while (pending_writes + remote_ids.size() >= dory::ControlBlock::CQDepth)
    ;

  auto bcast_buf = bcast_bufs.find(self_id)->second;

  bcast_buf.write(own_next, k, msg);
  auto slot = bcast_buf.slot(own_next);

  dory::crypto::sign(reinterpret_cast<unsigned char *>(
                         const_cast<uint8_t *>(slot->signature())),
                     reinterpret_cast<unsigned char *>(slot->addr()),
                     SLOT_SIGN_DATA_SIZE);

  remote_mux.lock_shared();
  for (auto &[id, rc] : bcast_conn) {
    auto ret = rc.postSendSingle(
        ReliableConnection::RdmaWrite, pack_write_id(id, own_next),
        reinterpret_cast<void *>(slot->addr()), MEMORY_SLOT_SIZE,
        bcast_buf.lkey, rc.remoteBuf() + bcast_buf.get_byte_offset(own_next));

    if (!ret) {
      LOGGER_WARN(logger, "Write post for {} at {} returned {}", own_next, id,
                  ret);
    }

    pending_writes++;
  }
  remote_mux.unlock_shared();

  // increase the message counter
  own_next++;

  // deliver to ourself
  deliver_mux.lock();
  deliver(k, slot->content(), self_id);
  deliver_mux.unlock();
}

void NonEquivocatingBroadcast::start_replayer(std::shared_future<void> f) {
  if (bcast_poller_running) return;

  LOGGER_INFO(logger, "Replayer thread running");

  bcast_poller_running = true;

  while (f.wait_for(std::chrono::seconds(0)) == std::future_status::timeout) {
    poll_bcast_bufs();
  }
}

// NOTE: this function is currently unused
void NonEquivocatingBroadcast::start_cq_poller(std::shared_future<void> f) {
  if (cq_poller_running) return;

  LOGGER_INFO(logger, "Replay reader thread running");

  cq_poller_running = true;

  auto &rcq = cb.cq(REPLAY_CQ_NAME);
  auto &wcq = cb.cq(BCAST_CQ_NAME);

  while (f.wait_for(std::chrono::seconds(0)) == std::future_status::timeout) {
    consume_read_wcs(rcq);
    consume_write_wcs(wcq);
  }
}

inline void NonEquivocatingBroadcast::poll_bcast_bufs() {
  remote_mux.lock_shared();
  for (auto origin : remote_ids) {
    // wait before exhausting the read CQ
    if (pending_reads + remote_ids.size() - 1 >= dory::ControlBlock::CQDepth)
      continue;

    auto &next_idx = next[origin];

    auto bcast_slot = bcast_bufs.find(origin)->second.slot(next_idx);

    if (bcast_slot->id() == 0 || bcast_slot->id() != next_idx ||
        replayed[origin].find(next_idx) != replayed[origin].end())
      continue;

    bool valid_sig = dory::crypto::verify(
        reinterpret_cast<unsigned char *>(
            const_cast<uint8_t *>(bcast_slot->signature())),
        reinterpret_cast<unsigned char *>(bcast_slot->addr()),
        SLOT_SIGN_DATA_SIZE, remote_keys[origin].get());

    if (!valid_sig) {
      LOGGER_INFO(logger,
                  "Signature could not be validated for (pid,k)=({},{})",
                  origin, next_idx);
      continue;
    }

    LOGGER_DEBUG(
        logger, "Bcast from {} at index {} with id {}", origin, next_idx,
        bcast_slot->id(),
        *reinterpret_cast<volatile const uint64_t *>(bcast_slot->content()));

    auto replay_slot_w = replay_w_buf->slot(origin, next_idx);

    bcast_slot->copy_to(*replay_slot_w);

    // FORCEFULLY INVALIDATE SIGNATURE
    // if (self_id == 1)
    //   reinterpret_cast<uint64_t *>(replay_slot_w->addr())[0] =
    //       std::numeric_limits<uint64_t>::max() - 1;

    // if there is only one remote process which is the message sender,
    // then we can directly deliver without replay reading
    if (remote_ids.size() == 1 &&
        replay_conn.find(origin) != replay_conn.end()) {
      deliver_mux.lock();
      deliver(bcast_slot->id(), bcast_slot->content(), origin);
      deliver_mux.unlock();

      next[origin]++;
      continue;
    }

    rep_mux.lock();
    auto next_msg = replayed[origin][next_idx];
    rep_mux.unlock();

    for (auto &[replayer, rc] : replay_conn) {
      if (replayer == origin) continue;

      auto replay_slot = replay_r_buf->slot(origin, replayer, next_idx);

      auto ret = rc.postSendSingle(
          ReliableConnection::RdmaRead,
          pack_read_id(replayer, origin, next_idx),
          reinterpret_cast<void *>(replay_slot->addr()), MEMORY_SLOT_SIZE,
          replay_r_buf->lkey,
          rc.remoteBuf() + replay_w_buf->get_byte_offset(origin, next_idx));

      if (!ret) {
        LOGGER_WARN(logger, "Read post for {} at {} returned {}", next_idx,
                    origin, ret);
      } else {
        pending_reads++;
      }
    }

    next[origin]++;
  }
  remote_mux.unlock_shared();
}  // namespace dory

inline void NonEquivocatingBroadcast::consume_write_wcs(
    dory::deleted_unique_ptr<ibv_cq> &cq) {
  write_wcs.resize(pending_writes);

  if (cb.pollCqIsOK(cq, write_wcs)) {
    if (write_wcs.size() > 0) {
      LOGGER_DEBUG(logger, "Polled {} entries from WRITE CQ", write_wcs.size());
      pending_writes -= write_wcs.size();
    }

    for (auto &wc : write_wcs) {
      auto receiver = unpack_receiver_id(wc.wr_id);

      switch (wc.status) {
        case IBV_WC_SUCCESS:
          break;
        case IBV_WC_RETRY_EXC_ERR:
          LOGGER_INFO(logger, "WC WRITE: Process {} not responding, removing",
                      receiver);
          remove_remote(receiver);

          break;
        default:
          LOGGER_INFO(logger, "WC for WRITE at {} has status {}", receiver,
                      wc.status);

          auto it = bcast_conn.find(receiver);

          if (it != bcast_conn.end()) {
            struct ibv_qp_attr qp_attr;
            struct ibv_qp_init_attr init_attr;

            it->second.query_qp(qp_attr, init_attr, 0);
            LOGGER_INFO(logger, "Bcast QP state: {}", qp_attr.qp_state);
          }
      }
    }
  }
}  // namespace dory

inline void NonEquivocatingBroadcast::consume_read_wcs(
    dory::deleted_unique_ptr<ibv_cq> &cq) {
  read_wcs.resize(pending_reads);

  if (!cb.pollCqIsOK(cq, read_wcs)) {
    LOGGER_WARN(logger, "Polling on read CQ failed!");
    return;
  }

  if (read_wcs.size() > 0) {
    LOGGER_DEBUG(logger, "READ CQ polled, size: {}", read_wcs.size());
    pending_reads -= read_wcs.size();
  }

  for (auto const &wc : read_wcs) {
    auto const &[r_id, o_id, next_idx] = unpack_read_id(wc.wr_id);
    LOGGER_DEBUG(logger, "WC for READ at {} for ({},{}) ", r_id, o_id,
                 next_idx);

    switch (wc.status) {
      case IBV_WC_SUCCESS: {
        auto &proc_map = replayed[o_id];
        auto it = proc_map.find(next_idx);
        // was already concurrently delivered
        if (it == proc_map.end()) {
          break;
        }

        rep_mux.lock();

        auto &next_msg = it->second;

        auto r_slot = replay_r_buf->slot(o_id, r_id, next_idx);
        auto o_slot = bcast_bufs.find(o_id)->second.slot(next_idx);

        // got no response, this may happen when the remote is not ready yet
        // and didn't initialize its memory buffer
        if (r_slot->id() == 0) {
          LOGGER_WARN(logger, "Slot was not successfully read");
          // TODO(Kristian): we should probably retry!
        }
        // got a response but nothing is replayed
        else if (r_slot->id() == std::numeric_limits<uint64_t>::max()) {
          // empty reads can be added to the quorum since they don't conflict
          next_msg.add_to_quorum(r_id);
        }
        // got something replayed
        else {
          if (r_slot->same_content_as(*o_slot)) {
            next_msg.add_to_quorum(r_id);
          } else {
            // Only now we verify the signature, as the operation is more costly
            // than comparing the slot contents. Anyhow, we want to verify the
            // signature in order to know if the origin broadcaster tried to
            // equivocate or we can count the current processing replayer
            // towards the quorum. A byzantine replayer who changed the slot
            // contents does not prevent us from delivering the messages
            if (!dory::crypto::verify(
                    reinterpret_cast<unsigned char *>(
                        const_cast<uint8_t *>(r_slot->signature())),
                    reinterpret_cast<unsigned char *>(r_slot->addr()),
                    SLOT_SIGN_DATA_SIZE, remote_keys[o_id].get())) {
              next_msg.add_to_quorum(r_id);
            } else {
              LOGGER_INFO(logger, "Process {} tried to equivocate on msg {}!",
                          o_id, next_idx);
            }
          }
        }

        if (next_msg.has_quorum_of(remote_ids.size() - 1)) {
          auto slot = bcast_bufs.find(o_id)->second.slot(next_idx);

          deliver_mux.lock();
          deliver(slot->id(), slot->content(), o_id);
          deliver_mux.unlock();

          // gc the entry
          auto it = proc_map.find(next_idx);
          if (it != proc_map.end()) {
            proc_map.erase(it);
          }
        }

        rep_mux.unlock();
      } break;
      case IBV_WC_RETRY_EXC_ERR: {
        LOGGER_INFO(logger, "WC read; Process {} not responding, removing!",
                    r_id);
        remove_remote(r_id);
      } break;

      default: {
        LOGGER_WARN(logger, "WC for READ at {} for ({},{}) has status {}", r_id,
                    o_id, next_idx, wc.status);

        auto it = replay_conn.find(r_id);

        if (it != replay_conn.end()) {
          struct ibv_qp_attr qp_attr;
          struct ibv_qp_init_attr init_attr;

          it->second.query_qp(qp_attr, init_attr, 0);
          LOGGER_WARN(logger, "Replay QP state: {}", qp_attr.qp_state);
        }
      }
    }
  }
}

inline void NonEquivocatingBroadcast::remove_remote(int pid) {
  std::lock_guard lock(remote_mux);

  auto rit = bcast_conn.find(pid);
  if (rit != bcast_conn.end()) {
    bcast_conn.erase(rit);
  }

  auto bit = replay_conn.find(pid);
  if (bit != replay_conn.end()) {
    replay_conn.erase(bit);
  }

  auto it = std::find(remote_ids.begin(), remote_ids.end(), pid);
  if (it != remote_ids.end()) {
    remote_ids.erase(it);
  }

  // we changed the quorum size, try to deliver all messages that are
  // currently replayed but not delivered yet where we were only waiting for
  // the remote process that is gone.
  for (auto &[origin, msgs] : replayed) {
    rep_mux.lock();
    for (auto it = msgs.cbegin(); it != msgs.cend();) {
      if (it->second.has_quorum_of(remote_ids.size() - 1) &&
          !it->second.includes(pid)) {
        auto slot = bcast_bufs.find(origin)->second.slot(it->first);

        deliver_mux.lock();
        deliver(slot->id(), slot->content(), origin);
        deliver_mux.unlock();

        // gc the entry
        it = msgs.erase(it);
      } else {
        it++;
      }
    }
    rep_mux.unlock();
  }
}
}  // namespace dory
