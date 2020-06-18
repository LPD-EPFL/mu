#include <future>
#include <limits>

#include <dory/conn/exchanger.hpp>
#include <dory/conn/wr-builder.hpp>
#include <dory/shared/bench.hpp>
#include <dory/shared/unused-suppressor.hpp>
#include <dory/store.hpp>

#include "../shared/consts.hpp"
#include "../shared/helpers.hpp"

#include "broadcast.hpp"

// (writer, replicator, origin, idx)
inline std::tuple<int, int, int, uint64_t> unpack_read_id(uint64_t wr_id) {
  return {(wr_id << 2) >> 54, (wr_id << 12) >> 54, (wr_id << 22) >> 54,
          (wr_id << 32) >> 32};
}

inline uint64_t pack_read_id(int writer, int replicator, int origin,
                             uint64_t idx) {
  return uint64_t(writer) << 52 | uint64_t(replicator) << 42 |
         uint64_t(origin) << 32 | idx;
}

// (receiver, origin, index)
inline std::tuple<int, int, uint64_t> unpack_write_id(uint64_t wr_id) {
  return {wr_id >> 48, (wr_id << 16) >> 48, (wr_id << 32) >> 32};
}

inline uint64_t pack_write_id(int origin, int receiver, uint64_t idx) {
  return uint64_t(receiver) << 48 | uint64_t(origin) << 32 | idx;
}

