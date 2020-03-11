#include <dory/conn/exchanger.hpp>
#include <dory/shared/unused-suppressor.hpp>
#include <dory/store.hpp>

#include "neb.hpp"

namespace dory {

NonEquivocatingBroadcast::~NonEquivocatingBroadcast() {
  poller_running = false;
  // while (!poller_finished) {
  // }
  logger->info("Poller thread finished\n");
}

NonEquivocatingBroadcast::NonEquivocatingBroadcast(int self_id,
                                                   std::vector<int> remote_ids,
                                                   dory::ControlBlock &cb,
                                                   deliver_callback deliver)
    : self_id(self_id),
      remote_ids{remote_ids},
      deliver(deliver),
      cb(cb),
      logger(std_out_logger("NEB")) {
  IGNORE(this->cb);

  logger->set_pattern(SPD_FORMAT_STR);
  logger->set_level(spdlog::level::debug);
  logger->info("Creating instance");

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

  poller_thread = std::thread([=] { start_poller(); });
  poller_thread.detach();

  started = true;
  logger->info("Started");
}

void NonEquivocatingBroadcast::broadcast(uint64_t k, Broadcastable &msg) {
  const auto next_idx = next[self_id];

  // write to own broadcast buffer and use it as the scatter element for writes
  auto bcast_buf = bcast_bufs.find(self_id)->second;
  auto entry = bcast_buf.get_entry(next_idx);
  auto buf = reinterpret_cast<uint64_t *>(entry->addr());

  buf[0] = k;

  auto msg_size = msg.marshall(reinterpret_cast<void *>(&buf[1]));

  for (auto &it : bcast_conn) {
    auto &rc = it.second;
    auto ret = rc.postSendSingle(
        ReliableConnection::RdmaWrite, next_idx,
        reinterpret_cast<void *>(entry->addr()), msg_size + NEB_MSG_OVERHEAD,
        bcast_buf.lkey, rc.remoteBuf() + bcast_buf.get_byte_offset(next_idx));

    if (!ret) {
      std::cout << "Post returned " << ret << std::endl;
    }
  }

  // increase the message counter
  next[self_id]++;
  // deliver to ourself
  deliver(k, entry->content(), self_id);
}

void NonEquivocatingBroadcast::start_poller() {
  logger->info("Poller thread running");

  if (poller_running) return;

  poller_running = true;

  while (poller_running) {
    for (int &id : remote_ids) {
      uint64_t next_index = next[id];

      auto bcast_entry = bcast_bufs.find(id)->second.get_entry(next_index);

      // TODO(Kristian): eventually check for matching signature
      if (bcast_entry->id() == 0 || bcast_entry->id() != next_index) continue;

      logger->debug(
          "Bcast from {} at {} = ({}, {})", id, next_index, bcast_entry->id(),
          *reinterpret_cast<volatile const uint64_t *>(bcast_entry->content()));

      auto replay_entry_w = replay_w_buf->get_entry(id, next_index);

      memcpy((void *)replay_entry_w->addr(), (void *)bcast_entry->addr(),
             BUFFER_ENTRY_SIZE);

      logger->debug("Replay for {} at {} = ({}, {})", id, next_index,
                    replay_entry_w->id(),
                    *reinterpret_cast<volatile const uint64_t *>(
                        replay_entry_w->content()));

      bool is_valid = true;

      for (auto &[j, rc] : replay_conn) {
        if (j == id) continue;

        auto replay_entry_r = replay_r_buf->get_entry(id, j, next_index);

        auto ret = rc.postSendSingle(
            ReliableConnection::RdmaRead, next_index,
            reinterpret_cast<void *>(replay_entry_r->addr()), BUFFER_ENTRY_SIZE,
            replay_r_buf->lkey,
            rc.remoteBuf() + replay_w_buf->get_byte_offset(id, next_index));

        if (!ret) {
          std::cout << "Post returned " << ret << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        logger->debug("Replay entry for ({},{}) at {} = ({},{})", id,
                      next_index, j, replay_entry_r->id(),
                      *reinterpret_cast<volatile const uint64_t *>(
                          replay_entry_r->content()));

        // TODO(Kristian): split the reading from writing
        if (replay_entry_r->id() != 0 &&
            memcmp((void *)bcast_entry->addr(), (void *)replay_entry_r->addr(),
                   BUFFER_ENTRY_SIZE)) {
          is_valid = false;
        }
      }

      if (is_valid) {
        deliver(bcast_entry->id(), bcast_entry->content(), id);
        next[id]++;
      }
    }
  }
  poller_finished = true;
}
}  // namespace dory
