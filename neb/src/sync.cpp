#include <fstream>
#include <future>
#include <limits>

#include <dory/conn/exchanger.hpp>
#include <dory/conn/wr-builder.hpp>
#include <dory/shared/bench.hpp>
#include <dory/shared/unused-suppressor.hpp>
#include <dory/store.hpp>

#include "consts.hpp"
#include "sync.hpp"

// TODO(Kristian): use uint32_t for message id
inline uint64_t unpack_msg_id(uint64_t wr_id) { return (wr_id << 32) >> 32; }
inline std::tuple<int, int, uint64_t> unpack_read_id(uint64_t wr_id) {
  return {wr_id >> 48, (wr_id << 16) >> 48, unpack_msg_id(wr_id)};
}
inline uint64_t pack_read_id(int replayer, int origin, uint64_t msg_id) {
  return uint64_t(replayer) << 48 | uint64_t(origin) << 32 | msg_id;
}
inline int unpack_receiver_id(uint64_t wr_id) {
  return static_cast<int>(wr_id >> 48);
}
inline std::pair<int, uint64_t> unpack_write_id(uint64_t wr_id) {
  return {unpack_receiver_id(wr_id), unpack_msg_id(wr_id)};
}
inline uint64_t pack_write_id(int receiver, uint64_t msg_id) {
  return (uint64_t(receiver) << 48) | msg_id;
}

inline void write(
    std::vector<std::pair<std::chrono::steady_clock::time_point,
                          std::chrono::steady_clock::time_point>> &ttd) {
  std::ofstream fs;
  fs.open("/tmp/neb-bench-lat");
  for (auto &p : ttd) {
    auto diff = std::chrono::duration<double, std::micro>(p.second - p.first);
    if (diff.count() > 0) fs << diff.count() << "\n";
  }
}

