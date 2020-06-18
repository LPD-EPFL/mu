#include <future>
#include <limits>

#include <dory/conn/exchanger.hpp>
#include <dory/conn/wr-builder.hpp>
#include <dory/shared/bench.hpp>
#include <dory/shared/branching.hpp>
#include <dory/shared/pinning.hpp>
#include <dory/shared/unused-suppressor.hpp>
#include <dory/store.hpp>

#include "../shared/consts.hpp"
#include "../shared/log-strings.hpp"
#include "broadcast.hpp"

#define BENCH(expr, str)                                                   \
  {                                                                        \
    struct timespec ts1, ts2;                                              \
    clock_gettime(CLOCK_MONOTONIC, &ts1);                                  \
    expr;                                                                  \
    clock_gettime(CLOCK_MONOTONIC, &ts2);                                  \
    SPDLOG_LOGGER_CRITICAL(logger, "benching {} took: {}ns", str,          \
                           (ts2.tv_sec * 1000000000UL + ts2.tv_nsec) -     \
                               (ts1.tv_sec * 1000000000UL + ts1.tv_nsec)); \
  }

inline uint64_t unpack_msg_id(uint64_t wr_id) { return (wr_id << 32) >> 32; }

inline std::tuple<bool, int, int, uint64_t> unpack_read_id(uint64_t wr_id) {
  return {wr_id >> 63, (wr_id << 12) >> 54, (wr_id << 22) >> 54,
          unpack_msg_id(wr_id)};
}

inline uint64_t pack_read_id(bool had_sig, int replayer, int origin,
                             uint64_t msg_id) {
  return uint64_t(had_sig) << 63 | uint64_t(replayer) << 42 |
         uint64_t(origin) << 32 | msg_id;
}

inline std::tuple<bool, int, uint64_t> unpack_write_id(uint64_t wr_id) {
  return {wr_id >> 63, (wr_id << 22) >> 54, unpack_msg_id(wr_id)};
}
inline uint64_t pack_write_id(bool is_sig, int receiver, uint64_t msg_id) {
  return uint64_t(is_sig) << 63 | (uint64_t(receiver) << 32) | msg_id;
}