namespace dory {
namespace neb {
namespace async {

NonEquivocatingBroadcast::~NonEquivocatingBroadcast() {
  stop_operation();
  write(ttd);
}

NonEquivocatingBroadcast::NonEquivocatingBroadcast(int self_id,
                                                   std::vector<int> proc_ids,
                                                   dory::ControlBlock &cb,
                                                   deliver_callback deliver_cb)
    : self_id(self_id),
      process_ids(proc_ids),
      remotes(proc_ids, self_id),
      deliver_cb(deliver_cb),
      cb(cb),
      logger(std_out_logger("NEB")),
      own_next(1),
      pending_reads(0),
      pending_writes(0),
      sign_pool(SIGN_POOL_SIZE, "sig_pool"),
      verify_pool(VERIFY_POOL_SIZE, "verify_pool"),
      post_writer(1, "post_w"),
      post_reader(1, "post_r"),
      replayed(RemotePendingSlots(proc_ids)),
      pending_remote_slots(0) {
  logger->set_level(LOG_LEVEL);
  SPDLOG_LOGGER_INFO(logger, "Creating instance");

  write_wcs.resize(dory::ControlBlock::CQDepth);
  read_wcs.resize(dory::ControlBlock::CQDepth);

  ttd.resize(100000);

  for (size_t i = 0; i < process_ids.size(); i++) {
    process_pos[process_ids[i]] = i;
  }

  auto remote_ids = remotes.ids();

  for (auto &id : *remote_ids) {
    next_msg_idx.insert(std::pair<int, uint64_t>(id, 1));
    next_sig.insert(std::pair<int, uint64_t>(id, 1));
    pending_reads_at[id];
    pending_writes_at[id];
  }

  // the maximum of concurrent pending slots restricts the size of the read
  // buffer
  // TODO(Kristian): increase the memory slot size to accomondate not/late
  // returning reads
  cb.allocateBuffer(REPLAY_R_NAME,
                    100000 * dory::neb::MEMORY_SLOT_SIZE *
                        dory::neb::MAX_CONCURRENTLY_PENDING_SLOTS *
                        remote_ids.get().size() * remote_ids.get().size(),
                    64);
  // we need to register the read buffer in order to obtain a lkey which we use
  // for RDMA reads where the RNIC does local writes.
  cb.registerMR(REPLAY_R_NAME, PD_NAME, REPLAY_R_NAME,
                dory::ControlBlock::LOCAL_WRITE);

  auto r_mr_r = cb.mr(REPLAY_R_NAME);
  replay_r_buf = std::make_unique<MemSlotPool>(*remote_ids, r_mr_r.addr,
                                               r_mr_r.size, r_mr_r.lkey);
}

void NonEquivocatingBroadcast::resize_ttd(std::map<int, int> &num_msgs) {
  int max = 0;
  for (auto &[pid, n] : num_msgs) {
    if (n > max) {
      max = n;
    }
  }

  auto size = (remotes.size() + 1) * (static_cast<size_t>(max) + 1);
  ttd.resize(size);
}

void NonEquivocatingBroadcast::set_connections(ConnectionExchanger &b_ce,
                                               ConnectionExchanger &r_ce) {
  if (b_ce.connections().size() != remotes.size()) {
    throw std::runtime_error(
        "number of broadcast QPs does not match remote processes");
  }

  if (r_ce.connections().size() != remotes.size()) {
    throw std::runtime_error(
        "number of broadcast QPs does not match remote processes");
  }

  remotes.set_connections(b_ce, r_ce);

  size_t plen = process_ids.size();
  auto bcast_conn = remotes.broadcast_connections();
  for (size_t i = 0; i < plen; i++) {
    int pid = process_ids[i];

    if (pid == self_id) {
      auto mr = cb.mr(REPLAY_W_NAME);

      bcast_bufs.insert(std::pair<int, BroadcastBuffer>(
          pid, BroadcastBuffer(mr.addr, mr.size, mr.lkey, process_ids)));
    } else {
      auto &mr = bcast_conn.get().find(pid)->second.get_mr();
      bcast_bufs.insert(std::pair<int, BroadcastBuffer>(
          pid, BroadcastBuffer(mr.addr, mr.size, mr.lkey, process_ids)));
    }
  }
  connected = true;
}

void NonEquivocatingBroadcast::set_remote_keys(
    std::map<int, dory::crypto::dalek::pub_key> &keys) {
  remotes.set_keys(keys);
}

void NonEquivocatingBroadcast::set_remote_keys(
    std::map<int, dory::crypto::dalek::pub_key> &&keys) {
  remotes.set_keys(keys);
}

/* -------------------------------------------------------------------------- */

void NonEquivocatingBroadcast::stop_operation() {
  if (!stopped) {
    stopped = true;

    exit_signal.set_value();
    if (bcast_content_poller_running) bcast_content_poller.join();
    SPDLOG_LOGGER_INFO(logger, "Bcast content poller thread finished");

    if (r_cq_poller_running) r_cq_poller_thread.join();
    SPDLOG_LOGGER_INFO(logger, "Read CQ poller thread finished");

    if (w_cq_poller_running) w_cq_poller_thread.join();
    SPDLOG_LOGGER_INFO(logger, "Write CQ poller thread finished");
  }
}

void NonEquivocatingBroadcast::end() {
  SPDLOG_LOGGER_INFO(logger, "End called");
  stop_operation();
  write(ttd);
}

void NonEquivocatingBroadcast::start() {
  if (started) return;

  if (!connected) {
    throw std::runtime_error(
        "Not connected to remote QPs, cannot start serving");
  }

  auto sf = std::shared_future(exit_signal.get_future());

  bcast_content_poller = std::thread([=] { start_bcast_content_poller(sf); });

  r_cq_poller_thread = std::thread([=] { start_r_cq_poller(sf); });

  w_cq_poller_thread = std::thread([=] { start_w_cq_poller(sf); });

  started = true;
  SPDLOG_LOGGER_INFO(logger, "Started");
}

void NonEquivocatingBroadcast::start_bcast_content_poller(
    std::shared_future<void> f) {
  if (bcast_content_poller_running) return;

  SPDLOG_LOGGER_INFO(logger, "Replayer thread running");

  bcast_content_poller_running = true;
  size_t next_proc = 0;

  while (f.wait_for(std::chrono::seconds(0)) == std::future_status::timeout) {
    next_proc = poll_bcast_bufs(next_proc);
  }
}

// void NonEquivocatingBroadcast::start_bcast_signature_poller(
//     std::shared_future<void> f) {
//   if (bcast_signature_poller_running) return;

//   SPDLOG_LOGGER_INFO(logger, "Replayer thread running");

//   bcast_signature_poller_running = true;

//   while (f.wait_for(std::chrono::seconds(0)) == std::future_status::timeout)
//   {
//     poll_bcast_signatures();
//   }
// }

void NonEquivocatingBroadcast::start_r_cq_poller(std::shared_future<void> f) {
  if (r_cq_poller_running) return;

  SPDLOG_LOGGER_INFO(logger, "Read CQ poller running");

  r_cq_poller_running = true;

  auto &rcq = cb.cq(R_CQ_NAME);

  while (f.wait_for(std::chrono::seconds(0)) == std::future_status::timeout) {
    consume_read_wcs(rcq);
  }
}

void NonEquivocatingBroadcast::start_w_cq_poller(std::shared_future<void> f) {
  if (w_cq_poller_running) return;

  SPDLOG_LOGGER_INFO(logger, "Write CQ poller running");

  w_cq_poller_running = true;

  auto &wcq = cb.cq(W_CQ_NAME);

  while (f.wait_for(std::chrono::seconds(0)) == std::future_status::timeout) {
    consume_write_wcs(wcq);
  }
}

/* -------------------------------------------------------------------------- */

void NonEquivocatingBroadcast::broadcast(uint64_t k, Broadcastable &msg) {
  // BenchTimer timer("broadcast", true);
  if (msg.size() > MSG_PAYLOAD_SIZE) {
    throw std::runtime_error("Allowed message size exceeded");
  }

  auto bcast_buf = bcast_bufs.find(self_id)->second;

  bcast_buf.write(self_id, own_next, k, msg);

  auto slot = bcast_buf.slot(self_id, own_next);

  auto sig = reinterpret_cast<unsigned char *>(
      const_cast<uint8_t *>(slot.signature()));
  auto sign_data = reinterpret_cast<unsigned char *>(slot.addr());

  {
    // dory::BenchTimer timer("signing", true);
    dory::crypto::dalek::sign(sig, sign_data, SLOT_SIGN_DATA_SIZE);
  }

  // // own_next is a member variable, so we rather capture a local one
  // auto sig_next = own_next;
  // // let a worker thread sign the slot and afterwards send the signature
  // // to the remote processes
  // sign_pool.enqueue([=]() {
  //   auto sig = reinterpret_cast<unsigned char *>(
  //       const_cast<uint8_t *>(slot.signature()));
  //   auto sign_data = reinterpret_cast<unsigned char *>(slot.addr());

  //   {
  //     dory::BenchTimer timer("signing", true);
  //     dory::crypto::sign(sig, sign_data, SLOT_SIGN_DATA_SIZE);
  //   }

  //   {
  //     auto remote_ids = remotes.ids();
  //     for (auto pid : *remote_ids) {
  //       post_write(pid, pack_write_id(pid, 0),
  //       reinterpret_cast<uintptr_t>(sig),
  //                  dory::crypto::SIGN_BYTES, bcast_buf.lkey,
  //                  bcast_buf.get_byte_offset(sig_next) +
  //                  SLOT_SIGNATURE_OFFSET);
  //     }
  //   }
  // });

  {
    auto remote_ids = remotes.ids();
    for (auto pid : *remote_ids) {
      post_write(pid, pack_write_id(self_id, pid, own_next), slot.addr(),
                 MEMORY_SLOT_SIZE, bcast_buf.lkey,
                 bcast_buf.get_byte_offset(self_id, own_next));
    }
  }

  // increase the message counter
  own_next++;

  // TODO(Kristian): deliver when consuming majority of write WCs
  // deliver to ourself
  deliver(slot, self_id);
}

inline size_t NonEquivocatingBroadcast::poll_bcast_bufs(size_t next_proc) {
  if (pending_remote_slots >= MAX_CONCURRENTLY_PENDING_SLOTS) {
    SPDLOG_LOGGER_TRACE(logger,
                        "Max concurrent reads. Waiting on broadcast poll cond");

    std::unique_lock<std::mutex> lock(bcast_poll_mux);
    bcast_poll_cond.wait(lock, [&]() {
      return pending_remote_slots < MAX_CONCURRENTLY_PENDING_SLOTS;
    });
  }

  auto remote_ids = remotes.ids();

  for (size_t i = next_proc; i < remote_ids.get().size(); i++) {
    auto origin = remote_ids.get()[i];
    auto next_idx = next_msg_idx[origin].load();

    auto bcast_slot = bcast_bufs.find(origin)->second.slot(origin, next_idx);

    // TODO(Kristian): also incorporate `MAX_CONCURRENTLY_PENDING_PER_PROCESS`
    if (bcast_slot.id() != next_idx) continue;

    ttd[static_cast<size_t>(origin) * next_idx].first =
        std::chrono::steady_clock::now();

    if (!verify_slot(bcast_slot, remotes.key(origin))) {
      SPDLOG_LOGGER_WARN(logger, "bcast slot invalid oid:{},idx:{}", origin,
                         next_idx);
      continue;
    }

    SPDLOG_LOGGER_DEBUG(
        logger, "Bcast from {} at index {} = ({},{}) is signed {}", origin,
        next_idx, bcast_slot.id(),
        *reinterpret_cast<const volatile uint64_t *>(bcast_slot.content()),
        bcast_slot.has_signature());

    // NOTE: we create here a tracker for the current processing message. This
    // needs to be here, so that the other threads can deduce the delivery
    // of the message when the tracker is not present anymore.
    auto tracker = replayed.get(origin).insert(next_idx);

    tracker.get().sig_valid = true;

    pending_remote_slots++;

    auto bcast_buf = bcast_bufs.find(self_id)->second;
    auto replay_slot = bcast_buf.slot(origin, next_idx);

    bcast_slot.copy_to(replay_slot);
    {
      auto remote_ids = remotes.ids();
      for (auto pid : *remote_ids) {
        post_write(pid, pack_write_id(origin, pid, next_idx), bcast_slot.addr(),
                   dory::neb::MEMORY_SLOT_SIZE, bcast_buf.lkey,
                   bcast_buf.get_byte_offset(origin, next_idx));
      }
    }

    next_msg_idx[origin]++;

    // we return here so we relase the remote share lock and wait on the
    // condition variable at the beginning of this function
    if (pending_remote_slots >= MAX_CONCURRENTLY_PENDING_SLOTS) {
      return (i + 1) % remote_ids.get().size();
    }
  }

  return 0;
}

inline void NonEquivocatingBroadcast::consume_write_wcs(
    dory::deleted_unique_ptr<ibv_cq> &cq) {
  write_wcs.resize(pending_writes);

  if (cb.pollCqIsOK(cq, write_wcs)) {
    if (write_wcs.size() > 0) {
      SPDLOG_LOGGER_DEBUG(logger, "WRITE CQ polled, size: {}",
                          write_wcs.size());
      {
        std::unique_lock<std::mutex> lock(write_mux);
        pending_writes -= static_cast<unsigned>(write_wcs.size());
        write_cond.notify_one();
      }
    }

    for (auto &wc : write_wcs) {
      auto const &[receiver, target, idx] = unpack_write_id(wc.wr_id);

      pending_writes_at[receiver]--;

      switch (wc.status) {
        case IBV_WC_SUCCESS:
          handle_write_ack(target, idx);
          break;
        default:
          SPDLOG_LOGGER_CRITICAL(logger, "WC for WRITE at {} has status {}",
                                 receiver, wc.status);
      }
    }
  }
}

inline void NonEquivocatingBroadcast::handle_write_ack(int target,
                                                       uint64_t idx) {
  // SPDLOG_LOGGER_DEBUG(logger, "ACK for tid: {}, idx: {}", target, idx);
  write_acks[target][idx]++;

  // TODO(Kristian): handle the self_id case
  // we clould notify the broadcast routine here

  // we only require the minority as the local process adds up to a majority
  if (write_acks[target][idx] == minority(process_ids.size()) &&
      target != self_id) {
    SPDLOG_LOGGER_TRACE(
        logger, "got majority acks for write - oid: {}, idx: {}", target, idx);

    // if there is only one remote process which is the message sender,
    // then we can directly deliver without replay reading
    // TODO(Kristian): unlikely
    if (remotes.replay_quorum_size() == 0 &&
        remotes.replay_connection(target)) {
      if (replayed.get(target).remove(idx)) {
        auto bcast_slot = bcast_bufs.find(self_id)->second.slot(target, idx);
        deliver(bcast_slot, target);
      }
      return;
    }

    read_replay_registers_for(target, idx);
  }
}

inline void NonEquivocatingBroadcast::read_replay_registers_for(int origin,
                                                                uint64_t idx) {
  auto remote_ids = remotes.ids();

  for (int writer : *remote_ids) {
    if (writer == origin) continue;

    for (int replicator : *remote_ids) {
      auto r_slot = replay_r_buf->slot(origin, writer, replicator, idx);
      size_t writer_offset = dory::neb::BUFFER_SIZE * process_ids.size() *
                             process_pos.find(writer)->second;
      // TODO(Kristian): encode had_sig for the fastpath
      post_read(
          replicator, pack_read_id(writer, replicator, origin, idx),
          r_slot.addr(), MEMORY_SLOT_SIZE, replay_r_buf->lkey(),
          writer_offset +
              bcast_bufs.find(self_id)->second.get_byte_offset(origin, idx));
    }
  }
}

inline void NonEquivocatingBroadcast::consume_read_wcs(
    dory::deleted_unique_ptr<ibv_cq> &cq) {
  read_wcs.resize(pending_reads);

  if (!cb.pollCqIsOK(cq, read_wcs)) {
    SPDLOG_LOGGER_WARN(logger, "Polling on read CQ failed!");
    return;
  }

  if (read_wcs.size() > 0) {
    SPDLOG_LOGGER_DEBUG(logger, "READ CQ polled, size: {}", read_wcs.size());
    {
      std::unique_lock<std::mutex> lock(read_mux);
      pending_reads -= read_wcs.size();
      read_cond.notify_one();
    }
  }

  for (auto const &wc : read_wcs) {
    auto const &[wid, rid, oid, idx] = unpack_read_id(wc.wr_id);
    pending_reads_at[rid]--;

    switch (wc.status) {
      case IBV_WC_SUCCESS:
        handle_replicator_read(wid, rid, oid, idx);
        break;
      default: {
        replay_r_buf->free(wid, oid, rid, idx);
        SPDLOG_LOGGER_CRITICAL(
            logger, "WC for READ at {} for (w={},t={},idx={}) has status {}",
            rid, wid, oid, idx, wc.status);
      }
    }
  }
}

inline void NonEquivocatingBroadcast::handle_replicator_read(int wid, int rid,
                                                             int oid,
                                                             uint64_t idx) {
  SPDLOG_LOGGER_DEBUG(
      logger,
      "WC for READ at replicator {} for write from {} over target ({},{}) ",
      rid, wid, oid, idx);

  auto &swmr_reg = registers[wid][oid][idx];

  if (swmr_reg.completed) {
    replay_r_buf->free(wid, oid, rid, idx);
    return;
  }

  swmr_reg.readlist.push_back(rid);
  // we got a majority when including us, so we check only for a minority
  if (swmr_reg.readlist.size() == minority(process_ids.size())) {
    SPDLOG_LOGGER_INFO(logger, "Got majority for wid:{}, oid:{}, idx:{}", wid,
                       oid, idx);

    auto slot = bcast_bufs.find(wid)->second.slot(oid, idx);
    int replicator = self_id;

    for (auto p : swmr_reg.readlist) {
      auto rslot = replay_r_buf->slot(oid, wid, p, idx);

      if (slot.has_same_data_content_as(rslot)) {
        replay_r_buf->free(wid, oid, p, idx);
        continue;
      }

      // TODO(Kristian):check for signature and reassign in case we found one
      // with sig
      // we found some written slot, assign to the empty default
      if (slot.id() == 0) {
        slot = rslot;
        replicator = p;
        // bot slots are not empty and they don't have same contents
      } else if (rslot.id() != 0) {
        throw std::runtime_error("conflicting slots, should not happen atm!");
      } else {
        // an empty tmp slot can be released
        replay_r_buf->free(wid, oid, p, idx);
      }
    }

    // we came to the end and either nothing was written yet, or we return
    // a slot with valid signature
    if (slot.id() == idx || slot.id() == 0) {
      handle_register_read(wid, replicator, oid, idx, slot);
    } else {
      SPDLOG_LOGGER_CRITICAL(
          logger, "slot is: ({},{}) for {},{},{}", slot.id(),
          *reinterpret_cast<const volatile uint64_t *>(slot.content()), wid,
          oid, idx);
    }

    // gc
    for (auto p : swmr_reg.readlist) {
      replay_r_buf->free(wid, oid, p, idx);
    }

    swmr_reg.set_complete();
  }
}

inline void NonEquivocatingBroadcast::handle_register_read(
    int wid, int rid, int oid, uint64_t idx,
    optional_ref<MemorySlot> opt_slot) {
  SPDLOG_LOGGER_DEBUG(logger,
                      "register read wid: {}, oid:{}, idx:{} - hasVal: {}", wid,
                      oid, idx, opt_slot.has_value());
  auto &p = replayed.get(oid);

  if (auto r = p.get(idx)) {
    auto &tracker = r->get();

    auto own_slot = bcast_bufs.find(self_id)->second.slot(oid, idx);

    // a nullopt indicates that the origin tired to equivocate on the register
    // level
    if (!opt_slot) {
      throw std::runtime_error("This should not happen atm!");
    } else if (own_slot.has_same_data_content_as(*opt_slot)) {
      tracker.add_to_match_reads(wid);
    } else {
      if (opt_slot.value().get().id() == 0) tracker.add_to_empty_reads(wid);
    }

    // ensure we free the tmp slot before delivering so it can be reused
    // concurrently
    if (rid != self_id) {
      replay_r_buf->free(wid, oid, rid, idx);
    }

    try_deliver(own_slot, oid, idx);
  }
}

inline bool NonEquivocatingBroadcast::verify_slot(
    MemorySlot &slot, dory::crypto::dalek::pub_key &key) {
  auto sig = reinterpret_cast<unsigned char *>(
      const_cast<uint8_t *>(slot.signature()));
  auto msg = reinterpret_cast<unsigned char *>(slot.addr());

  // dory::BenchTimer timer("verifying sig", true);
  return dory::crypto::dalek::verify(sig, msg, SLOT_SIGN_DATA_SIZE, key);
}

inline void NonEquivocatingBroadcast::post_write(int pid, uint64_t wrid,
                                                 uintptr_t lbuf, uint32_t lsize,
                                                 uint32_t lkey,
                                                 size_t roffset) {
  post_writer.enqueue([=]() {
    if (pending_writes + 1 > dory::ControlBlock::CQDepth) {
      SPDLOG_LOGGER_TRACE(logger, "waiting for write cond");

      std::unique_lock<std::mutex> lock(write_mux);
      write_cond.wait(lock, [&] {
        return pending_writes + 1 <= dory::ControlBlock::CQDepth;
      });

      SPDLOG_LOGGER_TRACE(logger, "write cond satisfied, now {}",
                          pending_writes);
    }

    if (auto rc = remotes.broadcast_connection(pid)) {
      auto ret = rc->get().postSendSingle(
          ReliableConnection::RdmaWrite, wrid, reinterpret_cast<void *>(lbuf),
          lsize, lkey, rc->get().remoteBuf() + roffset);
      if (!ret) {
        auto const &[receiver, target, idx] = unpack_write_id(wrid);
        SPDLOG_LOGGER_WARN(
            logger,
            "POST WRITE at {} for ({},{}) failed, current pending writes: {}",
            receiver, target, idx, pending_writes);

      } else {
        pending_writes++;
        pending_writes_at[pid]++;
      }
    }
  });
}

inline void NonEquivocatingBroadcast::post_read(int pid, uint64_t wrid,
                                                uintptr_t lbuf, uint32_t lsize,
                                                uint32_t lkey, size_t roffset) {
  post_reader.enqueue([=]() {
    if (pending_reads + 1 > dory::ControlBlock::CQDepth) {
      SPDLOG_LOGGER_TRACE(logger, "waiting for read cond");

      std::unique_lock<std::mutex> lock(read_mux);
      read_cond.wait(lock, [&] {
        return pending_reads + 1 <= dory::ControlBlock::CQDepth;
      });

      SPDLOG_LOGGER_TRACE(logger, "read cond satisfied, now {}", pending_reads);
    }

    if (auto rc = remotes.replay_connection(pid)) {
      auto ret = rc->get().postSendSingle(
          ReliableConnection::RdmaRead, wrid, reinterpret_cast<void *>(lbuf),
          lsize, lkey, rc->get().remoteBuf() + roffset);
      if (!ret) {
        auto const &[rid, wid, tid, mid] = unpack_read_id(wrid);
        SPDLOG_LOGGER_WARN(
            logger,
            "POST READ at {} for (w={},t={},mid={}) failed, current "
            "pending reads: {}",
            rid, wid, tid, mid, pending_reads);

      } else {
        pending_reads++;
        pending_reads_at[pid]++;
      }
    }
  });
}

inline void NonEquivocatingBroadcast::deliver(MemorySlot &slot, int origin) {
  std::unique_lock<std::mutex> lock(deliver_mux);

  if (origin != self_id) {
    ttd[static_cast<size_t>(origin) * slot.id()].second =
        std::chrono::steady_clock::now();

    {
      std::unique_lock<std::mutex> lock(bcast_poll_mux);
      pending_remote_slots--;
      bcast_poll_cond.notify_one();
    }

    SPDLOG_LOGGER_DEBUG(logger, "Pending slots: {}", pending_remote_slots);
  }

  deliver_cb(slot.id(), slot.content(), origin);
}

inline void NonEquivocatingBroadcast::try_deliver(MemorySlot &slot, int origin,
                                                  uint64_t idx) {
  if (replayed.get(origin).remove_if_complete(idx,
                                              remotes.replay_quorum_size())) {
    deliver(slot, origin);
  }
}

}  // namespace async
}  // namespace neb
}  // namespace dory