namespace dory {

using namespace neb;

NonEquivocatingBroadcast::~NonEquivocatingBroadcast() {
  stop_operation();
  write(ttd);
}  // namespace dory

NonEquivocatingBroadcast::NonEquivocatingBroadcast(int self_id,
                                                   std::vector<int> remote_ids,
                                                   dory::ControlBlock &cb,
                                                   deliver_callback deliver_cb)
    : self_id(self_id),
      remotes{remote_ids},
      deliver_cb(deliver_cb),
      cb(cb),
      logger(std_out_logger("NEB")),
      own_next(1),
      pending_reads(0),
      pending_writes(0),
      sign_pool(SIGN_POOL_SIZE),
      verify_pool(VERIFY_POOL_SIZE),
      post_writer(1),
      post_reader(1),
      replayed(RemotePendingSlots(remote_ids)),
      pending_remote_slots(0) {
  logger->set_level(LOG_LEVEL);
  SPDLOG_LOGGER_INFO(logger, "Creating instance");

  write_wcs.resize(dory::ControlBlock::CQDepth);
  read_wcs.resize(dory::ControlBlock::CQDepth);

  ttd.resize(100000);

  for (auto &id : remote_ids) {
    pending_reads_at[id];
    pending_writes_at[id];
  }

  auto proc_ids = remote_ids;
  proc_ids.push_back(self_id);

  for (auto &id : proc_ids) {
    next_msg_idx.insert(std::pair<int, uint64_t>(id, 1));
    next_sig.insert(std::pair<int, uint64_t>(id, 1));
  }

  auto r_mr_w = cb.mr(REPLAY_W_NAME);
  replay_w_buf =
      std::make_unique<ReplayBufferWriter>(r_mr_w.addr, r_mr_w.size, proc_ids);

  // the maximum of concurrent pending slots restricts the size of the read
  // buffer
  cb.allocateBuffer(REPLAY_R_NAME,
                    dory::neb::MEMORY_SLOT_SIZE *
                        dory::neb::MAX_CONCURRENTLY_PENDING_SLOTS *
                        remote_ids.size(),
                    64);
  // we need to register the read buffer in order to obtain a lkey which we use
  // for RDMA reads where the RNIC does local writes.
  cb.registerMR(REPLAY_R_NAME, PD_NAME, REPLAY_R_NAME,
                dory::ControlBlock::LOCAL_WRITE);

  auto r_mr_r = cb.mr(REPLAY_R_NAME);
  replay_r_buf = std::make_unique<ReplayBufferReader>(remote_ids, r_mr_r.addr,
                                                      r_mr_r.lkey);

  // Insert a buffer which will be used as scatter when post sending writes
  auto b_mr = cb.mr(BCAST_W_NAME);
  bcast_bufs.insert(std::pair<int, BroadcastBuffer>(
      self_id, BroadcastBuffer(b_mr.addr, b_mr.size, b_mr.lkey)));
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

  {
    auto bcast_conn = remotes.broadcast_connections();
    for (auto &[pid, rc] : bcast_conn.get()) {
      auto &mr = rc.get_mr();
      bcast_bufs.insert(std::pair<int, BroadcastBuffer>(
          pid, BroadcastBuffer(mr.addr, mr.size, mr.lkey)));
    }
  }
  connected = true;
}

void NonEquivocatingBroadcast::set_remote_keys(
    std::map<int, dory::crypto::sodium::pub_key> &keys) {
  remotes.set_keys(keys);
}

void NonEquivocatingBroadcast::set_remote_keys(
    std::map<int, dory::crypto::sodium::pub_key> &&keys) {
  remotes.set_keys(keys);
}

/* -------------------------------------------------------------------------- */

void NonEquivocatingBroadcast::stop_operation() {
  if (!stopped) {
    stopped = true;

    exit_signal.set_value();
    if (bcast_content_poller_running) bcast_content_poller.join();
    SPDLOG_LOGGER_INFO(logger, "Bcast content poller thread finished");

    if (bcast_signature_poller_running) bcast_signature_poller.join();
    SPDLOG_LOGGER_INFO(logger, "Bcast signature poller thread finished");

    if (cq_poller_running) cq_poller_thread.join();
    SPDLOG_LOGGER_INFO(logger, "CQ poller thread finished");
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
  bcast_signature_poller =
      std::thread([=] { start_bcast_signature_poller(sf); });

  cq_poller_thread = std::thread([=] { start_cq_poller(sf); });

  started = true;
  SPDLOG_LOGGER_INFO(logger, "Started");
}

void NonEquivocatingBroadcast::start_bcast_content_poller(
    std::shared_future<void> f) {
  if (bcast_content_poller_running) return;

  SPDLOG_LOGGER_INFO(logger, "Replayer thread running");

  bcast_content_poller_running = true;

  while (f.wait_for(std::chrono::seconds(0)) == std::future_status::timeout) {
    poll_bcast_bufs();
  }
}

void NonEquivocatingBroadcast::start_bcast_signature_poller(
    std::shared_future<void> f) {
  if (bcast_signature_poller_running) return;

  SPDLOG_LOGGER_INFO(logger, "Replayer thread running");

  bcast_signature_poller_running = true;

  while (f.wait_for(std::chrono::seconds(0)) == std::future_status::timeout) {
    poll_bcast_signatures();
  }
}

void NonEquivocatingBroadcast::start_cq_poller(std::shared_future<void> f) {
  if (cq_poller_running) return;

  SPDLOG_LOGGER_INFO(logger, "Replay reader thread running");

  cq_poller_running = true;

  auto &rcq = cb.cq(REPLAY_CQ_NAME);
  auto &wcq = cb.cq(BCAST_CQ_NAME);

  while (f.wait_for(std::chrono::seconds(0)) == std::future_status::timeout) {
    consume_read_wcs(rcq);
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

  bcast_buf.write(own_next, k, msg);

  auto slot = bcast_buf.slot(own_next);
  // own_next is a member variable, so we rather capture a local one
  auto sig_next = own_next;
  // let a worker thread sign the slot and afterwards send the signature
  // to the remote processes
  sign_pool.enqueue([=]() {
    auto sig = reinterpret_cast<unsigned char *>(
        const_cast<uint8_t *>(slot.signature()));
    auto sign_data = reinterpret_cast<unsigned char *>(slot.addr());

    {
      dory::BenchTimer timer("signing", true);
      dory::crypto::sodium::sign(sig, sign_data, SLOT_SIGN_DATA_SIZE);
    }

    {
      auto remote_ids = remotes.ids();
      for (auto pid : remote_ids.get()) {
        post_write(pid, pack_write_id(pid, 0), reinterpret_cast<uintptr_t>(sig),
                   dory::crypto::sodium::SIGN_BYTES, bcast_buf.lkey,
                   bcast_buf.get_byte_offset(sig_next) + SLOT_SIGNATURE_OFFSET);
      }
    }
  });

  {
    auto remote_ids = remotes.ids();
    for (auto pid : remote_ids.get()) {
      post_write(pid, pack_write_id(pid, own_next), slot.addr(),
                 SLOT_SIGN_DATA_SIZE, bcast_buf.lkey,
                 bcast_buf.get_byte_offset(own_next));
    }
  }

  // increase the message counter
  own_next++;

  // deliver to ourself
  auto dummy = SlotTracker();
  deliver(slot, dummy, self_id);
}

inline void NonEquivocatingBroadcast::poll_bcast_signatures() {
  auto remote_ids = remotes.ids();

  for (auto origin : remote_ids.get()) {
    auto next = next_sig[origin];
    // ensure we only verify a signature if also the content is already replayed
    if (next >= next_msg_idx[origin]) continue;

    auto bcast_slot = bcast_bufs.find(origin)->second.slot(next);

    if (bcast_slot.id() == 0 || bcast_slot.id() != next) continue;

    if (bcast_slot.has_signature()) {
      auto replay_slot = replay_w_buf->slot(origin, next);

      bcast_slot.copy_signature_to(replay_slot);
      // verify the signature by a worker thread who additionally tries to
      // deliver in case we were only waiting for validating the signature
      verify_pool.enqueue([=]() {
        auto r_slot = replay_w_buf->slot(origin, next);
        auto is_valid = verify_slot(r_slot, remotes.key(origin));
        SPDLOG_LOGGER_DEBUG(logger, "Verified sig pid,idx={},{}. Is valid {}",
                            origin, next, is_valid);

        auto &p = replayed.get(origin);

        if (auto tracker = p.get(next)) {
          tracker->get().processed_sig = true;
          tracker->get().sig_valid = is_valid;

          try_deliver(r_slot, tracker->get(), origin, next);
        }
      });

      next_sig[origin]++;
    }
  }
}

inline void NonEquivocatingBroadcast::poll_bcast_bufs() {
  if (pending_remote_slots >= MAX_CONCURRENTLY_PENDING_SLOTS) {
    SPDLOG_LOGGER_TRACE(logger,
                        "Max concurrent reads. Waiting on broadcast poll cond");

    std::unique_lock<std::mutex> lock(bcast_poll_mux);
    bcast_poll_cond.wait(lock, [&]() {
      return pending_remote_slots < MAX_CONCURRENTLY_PENDING_SLOTS;
    });
  }

  auto remote_ids = remotes.ids();
  for (auto origin : remote_ids.get()) {
    auto &next_idx = next_msg_idx[origin];

    auto bcast_slot = bcast_bufs.find(origin)->second.slot(next_idx);

    if (bcast_slot.id() == 0 || bcast_slot.id() != next_idx) continue;

    SPDLOG_LOGGER_DEBUG(
        logger, "Bcast from {} at index {} = ({},{}) is signed {}", origin,
        next_idx, bcast_slot.id(),
        *reinterpret_cast<const volatile uint64_t *>(bcast_slot.content()),
        bcast_slot.has_signature());

    auto replay_slot_w = replay_w_buf->slot(origin, next_idx);

    bcast_slot.copy_to(replay_slot_w);

    // NOTE: we create here a tracker for the current processing message. This
    // needs to be here, so that the other threads can deduce the delivery
    // of the message when the tracker is not present anymore.
    auto tracker = replayed.get(origin).insert(next_idx);
    pending_remote_slots++;

    ttd[static_cast<uint64_t>(origin) * next_idx].first =
        std::chrono::steady_clock::now();

    // if there is only one remote process which is the message sender,
    // then we can directly deliver without replay reading
    if (remotes.replay_quorum_size() == 0 &&
        remotes.replay_connection(origin)) {
      deliver(bcast_slot, tracker, origin);
      replayed.get(origin).remove(next_idx);

      next_msg_idx[origin]++;
      continue;
    }

    {
      auto remote_ids = remotes.ids();
      for (auto replayer : remote_ids.get()) {
        if (replayer == origin) continue;

        auto replay_r_slot = replay_r_buf->slot(origin, replayer, next_idx);
        post_read(replayer, pack_read_id(replayer, origin, next_idx),
                  replay_r_slot.addr(), MEMORY_SLOT_SIZE, replay_r_buf->lkey,
                  replay_w_buf->get_byte_offset(origin, next_idx));
      }
    }
    next_msg_idx[origin]++;

    // we return here so we relase the remote share lock and wait on the
    // condition variable at the beginning of this function
    if (pending_remote_slots >= MAX_CONCURRENTLY_PENDING_SLOTS) return;
  }
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
      }
      write_cond.notify_one();
    }

    for (auto &wc : write_wcs) {
      auto const &[receiver, msg_idx] = unpack_write_id(wc.wr_id);
      dory::IGNORE(msg_idx);
      pending_writes_at[receiver]--;

      switch (wc.status) {
        case IBV_WC_SUCCESS:
          break;
        case IBV_WC_RETRY_EXC_ERR:
          SPDLOG_LOGGER_INFO(logger,
                             "WC WRITE: Process {} not responding, removing",
                             receiver);
          remove_remote(receiver);

          break;
        default:
          SPDLOG_LOGGER_INFO(logger, "WC for WRITE at {} has status {}",
                             receiver, wc.status);
      }
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
    }
    read_cond.notify_one();
  }