namespace dory {
namespace neb {
namespace sync {

NonEquivocatingBroadcast::~NonEquivocatingBroadcast() {
  stop_operation();
  write(ttd);
}

NonEquivocatingBroadcast::NonEquivocatingBroadcast(int self_id,
                                                   std::vector<int> proc_ids,
                                                   dory::ControlBlock &cb,
                                                   deliver_callback deliver_cb)
    : self_id(self_id),
      remotes(proc_ids, self_id),
      deliver_cb(deliver_cb),
      cb(cb),
      logger(std_out_logger("NEB")),
      own_next(1),
      pending_reads(0),
      pending_writes(0),
      sign_pool(SIGN_POOL_SIZE, "sign_pool", {16, 18 /*, 20*/}),
      verify_pool(VERIFY_POOL_SIZE, "verify_pool",
                  {6, 8, 10, 12, /*14 , 16, 17, 18, 19, 26*/}),
      post_reader(1, "read_pool", {14}),
      replayed(RemotePendingSlots(proc_ids)),
      pending_remote_slots(0) {
  logger->set_level(LOG_LEVEL);
  SPDLOG_LOGGER_INFO(logger, "Creating instance");

  write_wcs.resize(dory::ControlBlock::CQDepth);
  read_wcs.resize(dory::ControlBlock::CQDepth);

  ttd.resize(100000);

  auto remote_ids = remotes.ids();

  for (auto &id : *remote_ids) {
    next_msg_idx.insert(std::pair<int, uint64_t>(id, 1));
    next_sig.insert(std::pair<int, uint64_t>(id, 1));
    pending_slots_at[id];
    pending_reads_at[id];
    pending_writes_at[id];
  }

  auto r_mr_w = cb.mr(REPLAY_W_NAME);
  replay_buf =
      std::make_unique<ReplayBuffer>(r_mr_w.addr, r_mr_w.size, proc_ids);

  auto r_procs_size = (*remote_ids).size();

  // the maximum of concurrent pending slots restricts the size of the read
  // buffer
  cb.allocateBuffer(REPLAY_R_NAME,
                    MEMORY_SLOT_SIZE * r_procs_size *
                        (MAX_CONCURRENTLY_PENDING_SLOTS +
                         // to cover byzantine broadcasters
                         MAX_CONCURRENTLY_PENDING_PER_PROCESS),
                    64);
  // we need to register the read buffer in order to obtain a lkey which we use
  // for RDMA reads where the RNIC does local writes.
  cb.registerMR(REPLAY_R_NAME, PD_NAME, REPLAY_R_NAME,
                dory::ControlBlock::LOCAL_WRITE);

  auto r_mr_r = cb.mr(REPLAY_R_NAME);
  replay_r_buf = std::make_unique<MemSlotPool>(*remote_ids, r_mr_r.addr,
                                               r_mr_r.size, r_mr_r.lkey);

  // Insert a buffer which will be used as scatter when post sending writes
  auto b_mr = cb.mr(BCAST_W_NAME);
  bcast_bufs.insert(std::pair<int, BroadcastBuffer>(
      self_id, BroadcastBuffer(b_mr.addr, b_mr.size, b_mr.lkey)));
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

  {
    auto bcast_conn = remotes.broadcast_connections();
    for (auto &[pid, rc] : *bcast_conn) {
      auto &mr = rc.get_mr();
      bcast_bufs.insert(std::pair<int, BroadcastBuffer>(
          pid, BroadcastBuffer(mr.addr, mr.size, mr.lkey)));
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
    SPDLOG_LOGGER_INFO(logger, BCAST_THREAD_FINISH);

    if (bcast_signature_poller_running) bcast_signature_poller.join();
    SPDLOG_LOGGER_INFO(logger, "Bcast signature poller thread finished");

    if (r_cq_poller_running) r_cq_poller_thread.join();
    SPDLOG_LOGGER_INFO(logger, "R CQ poller thread finished");

    if (w_cq_poller_running) w_cq_poller_thread.join();
    SPDLOG_LOGGER_INFO(logger, "W CQ poller thread finished");
  }
}

void NonEquivocatingBroadcast::end() {
  SPDLOG_LOGGER_WARN(logger, "End called");
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
  pinThreadToCore(bcast_content_poller, 0);
  set_thread_name(bcast_content_poller, "bcast_poller");

  // TODO(Kristian): do we need this in a separate thread?
  bcast_signature_poller =
      std::thread([=] { start_bcast_signature_poller(sf); });
  pinThreadToCore(bcast_signature_poller, 20);
  set_thread_name(bcast_signature_poller, "sig_poller");

  r_cq_poller_thread = std::thread([=] { start_r_cq_poller(sf); });
  pinThreadToCore(r_cq_poller_thread, 2);
  set_thread_name(r_cq_poller_thread, "r_cq_poller");

  // TODO(Kristian): do we need this in a separate thread?
  w_cq_poller_thread = std::thread([=] { start_w_cq_poller(sf); });
  pinThreadToCore(w_cq_poller_thread, 22);
  set_thread_name(w_cq_poller_thread, "w_cq_poller");

  started = true;
  SPDLOG_LOGGER_INFO(logger, "Started");
}

void NonEquivocatingBroadcast::start_bcast_content_poller(
    std::shared_future<void> f) {
  if (bcast_content_poller_running) return;

  SPDLOG_LOGGER_INFO(logger, "Replayer thread running");

  bcast_content_poller_running = true;
  size_t next_proc = 0;

  while (unlikely(f.wait_for(std::chrono::seconds(0)) ==
                  std::future_status::timeout)) {
    next_proc = poll_bcast_bufs(next_proc);
  }
}

void NonEquivocatingBroadcast::start_bcast_signature_poller(
    std::shared_future<void> f) {
  if (bcast_signature_poller_running) return;

  SPDLOG_LOGGER_INFO(logger, "Replayer thread running");

  bcast_signature_poller_running = true;

  while (likely(f.wait_for(std::chrono::seconds(0)) ==
                std::future_status::timeout)) {
    poll_bcast_signatures();
  }
}

void NonEquivocatingBroadcast::start_r_cq_poller(std::shared_future<void> f) {
  if (r_cq_poller_running) return;

  SPDLOG_LOGGER_INFO(logger, "Read CQ poller thread running");

  r_cq_poller_running = true;

  auto &rcq = cb.cq(REPLAY_CQ_NAME);

  while (likely(f.wait_for(std::chrono::seconds(0)) ==
                std::future_status::timeout)) {
    consume_read_wcs(rcq);
  }
}

void NonEquivocatingBroadcast::start_w_cq_poller(std::shared_future<void> f) {
  if (w_cq_poller_running) return;

  SPDLOG_LOGGER_INFO(logger, "WRITE CQ poller thread running");

  w_cq_poller_running = true;

  auto &wcq = cb.cq(BCAST_CQ_NAME);

  while (likely(f.wait_for(std::chrono::seconds(0)) ==
                std::future_status::timeout)) {
    consume_write_wcs(wcq);
  }
}

/* -------------------------------------------------------------------------- */

void NonEquivocatingBroadcast::broadcast(uint64_t k, Broadcastable &msg) {
  // BenchTimer timer("broadcast", true);
  if (unlikely(msg.size() > MSG_PAYLOAD_SIZE)) {
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

    dory::crypto::dalek::sign(sig, sign_data, SLOT_SIGN_DATA_SIZE);

    const_cast<MemorySlot *>(&slot)->set_signature_canary();

    // if (self_id == 1 && sig_next > 100) return;

    post_write_all(true, sig_next, reinterpret_cast<uintptr_t>(sig),
                   static_cast<uint32_t>(SIGNATURE_POST_WRITE_LEN),
                   bcast_buf.lkey,
                   bcast_buf.get_byte_offset(sig_next) + SLOT_SIGNATURE_OFFSET);
  });

  post_write_all(false, own_next, slot.addr(), SLOT_SIGN_DATA_SIZE,
                 bcast_buf.lkey, bcast_buf.get_byte_offset(own_next));

  // increase the message counter
  own_next++;

  // deliver to ourself
  deliver(slot, self_id);
}

inline void NonEquivocatingBroadcast::poll_bcast_signatures() {
  auto remote_ids = remotes.ids();

  for (auto origin : *remote_ids) {
    auto next = next_sig[origin];
    // ensure we only verify a signature if also the content is already replayed
    if (next >= next_msg_idx[origin]) continue;

    auto bcast_slot = bcast_bufs.find(origin)->second.slot(next);

    if (likely(bcast_slot.has_signature())) {
      auto replay_slot = replay_buf->slot(origin, next);

      // We only copy the signature if there is none present in the replay
      // buffer, as otherwise bad things can happen!
      if (!replay_slot.has_signature())
        bcast_slot.copy_signature_to(replay_slot);

      // The corresponding slot is already delivered. This may have happend
      // if all remotes had matching (and not empty) replayed slots.
      if (!replayed.get(origin).exist(next)) {
        next_sig[origin]++;
        continue;
      }

      // verify the signature by a worker thread who additionally tries to
      // deliver in case we were only waiting for validating the signature
      verify_pool.enqueue([=]() {
        auto r_slot = replay_buf->slot(origin, next);
        auto is_valid = verify_slot(r_slot, remotes.key(origin));
        SPDLOG_LOGGER_DEBUG(logger, "Verified sig pid,idx={},{}. Is valid {}",
                            origin, next, is_valid);

        if (auto tracker = replayed.get(origin).get(next)) {
          if (likely(is_valid)) {
            // the order here is important as other threads check first
            // for a processes sig and then for the outcome. If we swap
            // the following two line it may appear for a short period that
            // the siganture was invalid
            tracker->get().sig_valid = is_valid;
            tracker->get().processed_sig = true;
            try_deliver(r_slot, origin, next);
          } else {
            SPDLOG_LOGGER_CRITICAL(logger, "SIG INVALID FOR ({},{})", origin,
                                   next);
            tracker->get().processed_sig = true;
            // ensure we don't consider the origin anymore
            pending_slots_at[origin] = std::numeric_limits<int>::max();
            pending_remote_slots--;
          }
        }
      });

      next_sig[origin]++;
    }
  }
}

inline size_t NonEquivocatingBroadcast::poll_bcast_bufs(size_t next_proc) {
  // TODO(Kristian): spin-lock?
  if (pending_remote_slots >= MAX_CONCURRENTLY_PENDING_SLOTS) {
    SPDLOG_LOGGER_TRACE(logger,
                        "Maximum of concurrently pending slots reached. "
                        "Waiting on broadcast poll cond.");

    std::unique_lock<std::mutex> lock(bcast_poll_mux);
    bcast_poll_cond.wait(lock, [&]() {
      return pending_remote_slots < MAX_CONCURRENTLY_PENDING_SLOTS;
    });
  }

  auto remote_ids = remotes.ids();

  for (size_t i = next_proc; i < remote_ids.get().size(); i++) {
    auto origin = remote_ids.get()[i];
    auto &next_idx = next_msg_idx[origin];

    auto bcast_slot = bcast_bufs.find(origin)->second.slot(next_idx);

    if (bcast_slot.id() != next_idx ||
        pending_slots_at[origin] >= MAX_CONCURRENTLY_PENDING_PER_PROCESS)
      continue;

    SPDLOG_LOGGER_DEBUG(
        logger, "Bcast from {} at index {} = ({},{}) is signed {}", origin,
        next_idx, bcast_slot.id(),
        *reinterpret_cast<const volatile uint64_t *>(bcast_slot.content()),
        bcast_slot.has_signature());

    // start latency benchmark
    ttd[static_cast<size_t>(origin) * next_idx].first =
        std::chrono::steady_clock::now();

    auto replay_slot_w = replay_buf->slot(origin, next_idx);

    bcast_slot.copy_to(replay_slot_w);

    // NOTE: we create here a tracker for the current processing message. This
    // needs to be here, so that the other threads can deduce the delivery
    // of the message when the tracker is not present anymore.
    replayed.get(origin).insert(next_idx);
    pending_remote_slots++;
    pending_slots_at[origin]++;
    // if there is only one remote process which is the message sender,
    // then we can directly deliver without replay reading
    if (unlikely(remotes.replay_quorum_size() == 0)) {
      if (replayed.get(origin).remove(next_idx)) {
        deliver(replay_slot_w, origin);
      }

      next_msg_idx[origin]++;
      continue;
    }

    post_reads_for(origin, next_idx);

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

  if (unlikely(!cb.pollCqIsOK(cq, write_wcs))) {
    SPDLOG_LOGGER_WARN(logger, "Polling on WRITE CQ failed!");
    return;
  }

  if (write_wcs.size() > 0) {
    SPDLOG_LOGGER_DEBUG(logger, "WRITE CQ polled, size: {}", write_wcs.size());
    {
      std::unique_lock<std::mutex> lock(write_mux);
      pending_writes -= static_cast<unsigned>(write_wcs.size());
      write_cond.notify_one();
    }

    for (auto &wc : write_wcs) {
      auto const &[is_sig, receiver, msg_idx] = unpack_write_id(wc.wr_id);
      dory::IGNORE(msg_idx);
      dory::IGNORE(is_sig);

      switch (wc.status) {
        case IBV_WC_SUCCESS:
          pending_writes_at[receiver]--;
          break;
        case IBV_WC_RETRY_EXC_ERR:
          pending_writes_at[receiver]--;
          SPDLOG_LOGGER_INFO(logger,
                             "WC WRITE: Process {} not responding, removing",
                             receiver);
          remove_remote(receiver);

          break;
        default:
          pending_writes_at[receiver]--;
          SPDLOG_LOGGER_INFO(logger, "WC for WRITE at {} has status {}",
                             receiver, wc.status);
      }
    }
  }
}

inline void NonEquivocatingBroadcast::consume_read_wcs(
    dory::deleted_unique_ptr<ibv_cq> &cq) {
  read_wcs.resize(pending_reads);

  if (unlikely(!cb.pollCqIsOK(cq, read_wcs))) {
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

    for (auto const &wc : read_wcs) {
      auto const &[had_sig, r_id, o_id, idx] = unpack_read_id(wc.wr_id);
      switch (wc.status) {
        case IBV_WC_SUCCESS:
          pending_reads_at[r_id]--;
          handle_replay_read(had_sig, r_id, o_id, idx);
          break;
        case IBV_WC_RETRY_EXC_ERR:
          pending_reads_at[r_id]--;
          replay_r_buf->free(r_id, o_id, idx);
          SPDLOG_LOGGER_INFO(
              logger, "WC read; Process {} not responding, removing!", r_id);
          remove_remote(r_id);
          break;
        default: {
          pending_reads_at[r_id]--;
          replay_r_buf->free(r_id, o_id, idx);
          SPDLOG_LOGGER_WARN(logger,
                             "WC for READ at {} for ({},{}) has status {}",
                             r_id, o_id, idx, wc.status);
        }
      }
    }
  }
}

inline void NonEquivocatingBroadcast::handle_replay_read(bool had_sig, int r_id,
                                                         int o_id,
                                                         uint64_t idx) {
  SPDLOG_LOGGER_DEBUG(logger,
                      "WC for READ at {} for ({},{}). Local sig exists: {}",
                      r_id, o_id, idx, had_sig);

  auto &p = replayed.get(o_id);

  auto r = p.get(idx);

  if (unlikely(!r)) {
    replay_r_buf->free(r_id, o_id, idx);
    return;
  }

  auto &tracker = r->get();

  auto rr_slot = replay_r_buf->slot(o_id, r_id, idx);
  auto own_slot = replay_buf->slot(o_id, idx);

  // got no response, this may happen when the remote is not ready yet
  // and didn't initialize its memory buffer
  if (unlikely(rr_slot.id() == 0)) {
    SPDLOG_LOGGER_WARN(
        logger, "Slot ({},{} at {}) was not successfully read, val: {}", o_id,
        idx, r_id, *reinterpret_cast<uint64_t *>(rr_slot.addr()));

    post_read(r_id, pack_read_id(own_slot.has_signature(), r_id, o_id, idx),
              rr_slot.addr(), MEMORY_SLOT_SIZE, replay_r_buf->lkey(),
              replay_buf->get_byte_offset(o_id, idx));
    // this case means the remote has nothing replayed yet for thie
    // (origin,index) pair
  } else if (rr_slot.id() == std::numeric_limits<uint64_t>::max()) {
    // we check here if the read was triggered at a time when the signature
    // was locally replayed or not. This is important as it ensures that remote
    // processes will see it and we can use this information when considering
    // various scenarios. For example can we add an empty read to the empty-read
    // set which might count towards the final quorum when the signature was
    // already replayed. Otherwise it is not safe to add the remote process to
    // the empty-read set as he might receive a valid signature shortly after
    // our read and given that we don't have a signature replayed he might then
    // deliver that message. If we afterwards receive a valid signature we would
    // also deliver, which violates safety.
    if (had_sig) {
      // if the local signature is not processed yet, we might get lucky
      // when re-posting the read and potentially upgrading to a match-read
      // in the meanwhile, which could yield a delivery without needing a valid
      // signature at all - in case all remotes have replayed the same content.
      if (!tracker.processed_sig) {
        SPDLOG_LOGGER_TRACE(
            logger,
            "nothing replayed - {} for ({},{}). Local signature "
            "existed, but still not validated. Re-posting!",
            r_id, o_id, idx);
        // TODO(Kristian): enabling the following line messes up things - why
        // does the mem pool get exhausted?
        post_read(r_id, pack_read_id(had_sig, r_id, o_id, idx), rr_slot.addr(),
                  MEMORY_SLOT_SIZE, replay_r_buf->lkey(),
                  replay_buf->get_byte_offset(o_id, idx));
      } else {
        SPDLOG_LOGGER_TRACE(
            logger,
            "nothing replayed - {} for ({},{}). Local signature "
            "existed and is validated. Adding to empty reads",
            r_id, o_id, idx);
        tracker.add_to_empty_reads(r_id);
        if (tracker.has_quorum_of(remotes.replay_quorum_size()))
          try_deliver(own_slot, o_id, idx);
        replay_r_buf->free(r_id, o_id, idx);
      }
      // TODO(Kristian): limit the number of re-reads
      // we re-post the read as long as we don't have a local signature replayed
      // and exposed
    } else {
      SPDLOG_LOGGER_TRACE(logger,
                          "nothing replayed - {} for ({},{}). Re-reading as "
                          "local signature is missing!",
                          r_id, o_id, idx);
      post_read(r_id, pack_read_id(own_slot.has_signature(), r_id, o_id, idx),
                rr_slot.addr(), MEMORY_SLOT_SIZE, replay_r_buf->lkey(),
                replay_buf->get_byte_offset(o_id, idx));
    }
    // got a response and the replay slot was written, so we check for
    // matching contents, otherwise we need to check signatures
  } else if (rr_slot.has_same_data_content_as(own_slot)) {
    SPDLOG_LOGGER_TRACE(logger, "match read - {} for ({},{}) ", r_id, o_id,
                        idx);
    tracker.add_to_match_reads(r_id);
    if (tracker.has_quorum_of(remotes.replay_quorum_size()))
      try_deliver(own_slot, o_id, idx);
    replay_r_buf->free(r_id, o_id, idx);
  } else {
    SPDLOG_LOGGER_CRITICAL(logger, "CONFLICTING SLOTS r,o,i=({},{},{})", r_id,
                           o_id, idx);
    // Only now we'll verify the signatures, as the operation is more costly
    // than comparing the slot contents. Anyhow, we want to verify the
    // signature in order to know if the origin broadcaster tried to
    // equivocate or we can count the current processing replayer towards
    // the quorum.
    if (tracker.processed_sig) {
      SPDLOG_LOGGER_TRACE(logger, "Local signature for ({},{},{}) is processed",
                          o_id, r_id, idx);
      if (!tracker.sig_valid) {
        SPDLOG_LOGGER_TRACE(logger, "Local signature for ({},{},{}) invalid",
                            o_id, r_id, idx);
        replay_r_buf->free(r_id, o_id, idx);
        // Note: the validating worker thread does decrement the
        // pending_remote_slots as well as sets the maximum value for the
        // pending_slots_at entry for this origin s.t. we don't consider it
        // anymore as it has become byzantine by not providing a valid
        // signature
        return;
      }

      SPDLOG_LOGGER_TRACE(logger, "Local signature for ({},{},{}) is valid",
                          o_id, r_id, idx);
      if (!rr_slot.has_signature()) {
        SPDLOG_LOGGER_TRACE(logger,
                            "Remote signature for ({},{},{}) not present", o_id,
                            r_id, idx);
        // check if the signature was already replayed at the time of posting
        // the read corresponding to this WC. If not, we need to re-post the
        // read in order to not violate safety.
        if (!had_sig) {
          SPDLOG_LOGGER_TRACE(logger,
                              "Will retry reading ({},{},{}) as sig was "
                              "missing when posting read",
                              o_id, r_id, idx);
          // we need to re-post a read once more as the remote might have
          // also read an slot without a signature in our buffer in which
          // case we could violate agreement when adding the replayer
          // immediatelly to the quorum. This way we ensure that at least
          // one process will see the signature of the other and in case
          // of an attempt to equivocate by the broadcaster one process
          // will not add the other to the quorum and thus never deliver
          // the conflicting message.
          // tracker.add_to_conflicts(r_id);
          post_read(r_id,
                    pack_read_id(own_slot.has_signature(), r_id, o_id, idx),
                    rr_slot.addr(), MEMORY_SLOT_SIZE, replay_r_buf->lkey(),
                    replay_buf->get_byte_offset(o_id, idx));
        } else {
          SPDLOG_LOGGER_TRACE(
              logger,
              "No remote signature for ({},{},{}) but valid local "
              "sig was present. Adding to empty-read quorum",
              o_id, r_id, idx);
          // We resend the request already once more to see if in the
          // meanwhile the signature was written at the remote process
          // r_id. If it still not present, then we can include this
          // replayer to the quorum as if he is correct, then he will
          // re-try also once and then see that we have a valid signature
          // so he won't ever deliver his version of the message at the
          // current index.
          tracker.add_to_empty_reads(r_id);
          if (tracker.has_quorum_of(remotes.replay_quorum_size()))
            try_deliver(own_slot, o_id, idx);
          replay_r_buf->free(r_id, o_id, idx);
        }
      } else {
        verify_pool.enqueue(
            [=]() { verify_and_act_on_remote_sig(o_id, r_id, idx); });
      }
    } else {
      SPDLOG_LOGGER_TRACE(logger, "No local processed signature for ({},{},{})",
                          o_id, r_id, idx);
      if (!rr_slot.has_signature()) {
        SPDLOG_LOGGER_TRACE(
            logger, "Also no remote signature for ({},{},{}). Re-posting read!",
            o_id, r_id, idx);
        // At this point we don't have any signature but conflicting slots.
        // We need at least one signature in order to be able to know if the
        // broadcaster or replayer is byzantine. Therefore, we re-post a
        // remote read.
        post_read(r_id, pack_read_id(own_slot.has_signature(), r_id, o_id, idx),
                  rr_slot.addr(), MEMORY_SLOT_SIZE, replay_r_buf->lkey(),
                  replay_buf->get_byte_offset(o_id, idx));
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

inline void NonEquivocatingBroadcast::remove_remote(int pid) {
  remotes.remove_remote(pid);

  // TODO(Kristian): determine when do we need this
  // Not always are WCs consumed upon an error
  //
  // When still in send queue (err 5) then yes and for already send ones only
  // one that also transitions the QP in err state?
  //
  // auto &p_reads = pending_reads_at[pid];
  // if (p_reads > 0) {
  //   {
  //     std::unique_lock<std::mutex> lock(read_mux);
  //     if (pending_reads_at[pid] > pending_reads) {
  //       throw std::runtime_error("Would overflow pending_reads by reducing");
  //     }
  //     pending_reads -= pending_reads_at[pid];
  //     read_cond.notify_one();
  //   }
  // }

  // auto &p_writes = pending_writes_at[pid];
  // if (p_writes > 0) {
  //   {
  //     std::unique_lock<std::mutex> lock(write_mux);
  //     if (pending_writes_at[pid] > pending_writes) {
  //       throw std::runtime_error("Would overflow pending_writes by
  //       reducing");
  //     }
  //     pending_writes -= static_cast<unsigned>(pending_writes_at[pid]);
  //     write_cond.notify_one();
  //   }
  // }

  for (auto &[origin, tracker_map] : replayed) {
    if (origin == pid) continue;

    auto quorum_size = remotes.replay_quorum_size();
    auto potential_ids =
        tracker_map.deliverable_after_remove_of(pid, quorum_size);

    for (auto id : potential_ids) {
      if (tracker_map.remove(id)) {
        auto slot = bcast_bufs.find(origin)->second.slot(id);
        deliver(slot, origin);
      }
    }
  }
  SPDLOG_LOGGER_TRACE(logger, "Done removing!");
}

inline bool NonEquivocatingBroadcast::verify_slot(
    MemorySlot &slot, dory::crypto::dalek::pub_key &key) {
  auto sig = reinterpret_cast<unsigned char *>(
      const_cast<uint8_t *>(slot.signature()));
  auto msg = reinterpret_cast<unsigned char *>(slot.addr());

  return dory::crypto::dalek::verify(sig, msg, SLOT_SIGN_DATA_SIZE, key);
}

// inline void NonEquivocatingBroadcast::post_write(int pid, uint64_t wrid,
//                                                  uintptr_t lbuf, uint32_t
//                                                  lsize, uint32_t lkey, size_t
//                                                  roffset) {
//   post_writer.enqueue([=]() {
//     if (pending_writes + 1 > dory::ControlBlock::CQDepth) {
//       SPDLOG_LOGGER_TRACE(logger, "waiting for write cond");

//       std::unique_lock<std::mutex> lock(write_mux);
//       write_cond.wait(lock, [&] {
//         return pending_writes + 1 <= dory::ControlBlock::CQDepth;
//       });

//       SPDLOG_LOGGER_TRACE(logger, "write cond satisfied, now {}",
//                           pending_writes);
//     }

//     auto rc = remotes.broadcast_connection(pid);
//     if (likely(rc)) {
//       auto ret = rc->get().postSendSingle(
//           ReliableConnection::RdmaWrite, wrid, reinterpret_cast<void
//           *>(lbuf), lsize, lkey, rc->get().remoteBuf() + roffset);

//       handle_write_post_ret(ret, pid, wrid);
//     }
//   });
// }

inline void NonEquivocatingBroadcast::post_write_all(bool is_sig, uint64_t idx,
                                                     uintptr_t addr,
                                                     uint32_t len, uint32_t key,
                                                     size_t roffset) {
  if (unlikely(pending_writes + remotes.size() > dory::ControlBlock::CQDepth)) {
    SPDLOG_LOGGER_TRACE(logger, "waiting for write cond");

    std::unique_lock<std::mutex> lock(write_mux);
    write_cond.wait(lock, [&] {
      return pending_writes + remotes.size() <= dory::ControlBlock::CQDepth;
    });

    SPDLOG_LOGGER_TRACE(logger, "write cond satisfied, now {}", pending_writes);
  }

  auto rcs = remotes.broadcast_connections();
  for (auto &[remote, rc] : rcs.get()) {
    auto wrid = pack_write_id(is_sig, remote, idx);
    // We're casting away the const qualifier which is meant only for
    // the map container, not it's elements. A bit hacky, but should work for
    // now. Eventually, refactor remotes.replay_connections().
    auto *r = const_cast<ReliableConnection *>(&rc);

    // if (self_id == 1 && idx > 100 && remote == 4) {
    //   reinterpret_cast<uint64_t *>(addr)[0] = 1337;
    // }

    auto ret = r->postSendSingle(ReliableConnection::RdmaWrite, wrid,
                                 reinterpret_cast<void *>(addr), len, key,
                                 roffset + r->remoteBuf());

    handle_write_post_ret(ret, remote, wrid);
  }
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

      handle_read_post_ret(ret, pid, wrid);
    }
  });
}

inline void NonEquivocatingBroadcast::post_reads_for(int origin,
                                                     uint64_t slot_index) {
  if (unlikely(pending_reads + remotes.size() - 1 >
               dory::ControlBlock::CQDepth)) {
    SPDLOG_LOGGER_TRACE(logger, "waiting for read cond");

    std::unique_lock<std::mutex> lock(read_mux);
    read_cond.wait(lock, [&] {
      return pending_reads + remotes.size() - 1 <= dory::ControlBlock::CQDepth;
    });

    SPDLOG_LOGGER_TRACE(logger, "read cond satisfied, now {}", pending_reads);
  }

  auto lkey = replay_r_buf->lkey();
  auto roffset = replay_buf->get_byte_offset(origin, slot_index);
  auto rcs = remotes.replay_connections();
  for (auto &[replayer, rc] : rcs.get()) {
    // we don't care what the broadcaster has replayed for itself
    if (replayer == origin) continue;

    auto wrid =
        pack_read_id(replay_buf->slot(origin, slot_index).has_signature(),
                     replayer, origin, slot_index);
    auto replay_r_slot = replay_r_buf->slot(origin, replayer, slot_index);
    // We're casting away the const qualifier which is meant only for
    // the map container, not it's elements. A bit hacky, but should work for
    // now. Eventually, refactor remotes.replay_connections().
    auto *r = const_cast<ReliableConnection *>(&rc);

    auto ret =
        r->postSendSingle(ReliableConnection::RdmaRead, wrid,
                          reinterpret_cast<void *>(replay_r_slot.addr()),
                          MEMORY_SLOT_SIZE, lkey, roffset + r->remoteBuf());

    handle_read_post_ret(ret, replayer, wrid);
  }
}

inline void NonEquivocatingBroadcast::handle_write_post_ret(int ret, int remote,
                                                            uint64_t wrid) {
  if (unlikely(!ret)) {
    SPDLOG_LOGGER_WARN(
        logger,
        "POST WRITE at {} with id {} returned {}, current pending writes: {}",
        remote, wrid, pending_writes);

  } else {
    pending_writes++;
    pending_writes_at[remote]++;
    // SPDLOG_LOGGER_DEBUG(logger, "Pending writes: {}, at {}: {}",
    // pending_writes,
    //                     remote, pending_writes_at[remote]);
  }
}

inline void NonEquivocatingBroadcast::handle_read_post_ret(int ret, int remote,
                                                           uint64_t wrid) {
  if (unlikely(!ret)) {
    SPDLOG_LOGGER_WARN(logger,
                       "POST READ at {} for id {} returned {}, current "
                       "pending reads: {}",
                       remote, wrid, ret, pending_reads);

  } else {
    pending_reads++;
    pending_reads_at[remote]++;
    // SPDLOG_LOGGER_DEBUG(logger, "Pending reads: {}, at {}: {}",
    // pending_reads,
    //                     remote, pending_reads_at[remote]);
  }
}

inline void NonEquivocatingBroadcast::deliver(MemorySlot &slot, int origin) {
  if (origin != self_id) {
    pending_slots_at[origin]--;

    {
      std::unique_lock<std::mutex> lock(bcast_poll_mux);
      pending_remote_slots--;
      bcast_poll_cond.notify_one();
    }

    SPDLOG_LOGGER_DEBUG(logger, "Pending slots: {}", pending_remote_slots);
  }
  // synchronize access to the layer above
  std::unique_lock<std::mutex> lock(deliver_mux);

  if (origin != self_id)
    ttd[static_cast<size_t>(origin) * slot.id()].second =
        std::chrono::steady_clock::now();

  deliver_cb(slot.id(), slot.content(), origin);
}

inline void NonEquivocatingBroadcast::try_deliver(MemorySlot &slot, int origin,
                                                  uint64_t idx) {
  if (replayed.get(origin).remove_if_complete(idx,
                                              remotes.replay_quorum_size())) {
    deliver(slot, origin);
  }
}

inline void NonEquivocatingBroadcast::verify_and_act_on_remote_sig(
    int o_id, int r_id, uint64_t idx) {
  if (auto tracker = replayed.get(o_id).get(idx)) {
    auto rr_slot = replay_r_buf->slot(o_id, r_id, idx);
    bool replay_sig_valid = verify_slot(rr_slot, remotes.key(o_id));

    replay_r_buf->free(r_id, o_id, idx);

    if (!replay_sig_valid) {
      SPDLOG_LOGGER_TRACE(logger, "Remote signature for ({},{},{}) not valid",
                          o_id, r_id, idx);
      tracker->get().add_to_empty_reads(r_id);
      auto slot = bcast_bufs.find(o_id)->second.slot(idx);
      try_deliver(slot, o_id, idx);
    } else {
      SPDLOG_LOGGER_TRACE(
          logger,
          "Remote signature from {} for ({},{}) is valid, Process "
          "{} tried to equivocate!",
          r_id, o_id, idx, o_id);
      // with this we ensure we won't replay any values by this remote anymore
      // also we decrease the concurrency value as this slot won't ever be
      // delivered
      pending_slots_at[o_id] = std::numeric_limits<int>::max();
      pending_remote_slots--;
    }
  }
}

}  // namespace sync
}  // namespace neb
}  // namespace dory