  for (auto const &wc : read_wcs) {
    auto const &[r_id, o_id, idx] = unpack_read_id(wc.wr_id);
    pending_reads_at[r_id]--;

    switch (wc.status) {
      case IBV_WC_SUCCESS:
        handle_replay_read(r_id, o_id, idx);
        break;
      case IBV_WC_RETRY_EXC_ERR:
        replay_r_buf->free(r_id, o_id, idx);
        SPDLOG_LOGGER_INFO(
            logger, "WC read; Process {} not responding, removing!", r_id);
        remove_remote(r_id);
        break;
      default: {
        replay_r_buf->free(r_id, o_id, idx);
        SPDLOG_LOGGER_WARN(logger,
                           "WC for READ at {} for ({},{}) has status {}", r_id,
                           o_id, idx, wc.status);
      }
    }
  }
}

inline void NonEquivocatingBroadcast::handle_replay_read(int r_id, int o_id,
                                                         uint64_t idx) {
  SPDLOG_LOGGER_DEBUG(logger, "WC for READ at {} for ({},{}) ", r_id, o_id,
                      idx);

  auto &p = replayed.get(o_id);

  auto r = p.get(idx);

  if (!r) {
    return;
  }

  auto &tracker = r->get();

  auto rr_slot = replay_r_buf->slot(o_id, r_id, idx);
  auto own_slot = replay_w_buf->slot(o_id, idx);

  // got no response, this may happen when the remote is not ready yet
  // and didn't initialize its memory buffer
  if (rr_slot.id() == 0) {
    SPDLOG_LOGGER_WARN(logger, "Slot was not successfully read");
    // TODO(Kristian): we should probably retry!
  } else if (rr_slot.id() == std::numeric_limits<uint64_t>::max()) {
    // got a response but nothing is replayed
    // empty reads don't conflict so we can add the replayer to the quorum
    SPDLOG_LOGGER_TRACE(logger, "nothing replayed");
    tracker.add_to_empty_reads(r_id);
    replay_r_buf->free(r_id, o_id, idx);
  } else {
    // got a response and the replay slot was written, so we check for
    // matching contents, otherwise we need to check signatures
    if (rr_slot.has_same_data_content_as(own_slot)) {
      tracker.add_to_match_reads(r_id);
      replay_r_buf->free(r_id, o_id, idx);
    } else {
      // Only now we'll verify the signatures, as the operation is more costly
      // than comparing the slot contents. Anyhow, we want to verify the
      // signature in order to know if the origin broadcaster tried to
      // equivocate or we can count the current processing replayer towards
      // the quorum.
      if (tracker.processed_sig) {
        SPDLOG_LOGGER_TRACE(logger, "Local signature for ({},{},{}) exists",
                            o_id, r_id, idx);
        if (!tracker.sig_valid) {
          SPDLOG_LOGGER_TRACE(logger, "Local signature for ({},{},{}) invalid",
                              o_id, r_id, idx);
          replay_r_buf->free(r_id, o_id, idx);
          return;
        }

        SPDLOG_LOGGER_TRACE(logger, "Local signature for ({},{},{}) is valid",
                            o_id, r_id, idx);
        if (!rr_slot.has_signature()) {
          SPDLOG_LOGGER_TRACE(logger,
                              "Remote signature for ({},{},{}) not present",
                              o_id, r_id, idx);
          if (!tracker.conflict_includes(r_id)) {
            SPDLOG_LOGGER_TRACE(logger,
                                "Will retry reading ({},{},{}) once more", o_id,
                                r_id, idx);
            // we need to re-post a read once more as the remote might have
            // also read an slot without a signature in our buffer in which
            // case we could violate agreement when adding the replayer
            // immediatelly to the quorum. This way we ensure that at least
            // one process will see the signature of the other and in case
            // of an attempt to equivocate by the broadcaster one process
            // will not add the other to the quorum and thus never deliver
            // the conflicting message.
            tracker.add_to_conflicts(r_id);
            post_read(r_id, pack_read_id(r_id, o_id, idx), rr_slot.addr(),
                      MEMORY_SLOT_SIZE, replay_r_buf->lkey,
                      replay_w_buf->get_byte_offset(o_id, idx));
          } else {
            SPDLOG_LOGGER_TRACE(
                logger,
                "Sill no remote signature for ({},{},{}). Adding to quorum",
                o_id, r_id, idx);
            // We resend the request already once more to see if in the
            // meanwhile the signature was written at the remote process
            // r_id. If it still not present, then we can include this
            // replayer to the quorum as if he is correct, then he will
            // re-try also once and then see that we have a valid signature
            // so he won't ever deliver his version of the message at the
            // current index.
            tracker.add_to_empty_reads(r_id);
            replay_r_buf->free(r_id, o_id, idx);
          }
        } else {
          verify_pool.enqueue(
              [=]() { verify_and_act_on_remote_sig(o_id, r_id, idx); });
        }
      } else {
        SPDLOG_LOGGER_TRACE(logger,
                            "No local processed signature for ({},{},{})", o_id,
                            r_id, idx);
        if (!rr_slot.has_signature()) {
          SPDLOG_LOGGER_TRACE(
              logger, "No remote signature for ({},{},{}). Re-posting read!",
              o_id, r_id, idx);
          // At this point we don't have any signature but conflicting slots.
          // We need at least one signature in order to be able to know if the
          // broadcaster or replayer is byzantine. Therefore, we re-post a
          // read request (only for the signature).
          post_read(r_id, pack_read_id(r_id, o_id, idx),
                    rr_slot.addr() + SLOT_SIGNATURE_OFFSET, MEMORY_SLOT_SIZE,
                    replay_r_buf->lkey,
                    replay_w_buf->get_byte_offset(o_id, idx));
          // NOTE: when re-posting a read infinitelly often we open up an
          // attack vector for a byzantine broadcaster (or replayer) who never
          // includes a signature. This will slow us down as we need to
          // process messages that will never be delivered.
        } else {
          SPDLOG_LOGGER_TRACE(logger, "Remote signature for ({},{},{}) exists",
                              o_id, r_id, idx);

          verify_pool.enqueue(
              [=]() { verify_and_act_on_remote_sig(o_id, r_id, idx); });
        }
      }
    }
  }

  try_deliver(own_slot, tracker, o_id, idx);
}

inline void NonEquivocatingBroadcast::remove_remote(int pid) {
  remotes.remote_remote(pid);

  auto &p_reads = pending_reads_at[pid];
  if (p_reads > 0) {
    {
      std::unique_lock<std::mutex> lock(read_mux);
      pending_reads -= pending_reads_at[pid];
    }
    read_cond.notify_one();
  }

  auto &p_writes = pending_writes_at[pid];
  if (p_writes > 0) {
    {
      std::unique_lock<std::mutex> lock(write_mux);
      pending_writes -= static_cast<unsigned>(pending_writes_at[pid]);
    }
    write_cond.notify_one();
  }

  for (auto &[origin, tracker_map] : replayed) {
    if (origin == pid) continue;

    auto quorum_size = remotes.replay_quorum_size();
    for (auto &[id, tracker] :
         tracker_map.deliverable_after_remove_of(pid, quorum_size)) {
      auto slot = bcast_bufs.find(origin)->second.slot(id);
      deliver(slot, *tracker, origin);
      tracker_map.remove(id);
    }
  }
}

inline bool NonEquivocatingBroadcast::verify_slot(
    MemorySlot &slot, dory::crypto::sodium::pub_key &key) {
  auto sig = reinterpret_cast<unsigned char *>(
      const_cast<uint8_t *>(slot.signature()));
  auto msg = reinterpret_cast<unsigned char *>(slot.addr());
  auto k = key.get();

  dory::BenchTimer timer("verifying sig", true);
  return dory::crypto::sodium::verify(sig, msg, SLOT_SIGN_DATA_SIZE, k);
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

    if (auto rc = remotes.broadcast_conneciton(pid)) {
      auto ret = rc->get().postSendSingle(
          ReliableConnection::RdmaWrite, wrid, reinterpret_cast<void *>(lbuf),
          lsize, lkey, rc->get().remoteBuf() + roffset);
      if (!ret) {
        auto const &[pid, m_id] = unpack_write_id(wrid);
        SPDLOG_LOGGER_WARN(
            logger,
            "POST WRITE at {} for {} returned {}, current pending writes: {}",
            pid, m_id, pending_writes);

      } else {
        pending_writes++;
        pending_writes_at[pid]++;
        SPDLOG_LOGGER_DEBUG(logger, "Pending writes: {}, at {}: {}",
                            pending_writes, pid, pending_writes_at[pid]);
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
        auto const &[r_id, o_id, m_id] = unpack_read_id(wrid);
        SPDLOG_LOGGER_WARN(logger,
                           "POST READ at {} for ({},{}) returned {}, current "
                           "pending reads: {}",
                           r_id, o_id, m_id, ret, pending_reads);

      } else {
        pending_reads++;
        pending_reads_at[pid]++;
        SPDLOG_LOGGER_DEBUG(logger, "Pending reads: {}, at {}: {}",
                            pending_reads, pid, pending_reads_at[pid]);
      }
    }
  });
}

inline void NonEquivocatingBroadcast::deliver(MemorySlot &slot,
                                              SlotTracker &tracker,
                                              int origin) {
  deliver_mux.lock();
  // the deliver mutex syncronizes not only access to the upper layer, but also
  // ensures that when two theads race to deliver a slot, it'll be delivered
  // only once. as this check will pass only once.
  if (!tracker.delivered) {
    tracker.delivered = true;

    if (origin != self_id) {
      ttd[static_cast<uint64_t>(origin) * slot.id()].second =
          std::chrono::steady_clock::now();

      {
        std::unique_lock<std::mutex> lock(bcast_poll_mux);
        pending_remote_slots--;
      }
      bcast_poll_cond.notify_one();

      SPDLOG_LOGGER_DEBUG(logger, "Pending slots: {}", pending_remote_slots);
    }

    deliver_cb(slot.id(), slot.content(), origin);
  }
  deliver_mux.unlock();
}

inline void NonEquivocatingBroadcast::try_deliver(MemorySlot &slot,
                                                  SlotTracker &tracker,
                                                  int origin, uint64_t idx) {
  if (tracker.has_quorum_of(remotes.replay_quorum_size()) &&
      ((tracker.needs_valid_local_sig && tracker.sig_valid) ||
       !tracker.needs_valid_local_sig)) {
    deliver(slot, tracker, origin);

    replayed.get(origin).remove(idx);
  }
}

inline void NonEquivocatingBroadcast::verify_and_act_on_remote_sig(
    int o_id, int r_id, uint64_t idx) {
  if (auto tracker = replayed.get(o_id).get(idx)) {
    auto rr_slot = replay_r_buf->slot(o_id, r_id, idx);
    bool replay_sig_valid = verify_slot(rr_slot, remotes.key(o_id));

    if (!replay_sig_valid) {
      SPDLOG_LOGGER_TRACE(logger, "Remote signature for ({},{},{}) not valid",
                          o_id, r_id, idx);
      tracker->get().add_to_empty_reads(r_id);
      replay_r_buf->free(r_id, o_id, idx);
      auto slot = bcast_bufs.find(o_id)->second.slot(idx);
      try_deliver(slot, tracker->get(), o_id, idx);

    } else {
      replay_r_buf->free(r_id, o_id, idx);
      SPDLOG_LOGGER_TRACE(
          logger,
          "Remote signature from {} for ({},{}) is valid, Process "
          "{} tried to equivocate!",
          r_id, o_id, idx, o_id);
    }
  }
}

}  // namespace dory
