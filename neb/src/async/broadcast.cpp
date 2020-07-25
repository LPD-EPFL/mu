#include <future>
#include <limits>

#include <dory/conn/exchanger.hpp>
#include <dory/conn/wr-builder.hpp>
#include <dory/shared/bench.hpp>
#include <dory/shared/unused-suppressor.hpp>
#include <dory/store.hpp>

#include "../shared/helpers.hpp"

#include "broadcast.hpp"
#include "consts.hpp"

inline void print_slot(bool is_local, MemorySlot &slot) {
  std::cout << "For " << is_local << std::endl;

  auto buf = reinterpret_cast<uint8_t *>(slot.addr());
  for (size_t i = 0; i < dory::neb::MEMORY_SLOT_SIZE; i++) {
    std::cout << std::hex << static_cast<int>(buf[i]) << " ";
  }
  std::cout << std::dec << std::endl;
}

inline bool same_slot(MemorySlot &lhs, MemorySlot &rhs) {
  auto lb = reinterpret_cast<uint8_t *>(lhs.addr());
  auto rb = reinterpret_cast<uint8_t *>(rhs.addr());
  for (size_t i = 0; i < dory::neb::MEMORY_SLOT_SIZE; i++) {
    auto l = lb[i];
    auto r = rb[i];
    if (l != r) {
      std::cout << "Conflict at " << i << " " << static_cast<int>(l) << " "
                << static_cast<int>(r) << std::endl;
      print_slot(true, lhs);
      print_slot(false, rhs);
      std::cout << "------------" << std::endl;
      return false;
    }
  }
  return true;
}

// NOTE: when changing the WR structure make sure to also adjust the WC error
// handling

// <had_sig(1), count(10), replayer(7), replicator(7), origin(7), idx(32)>
inline std::tuple<bool, int, int, int, int, uint64_t> unpack_read_id(
    uint64_t wr_id) {
  return {wr_id >> 63,         (wr_id << 1) >> 54,  (wr_id << 11) >> 57,
          (wr_id << 18) >> 57, (wr_id << 25) >> 57, (wr_id << 32) >> 32};
}

inline uint64_t pack_read_id(bool had_sig, int count, int writer,
                             int replicator, int origin, uint64_t idx) {
  return uint64_t(had_sig) << 63 | uint64_t(count) << 53 |
         uint64_t(writer) << 46 | uint64_t(replicator) << 39 |
         uint64_t(origin) << 32 | idx;
}

// <is_sig(1), has_sig(1), origin(10), receiver(10), index(32)>
inline std::tuple<bool, bool, int, int, uint64_t> unpack_write_id(
    uint64_t wr_id) {
  return {wr_id >> 63, (wr_id << 1) >> 63, (wr_id << 12) >> 54,
          (wr_id << 22) >> 54, (wr_id << 32) >> 32};
}

inline uint64_t pack_write_id(bool is_sig, bool has_sig, int origin,
                              int receiver, uint64_t idx) {
  return uint64_t(is_sig) << 63 | uint64_t(has_sig) << 62 |
         uint64_t(origin) << 42 | uint64_t(receiver) << 32 | idx;
}

namespace dory {
namespace neb {
namespace async {

NonEquivocatingBroadcast::~NonEquivocatingBroadcast() {
  write(ttd);
  stop_operation();
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
      verify_pool(VERIFY_POOL_SIZE, "verify_pool", {8, 10}),
      sign_pool(SIGN_POOL_SIZE, "sig_pool", {14}),
      post_writer(1, "post_w", {12}),
      post_reader(1, "post_r", {16}),
      replayed(proc_ids),
      pending_remote_slots(0),
      sf(std::shared_future(exit_signal.get_future())) {
  if (proc_ids.size() < 3) {
    throw std::runtime_error("Async NEB needs at least 3 processes");
  }

  logger->set_level(LOG_LEVEL);
  SPDLOG_LOGGER_INFO(logger, "Creating instance");

  write_wcs.resize(dory::ControlBlock::CQDepth);
  read_wcs.resize(dory::ControlBlock::CQDepth);

  ttd.resize(proc_ids.size() * (max_msg_count + 1));

  for (size_t i = 0; i < process_ids.size(); i++) {
    process_pos[process_ids[i]] = i;
  }

  for (auto &id : remotes.ids()) {
    next_msg_idx.insert(std::pair<int, uint64_t>(id, 1));
    next_sig.insert(std::pair<int, uint64_t>(id, 1));
    pending_slots_at[id];
    pending_read_ids_at[id];
    // pending_reads_at[id];
    pending_write_ids_at[id];
    sig_write_acks[id];
  }

  // the maximum of concurrent pending slots and the CQ size restrict the size
  // of the read buffer
  cb.allocateBuffer(REPLAY_R_NAME,
                    MEMORY_SLOT_SIZE * (ControlBlock::CQDepth * 2 +
                                        MAX_CONCURRENTLY_PENDING_SLOTS *
                                            remotes.size() * remotes.size()),
                    64);
  // we need to register the read buffer in order to obtain a lkey which we use
  // for RDMA reads where the RNIC does local writes.
  cb.registerMR(REPLAY_R_NAME, PD_NAME, REPLAY_R_NAME,
                dory::ControlBlock::LOCAL_WRITE);

  auto r_mr_r = cb.mr(REPLAY_R_NAME);
  replay_r_buf = std::make_unique<MemSlotPool>(remotes.ids(), r_mr_r.addr,
                                               r_mr_r.size, r_mr_r.lkey);
}

void NonEquivocatingBroadcast::resize_ttd(std::map<int, int> &num_msgs) {
  size_t max = 0;
  for (auto &[pid, n] : num_msgs) {
    if (size_t(n) > max) {
      max = size_t(n);
    }
  }

  max_msg_count = max;

  auto size = (process_ids.size()) * (static_cast<size_t>(max_msg_count) + 1);
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
  auto &bcast_conn = remotes.broadcast_connections();
  for (size_t i = 0; i < plen; i++) {
    int pid = process_ids[i];

    if (pid == self_id) {
      auto mr = cb.mr(REPLAY_W_NAME);
      bcast_bufs.try_emplace(pid, mr.addr, mr.size, mr.lkey, process_ids);
    } else {
      auto &mr = bcast_conn.find(pid)->second.get_mr();
      bcast_bufs.try_emplace(pid, mr.addr, mr.size, mr.lkey, process_ids);
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

    if (bcast_signature_poller_running) bcast_signature_poller.join();
    SPDLOG_LOGGER_INFO(logger, "Bcast signautre poller finished");
  }
}

void NonEquivocatingBroadcast::end() {
  write(ttd);
  stop_operation();
}

void NonEquivocatingBroadcast::start() {
  if (started) return;

  if (!connected) {
    throw std::runtime_error(
        "Not connected to remote QPs, cannot start serving");
  }

  bcast_content_poller = std::thread([=] { start_bcast_content_poller(); });
  pinThreadToCore(bcast_content_poller, 0);
  set_thread_name(bcast_content_poller, "bcast_poller");

  bcast_signature_poller = std::thread([=] { start_bcast_signature_poller(); });
  pinThreadToCore(bcast_signature_poller, 2);
  set_thread_name(bcast_signature_poller, "sig_poller");

  r_cq_poller_thread = std::thread([=] { start_r_cq_poller(); });
  pinThreadToCore(r_cq_poller_thread, 4);
  set_thread_name(r_cq_poller_thread, "r_cq_poller");

  w_cq_poller_thread = std::thread([=] { start_w_cq_poller(); });
  pinThreadToCore(w_cq_poller_thread, 6);
  set_thread_name(w_cq_poller_thread, "w_cq_poller");

  started = true;
  SPDLOG_LOGGER_INFO(logger, "Started");
}

void NonEquivocatingBroadcast::start_bcast_content_poller() {
  if (bcast_content_poller_running) return;

  SPDLOG_LOGGER_INFO(logger, "Broadcast content poller thread running");

  bcast_content_poller_running = true;

  while (true) {
    poll_bcast_bufs();
  }
}

void NonEquivocatingBroadcast::start_bcast_signature_poller() {
  if (bcast_signature_poller_running) return;

  SPDLOG_LOGGER_INFO(logger, "Broadcast signature poller thread running");

  bcast_signature_poller_running = true;

  while (true) {
    poll_bcast_signatures();
  }
}

void NonEquivocatingBroadcast::start_r_cq_poller() {
  if (r_cq_poller_running) return;

  SPDLOG_LOGGER_INFO(logger, "Read CQ poller running");

  r_cq_poller_running = true;

  auto &rcq = cb.cq(R_CQ_NAME);

  std::random_device dev;
  std::mt19937 rng(dev());

  while (true) {
    consume_read_wcs(rcq, rng);
  }
}

void NonEquivocatingBroadcast::start_w_cq_poller() {
  if (w_cq_poller_running) return;

  SPDLOG_LOGGER_INFO(logger, "Write CQ poller running");

  w_cq_poller_running = true;

  auto &wcq = cb.cq(W_CQ_NAME);

  while (true) {
    consume_write_wcs(wcq);
  }
}

/* -------------------------------------------------------------------------- */

void NonEquivocatingBroadcast::broadcast(uint64_t k, Broadcastable &msg) {
  if (msg.size() > MSG_PAYLOAD_SIZE) {
    throw std::runtime_error("Allowed message size exceeded");
  }

  auto &bcast_buf = bcast_bufs.find(self_id)->second;
  {
    std::unique_lock lock(bcast_buf.get_mux(self_id, own_next));
    bcast_buf.write(self_id, own_next, k, msg);
  }

  auto slot = bcast_buf.slot(self_id, own_next);

  slot.set_content_canary();

  // own_next is a member variable, so we rather capture a local one
  auto sig_next = own_next;

  sign_pool.enqueue([=, &bcast_buf]() {
    // TODO(Kristian): we could GC
    std::unique_lock lock(bcast_buf.get_mux(self_id, sig_next));
    auto sig = reinterpret_cast<unsigned char *>(
        const_cast<uint8_t *>(slot.signature()));
    auto sign_data = reinterpret_cast<unsigned char *>(slot.addr());

    dory::crypto::dalek::sign(sig, sign_data, SLOT_SIGN_DATA_SIZE);

    const_cast<MemorySlot *>(&slot)->set_signature_canary();

    for (auto pid : remotes.ids()) {
      post_write(pid, pack_write_id(true, true, self_id, pid, sig_next),
                 slot.addr(), MEMORY_SLOT_SIZE, bcast_buf.lkey,
                 bcast_buf.get_byte_offset(self_id, sig_next));
    }
  });

  for (auto pid : remotes.ids()) {
    post_write(pid, pack_write_id(false, false, self_id, pid, own_next),
               slot.addr(), CONTENT_POST_WRITE_LEN, bcast_buf.lkey,
               bcast_buf.get_byte_offset(self_id, own_next));
  }

  own_next++;
}

inline void NonEquivocatingBroadcast::poll_bcast_bufs() {
  for (auto origin : remotes.ids()) {
    while (pending_remote_slots >= MAX_CONCURRENTLY_PENDING_SLOTS)
      ;

    if (pending_slots_at[origin] >= MAX_CONCURRENTLY_PENDING_PER_PROCESS)
      continue;

    auto next_idx = next_msg_idx[origin].load();
    auto bcast_slot = bcast_bufs.find(origin)->second.slot(origin, next_idx);

    if (bcast_slot.id() != next_idx) continue;

    SPDLOG_LOGGER_DEBUG(
        logger, "Bcast from {} at index {} = ({},{}) is signed {}", origin,
        next_idx, bcast_slot.id(),
        *reinterpret_cast<const volatile uint64_t *>(bcast_slot.content()),
        bcast_slot.has_signature());

    ttd[static_cast<size_t>(process_pos[origin]) * max_msg_count + next_idx]
        .first = std::chrono::steady_clock::now();

    auto &bcast_buf = bcast_bufs.find(self_id)->second;
    auto replay_slot = bcast_buf.slot(origin, next_idx);

    {
      std::unique_lock lock(bcast_buf.get_mux(origin, next_idx));
      bcast_slot.copy_to(replay_slot);
    }

    // NOTE: we create here a tracker for the current processing message. This
    // needs to be here, so that the other threads can deduce the delivery
    // of the message when the tracker is not present anymore.
    replayed.get(origin).insert(next_idx);

    pending_remote_slots++;
    pending_slots_at[origin]++;
    // make sure to increment this value only after copying the replay slot, so
    // that the signature poller thread does see if the siganture was already
    // replayed or not
    next_msg_idx[origin]++;

    auto has_sig = replay_slot.has_signature();
    for (auto pid : remotes.ids()) {
      post_write(pid, pack_write_id(false, has_sig, origin, pid, next_idx),
                 bcast_slot.addr(), dory::neb::MEMORY_SLOT_SIZE, bcast_buf.lkey,
                 bcast_buf.get_byte_offset(origin, next_idx));
    }

    // read_replay_registers_for(origin, next_idx);
    // read_replayed_slots_at_sources_for(origin, next_idx);
  }
}

inline void NonEquivocatingBroadcast::poll_bcast_signatures() {
  for (auto origin : remotes.ids()) {
    auto next = next_sig[origin].load();
    // ensure we only verify a signature if also the content is already replayed
    if (next >= next_msg_idx[origin]) continue;

    auto bcast_slot = bcast_bufs.find(origin)->second.slot(origin, next);

    if (bcast_slot.has_signature()) {
      auto &bcast_buf = bcast_bufs.find(self_id)->second;
      auto replay_slot = bcast_buf.slot(origin, next);

      // We only copy the signature if there is none present in the replay
      // buffer, as otherwise bad things can happen!
      if (!replay_slot.has_signature()) {
        SPDLOG_LOGGER_DEBUG(logger,
                            "Sig for {},{} was not present. Replicating.",
                            origin, next);
        {
          std::unique_lock lock(bcast_buf.get_mux(origin, next));
          bcast_slot.copy_signature_to(replay_slot);
        }

        auto offset = bcast_buf.get_byte_offset(origin, next);

        for (auto pid : remotes.ids()) {
          // TODO(Kristian): is it enough to only send the signature?
          post_write(pid, pack_write_id(true, true, origin, pid, next),
                     replay_slot.addr(),
                     static_cast<uint32_t>(MEMORY_SLOT_SIZE), bcast_buf.lkey,
                     offset);
        }
      }

      // The corresponding slot is already delivered. This may have happend
      // if all remotes had matching (and not empty) replayed slots.
      if (!replayed.get(origin).exist(next)) {
        next_sig[origin]++;
        continue;
      }

      // verify the signature by a worker thread who additionally tries to
      // deliver in case we were only waiting for validating the signature
      verify_pool.enqueue([=]() {
        auto &bcast_buf = bcast_bufs.find(self_id)->second;
        // TODO(Kristian): we could GC hereafter
        std::unique_lock lock(bcast_buf.get_mux(origin, next));

        auto r_slot = bcast_bufs.find(self_id)->second.slot(origin, next);
        auto is_valid = verify_slot(r_slot, remotes.key(origin));
        SPDLOG_LOGGER_DEBUG(logger, "Verified sig pid,idx={},{}. Is valid {}",
                            origin, next, is_valid);

        replayed.get(origin).set_sig_validity(next, is_valid);

        if (is_valid) {
          try_deliver(r_slot, origin, next);
        } else {
          SPDLOG_LOGGER_CRITICAL(logger, "SIG INVALID FOR ({},{})", origin,
                                 next);
          auto oslot = bcast_bufs.find(origin)->second.slot(origin, next);
          SPDLOG_LOGGER_CRITICAL(logger, "Origin bcast sig valid: {}",
                                 verify_slot(oslot, remotes.key(origin)));
          pending_slots_at[origin] = std::numeric_limits<int>::max();
          pending_remote_slots--;
        }
      });

      next_sig[origin]++;
    }
  }
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

    pending_writes -= static_cast<unsigned>(write_wcs.size());
  }

  std::set<int> resetted;

  for (size_t i = 0; i < write_wcs.size(); i++) {
    auto &wc = write_wcs[i];

    auto const &[is_sig, has_sig, target, receiver, idx] =
        unpack_write_id(wc.wr_id);

    switch (wc.status) {
      case IBV_WC_SUCCESS: {
        {
          auto &p = pending_write_ids_at[receiver];
          std::unique_lock lock(p.first);
          if (!p.second.erase(wc.wr_id)) {
            SPDLOG_LOGGER_WARN(logger, "Didn't erase {}", wc.wr_id);
          }
        }

        // first we ensure that we update the write acks for the signature,
        // as it is important to know if subsequent reads potentially
        // triggered by the `handle_write_ack` routine, were done while having
        // a signature replicated or not.
        if (is_sig || has_sig) {
          handle_sig_write_ack(target, idx);
        }

        if (!is_sig) {
          handle_write_ack(target, idx);
        }

        break;
      }
      default: {
        SPDLOG_LOGGER_WARN(logger, "WC for WRITE({},{}) at {} has status {}",
                           target, idx, receiver, wc.status);

        if (resetted.find(receiver) != resetted.end()) {
          SPDLOG_LOGGER_INFO(logger, "Skipping resetting, already done");
          break;
        }

        resetted.insert(receiver);
        std::unique_lock lock(remotes.get_bcast_mux(receiver));

        auto &p = pending_write_ids_at[receiver];
        std::unique_lock slock(p.first);

        // skip WRs for which an WC was actually created
        size_t ignore = 0;
        for (size_t j = i; j < write_wcs.size(); j++) {
          if (std::get<3>(unpack_write_id(write_wcs[j].wr_id)) == receiver) {
            ignore++;
          }
        }
        auto pending = p.second.size() - ignore;
        SPDLOG_LOGGER_INFO(
            logger, "Decreasing pending writes: {}, with: {}, ignore: {}",
            pending_writes, pending, ignore);
        // reset the capacity
        pending_writes -= static_cast<unsigned>(pending);

        // we have overflown the number
        if (pending_writes > 128) {
          throw std::runtime_error("pending writes did overflow!");
        }

        if (!remotes.broadcast_connection(receiver)) {
          throw std::runtime_error("Cannot find RC for " +
                                   std::to_string(receiver) + " to reconnect");
        }
        auto &rc = remotes.broadcast_connection(receiver)->get();

        rc.reset();
        rc.init(dory::ControlBlock::LOCAL_WRITE |
                dory::ControlBlock::REMOTE_WRITE);
        rc.reconnect();

        for (auto wrid : p.second) {
          auto const &[is_sig_, has_sig_, target_, receiver_, idx_] =
              unpack_write_id(wrid);
          // constantly re-posting writes for own broadcasts to a crashed
          // receiver consumsed lot of resources and decreases performance
          // greatly, Eventually rather switch to a batching mechanism with
          // exponential backoff.
          // TODO(Kristian): incorporate what's described above
          if (target_ == self_id ||
              write_acks[target_][idx_] < simple_minority(process_ids.size())) {
            SPDLOG_LOGGER_INFO(logger, "Re-posting write {},{},{}", receiver_,
                               target_, idx_);
            auto id_bind = wrid;
            post_writer.enqueue([&, id_bind]() {
              auto const &[is_sig, has_sig, target, receiver, idx] =
                  unpack_write_id(id_bind);
              auto &bcast_buf = bcast_bufs.find(self_id)->second;

              post_write(receiver, id_bind, bcast_buf.slot(target, idx).addr(),
                         MEMORY_SLOT_SIZE, bcast_buf.lkey,
                         bcast_buf.get_byte_offset(target, idx));
            });
          }
        }

        p.second.clear();
        SPDLOG_LOGGER_INFO(logger, "WRITE: Size after processing: {}",
                           p.second.size());
      }
    }
  }
}

inline void NonEquivocatingBroadcast::handle_sig_write_ack(int target,
                                                           uint64_t idx) {
  if (++sig_write_acks[target][idx] == simple_minority(process_ids.size()) &&
      target != self_id) {
    replayed.get(target).set_has_sig(idx);
  }

  // gc to keep load low
  if (sig_write_acks[target][idx] == remotes.size()) {
    sig_write_acks[target].erase(idx);
  }
}

inline void NonEquivocatingBroadcast::handle_write_ack(int target,
                                                       uint64_t idx) {
  // SPDLOG_LOGGER_DEBUG(logger, "ACK for tid: {}, idx: {}", target, idx);
  write_acks[target][idx]++;

  // we only require the minority as the local process adds up to a majority
  if (write_acks[target][idx] == simple_minority(process_ids.size())) {
    if (target != self_id) {
      SPDLOG_LOGGER_TRACE(logger, "got WRITE majority for - oid: {}, idx: {}",
                          target, idx);

      // swapping the following two lines changes optimistic source reading
      // with majority reading for all replay reads
      read_replayed_slots_at_sources_for(target, idx);
      // read_replay_registers_for(target, idx);

    } else {
      auto slot = bcast_bufs.find(self_id)->second.slot(self_id, idx);
      deliver(slot, self_id);
    }
  } else if (write_acks[target][idx] == remotes.size()) {
    write_acks[target].erase(idx);
  }
}

inline void NonEquivocatingBroadcast::read_replayed_slots_at_sources_for(
    int origin, uint64_t idx) {
  auto const &[had_sig, ok] = replayed.get(origin).has_sig(idx);
  dory::IGNORE(ok);

  for (int pid : remotes.ids()) {
    if (pid == origin) continue;

    auto r_slot = replay_r_buf->slot(origin, pid, pid, idx, 0);
    size_t writer_offset = dory::neb::BUFFER_SIZE * process_ids.size() *
                           process_pos.find(pid)->second;
    post_read(pid, pack_read_id(had_sig, 0, pid, pid, origin, idx),
              r_slot.addr(), MEMORY_SLOT_SIZE, replay_r_buf->lkey(),
              writer_offset + bcast_bufs.find(self_id)->second.get_byte_offset(
                                  origin, idx));
  }
}

inline void NonEquivocatingBroadcast::read_replay_registers_for(int origin,
                                                                uint64_t idx) {
  auto const &[had_sig, ok] = replayed.get(origin).has_sig(idx);

  dory::IGNORE(ok);

  // first try to read directly from the remotes memory
  for (int pid : remotes.ids()) {
    if (pid == origin) continue;

    auto r_slot = replay_r_buf->slot(origin, pid, pid, idx, 1);
    size_t writer_offset = dory::neb::BUFFER_SIZE * process_ids.size() *
                           process_pos.find(pid)->second;
    post_read(pid, pack_read_id(had_sig, 1, pid, pid, origin, idx),
              r_slot.addr(), MEMORY_SLOT_SIZE, replay_r_buf->lkey(),
              writer_offset + bcast_bufs.find(self_id)->second.get_byte_offset(
                                  origin, idx));
  }

  // now post reads for replicas at other processes
  for (int writer : remotes.ids()) {
    if (writer == origin) continue;

    for (int replicator : remotes.ids()) {
      // we posted this in the previous loop above already
      if (writer == replicator) continue;

      auto r_slot = replay_r_buf->slot(origin, writer, replicator, idx, 1);
      size_t writer_offset = dory::neb::BUFFER_SIZE * process_ids.size() *
                             process_pos.find(writer)->second;
      post_read(
          replicator, pack_read_id(had_sig, 1, writer, replicator, origin, idx),
          r_slot.addr(), MEMORY_SLOT_SIZE, replay_r_buf->lkey(),
          writer_offset +
              bcast_bufs.find(self_id)->second.get_byte_offset(origin, idx));
    }
  }
}

inline void NonEquivocatingBroadcast::read_replay_slot(int origin, int replayer,
                                                       uint64_t idx) {
  auto const &[had_sig, ok] = replayed.get(origin).has_sig(idx);

  dory::IGNORE(ok);

  auto r_slot = replay_r_buf->slot(origin, replayer, replayer, idx, 0);
  auto writer_offset = dory::neb::BUFFER_SIZE * process_ids.size() *
                       process_pos.find(replayer)->second;

  post_read(replayer, pack_read_id(had_sig, 0, replayer, replayer, origin, idx),
            r_slot.addr(), MEMORY_SLOT_SIZE, replay_r_buf->lkey(),
            writer_offset +
                bcast_bufs.find(self_id)->second.get_byte_offset(origin, idx));
}

inline void NonEquivocatingBroadcast::read_replay_register(int origin,
                                                           int replayer,
                                                           uint64_t idx,
                                                           int count) {
  if (count >= 1024) {
    SPDLOG_LOGGER_INFO(logger,
                       "While reading register ({},{},{}): Count >= 1024! "
                       "Starting again from 1",
                       origin, replayer, idx);
    count = 1;
  }

  auto const &[had_sig, ok] = replayed.get(origin).has_sig(idx);

  dory::IGNORE(ok);

  // first read at the source
  {
    auto r_slot = replay_r_buf->slot(origin, replayer, replayer, idx, count);
    auto writer_offset = dory::neb::BUFFER_SIZE * process_ids.size() *
                         process_pos.find(replayer)->second;
    post_read(replayer,
              pack_read_id(had_sig, count, replayer, replayer, origin, idx),
              r_slot.addr(), MEMORY_SLOT_SIZE, replay_r_buf->lkey(),
              writer_offset + bcast_bufs.find(self_id)->second.get_byte_offset(
                                  origin, idx));
  }

  for (int replicator : remotes.ids()) {
    if (replicator == replayer) continue;
    auto r_slot = replay_r_buf->slot(origin, replayer, replicator, idx, count);
    size_t writer_offset = dory::neb::BUFFER_SIZE * process_ids.size() *
                           process_pos.find(replayer)->second;
    post_read(replicator,
              pack_read_id(had_sig, count, replayer, replicator, origin, idx),
              r_slot.addr(), MEMORY_SLOT_SIZE, replay_r_buf->lkey(),
              writer_offset + bcast_bufs.find(self_id)->second.get_byte_offset(
                                  origin, idx));
  }
}

inline void NonEquivocatingBroadcast::consume_read_wcs(
    dory::deleted_unique_ptr<ibv_cq> &cq, std::mt19937 &rng) {
  dory::IGNORE(rng);

  read_wcs.resize(pending_reads);

  if (unlikely(!cb.pollCqIsOK(cq, read_wcs))) {
    SPDLOG_LOGGER_WARN(logger, "Polling on read CQ failed!");
    return;
  }

  if (read_wcs.size() > 0) {
    SPDLOG_LOGGER_DEBUG(logger, "READ CQ polled, size: {}", read_wcs.size());
    pending_reads -= read_wcs.size();

    std::set<int> resetted;

    for (size_t i = 0; i < read_wcs.size(); i++) {
      auto &wc = read_wcs[i];
      auto const &[had_sig, count, wid, rid, oid, idx] =
          unpack_read_id(wc.wr_id);

      // simulate a WC retry error
      // std::uniform_int_distribution<std::mt19937::result_type> dist100(1,
      // 100);

      // auto r = dist100(rng);

      // if (r > 90 && count == 0) {
      //   SPDLOG_LOGGER_INFO(
      //       logger,
      //       "Simulating retry-error for o:{},w:{},r:{},i:{} as value is {}",
      //       oid, wid, rid, idx, r);
      //   wc.status = IBV_WC_RETRY_EXC_ERR;
      // }

      switch (wc.status) {
        case IBV_WC_SUCCESS: {
          // pending_reads_at[rid]--;
          {
            auto &p = pending_read_ids_at[rid];
            std::unique_lock lock(p.first);
            if (!p.second.erase(wc.wr_id)) {
              SPDLOG_LOGGER_CRITICAL(logger, "Didn't erase {}", wc.wr_id);
            }
          }
          if (count == 0 && wid == rid) {
            handle_source_read(had_sig, wid, oid, idx);
          } else {
            handle_replicator_read(had_sig, count, wid, rid, oid, idx);
          }
          break;
        }
        default: {
          // if (count == 0) {
          //   replay_r_buf->free(wid, oid, rid, idx, 0);
          //   auto id_bind = wc.wr_id;
          //   post_reader.enqueue([&, id_bind]() {
          //     auto const &[had_sig, count, wid, rid, oid, idx] =
          //         unpack_read_id(id_bind);
          //     read_replay_register(oid, wid, idx, 1);
          //   });
          //   {
          //     auto &p = pending_read_ids_at[rid];
          //     std::unique_lock lock(p.first);
          //     if (!p.second.erase(wc.wr_id)) {
          //       SPDLOG_LOGGER_CRITICAL(logger, "Didn't erase {}", wc.wr_id);
          //     }
          //   }
          //   break;
          // }

          SPDLOG_LOGGER_WARN(
              logger, "WC for READ at {} for (w={},t={},idx={}) has status {}",
              rid, wid, oid, idx, wc.status);

          if (resetted.find(rid) != resetted.end()) {
            SPDLOG_LOGGER_INFO(logger, "Skipping resetting, already done");
            break;
          }

          resetted.insert(rid);
          std::unique_lock lock(remotes.get_replay_mux(rid));

          auto &p = pending_read_ids_at[rid];
          std::unique_lock slock(p.first);

          // skip WRs for which an WC was actually created
          size_t ignore = 0;
          for (size_t j = i; j < read_wcs.size(); j++) {
            if (std::get<3>(unpack_read_id(read_wcs[j].wr_id)) == rid) {
              ignore++;
            }
          }

          auto pending = p.second.size() - ignore;
          SPDLOG_LOGGER_INFO(
              logger, "Decreasing pending reads: {}, with: {}, ignore: {}",
              pending_reads, pending, ignore);
          pending_reads -= static_cast<unsigned>(pending);
          // we have overflown the number
          if (pending_reads > 128) {
            throw std::runtime_error("pending reads did overflow!");
          }

          if (!remotes.replay_connection(rid)) {
            throw std::runtime_error("Cannot find RC for " +
                                     std::to_string(rid) + " to reconnect");
          }

          auto &rc = remotes.replay_connection(rid)->get();

          rc.reset();
          rc.init(ControlBlock::REMOTE_READ);
          rc.reconnect();

          // check for all outstanding reads at that destination if we need to
          // re-post them or if we can free them
          for (auto wrid : p.second) {
            auto const &[had_sig_, count_, wid_, rid_, oid_, idx_] =
                unpack_read_id(wrid);

            // if the source read failed, then we trigger a replicated
            // register read on all remotes
            if (wid_ == rid_ && count_ == 0) {
              replay_r_buf->free(wid_, oid_, rid_, idx_, 0);
              SPDLOG_LOGGER_INFO(logger,
                                 "Source read failed, falling back to "
                                 "replicated register read for {},{},{},{}",
                                 wid_, oid_, idx_, count_, rid_);
              auto id_bind = wrid;
              post_reader.enqueue([&, id_bind]() {
                auto const &[had_sig, count, wid, rid, oid, idx] =
                    unpack_read_id(id_bind);
                read_replay_register(oid, wid, idx, 1);
              });

            } else {
              auto &swmr_reg = registers[wid_][oid_][idx_];

              // if we already gathered a majority then we add the current
              // remote to the complete list which indicates when we can reuse
              // this count
              if (swmr_reg.is_complete(count_)) {
                replay_r_buf->free(wid_, oid_, rid_, idx_, count_);

                swmr_reg.add_to_complete(count_, rid_);
                if (swmr_reg.complete_list_size(count_) ==
                    minority(process_ids.size())) {
                  swmr_reg.reset(count_);
                  SPDLOG_LOGGER_INFO(logger,
                                     "Resetting {},{},{},{} receiver: {}", wid_,
                                     oid_, idx_, count_, rid_);
                }
              } else {
                SPDLOG_LOGGER_INFO(logger, "Re-reading at {}: {},{},{},{}",
                                   rid_, wid_, oid_, idx_, count_);

                auto id_bind = wrid;
                post_reader.enqueue([&, id_bind]() {
                  auto const &[had_sig, count, wid, rid, oid, idx] =
                      unpack_read_id(id_bind);
                  auto slot = replay_r_buf->slot(oid, wid, rid, idx, count);
                  auto writer_offset = BUFFER_SIZE * process_ids.size() *
                                       process_pos.find(wid)->second;
                  post_read(rid, id_bind, slot.addr(), MEMORY_SLOT_SIZE,
                            replay_r_buf->lkey(),
                            bcast_bufs.find(self_id)->second.get_byte_offset(
                                oid, idx) +
                                writer_offset);
                });
              }
            }
          }

          p.second.clear();
          SPDLOG_LOGGER_INFO(logger, "Size after processing: {}",
                             p.second.size());
        }
      }
    }
  }
}

inline void NonEquivocatingBroadcast::handle_source_read(bool had_sig, int wid,
                                                         int oid,
                                                         uint64_t idx) {
  auto rslot = replay_r_buf->slot(oid, wid, wid, idx, 0);
  handle_register_read(had_sig, 0, wid, wid, oid, idx, rslot);
}

inline void NonEquivocatingBroadcast::handle_replicator_read(bool had_sig,
                                                             int count, int wid,
                                                             int rid, int oid,
                                                             uint64_t idx) {
  SPDLOG_LOGGER_DEBUG(logger,
                      "WC for READ at replicator {} for write from {} over "
                      "target ({},{}), count {}",
                      rid, wid, oid, idx, count);

  // TODO(Kristian): erase can keep the load factor low
  auto &swmr_reg = registers[wid][oid][idx];

  if (swmr_reg.is_complete(count)) {
    swmr_reg.add_to_complete(count, rid);

    if (swmr_reg.complete_list_size(count) == minority(process_ids.size())) {
      // set as reusable
      swmr_reg.reset(count);
      SPDLOG_LOGGER_DEBUG(logger, "Resetting {},{},{},{}", wid, oid, idx,
                          count);
    }
    replay_r_buf->free(wid, oid, rid, idx, count);
    return;
  }

  swmr_reg.add_to(count, rid);

  // we got a majority when including us, so we check only for a minority
  if (swmr_reg.read_list_size(count) == simple_minority(process_ids.size())) {
    SPDLOG_LOGGER_DEBUG(logger, "Got READ majority for wid:{}, oid:{}, idx: {}",
                        wid, oid, idx);

    auto slot = bcast_bufs.find(wid)->second.slot(oid, idx);
    int replicator = self_id;
    bool conflict = false;
    auto readlist = swmr_reg.readlist(count);

    for (auto p : readlist) {
      auto rslot = replay_r_buf->slot(oid, wid, p, idx, count);

      // an empty tmp slot can be released
      if (!rslot.has_content()) {
        replay_r_buf->free(wid, oid, p, idx, count);
        continue;
      } else if (!slot.has_content() && rslot.has_content()) {
        slot = rslot;
        replicator = p;
      } else if (slot.has_content() && rslot.has_content()) {
        if (slot.has_same_data_content_as(rslot)) {
          if (!slot.has_signature() && rslot.has_signature()) {
            slot = rslot;
            replicator = p;
            if (replicator != self_id) {
              replay_r_buf->free(wid, oid, replicator, idx, count);
            }
          } else {
            replay_r_buf->free(wid, oid, p, idx, count);
          }
        } else {
          SPDLOG_LOGGER_WARN(logger,
                             "{} has written conflicting slots for ({},{}). "
                             "({},{}) addr: {} vs ({},{}) addr: {}",
                             wid, oid, idx, slot.id(),
                             *reinterpret_cast<uint64_t *>(slot.addr()),
                             slot.addr(), rslot.id(),
                             *reinterpret_cast<uint64_t *>(rslot.addr()),
                             rslot.addr());

          replay_r_buf->free(wid, oid, p, idx, count);
          conflict = true;
          break;
        }
      }
    }

    if (!conflict) {
      handle_register_read(had_sig, count, wid, replicator, oid, idx, slot);
    } else {
      handle_register_read(had_sig, count, wid, replicator, oid, idx,
                           std::nullopt);
    }

    // gc what might be left over
    for (auto p : readlist) {
      replay_r_buf->free(wid, oid, p, idx, count);
    }

    swmr_reg.set_complete(count);
  }
}

inline void NonEquivocatingBroadcast::handle_register_read(
    bool had_sig, int count, int wid, int rid, int oid, uint64_t idx,
    optional_ref<MemorySlot> opt_slot) {
  SPDLOG_LOGGER_DEBUG(
      logger, "register read wid: {}, oid:{}, idx:{} - hasVal: {}, count: {}",
      wid, oid, idx, opt_slot.has_value(), count);
  auto &p = replayed.get(oid);

  if (!p.exist(idx)) {
    if (rid != self_id) replay_r_buf->free(wid, oid, rid, idx, count);

    return;
  }

  auto own_slot = bcast_bufs.find(self_id)->second.slot(oid, idx);

  // a nullopt indicates that the replayer has replicated conflicting slots
  // and so he's byzantine. We can safely add him to the empty matches
  if (!opt_slot) {
    SPDLOG_LOGGER_WARN(logger, "Got null-opt for {},{},{},{},{}", wid, oid, rid,
                       idx, count);
    p.add_empty(idx, wid);
    // ensure we free the tmp slot before potentially delivering so it can be
    // reused concurrently
    if (rid != self_id) {
      replay_r_buf->free(wid, oid, rid, idx, count);
    }
    try_deliver(own_slot, oid, idx);

    return;
  }

  auto &rr_slot = opt_slot.value().get();

  if (!rr_slot.has_content() || rr_slot.id() == 0) {
    if (had_sig) {
      if (!p.add_empty(idx, wid)) {
        if (rid != self_id) {
          replay_r_buf->free(wid, oid, rid, idx, count);
          return;
        }
      }

      auto const &[processed_sig, ok] = p.processed_sig(idx);
      if (!ok) {
        if (rid != self_id) {
          replay_r_buf->free(wid, oid, rid, idx, count);
          return;
        }
      }

      if (!processed_sig) {
        SPDLOG_LOGGER_TRACE(
            logger,
            "nothing replayed - {} for ({},{}). Local signature "
            "existed, but still not validated. Re-posting!",
            wid, oid, idx);
        // if (rid != self_id) {
        //   replay_r_buf->free(wid, oid, rid, idx, count);
        // }
        post_reader.enqueue([=]() {
          if (wid == rid && count == 0) {
            read_replay_slot(oid, wid, idx);
          } else {
            read_replay_register(oid, wid, idx, count + 1);
          }
        });

      } else {
        if (rid != self_id) {
          replay_r_buf->free(wid, oid, rid, idx, count);
        }
        try_deliver(own_slot, oid, idx);
      }
    } else {
      SPDLOG_LOGGER_TRACE(logger,
                          "nothing replayed - {} for ({},{}). Re-reading as "
                          "local signature is missing!",
                          wid, oid, idx);
      // if (rid != self_id) {
      //   replay_r_buf->free(wid, oid, rid, idx, count);
      // }
      post_reader.enqueue([=]() {
        if (wid == rid && count == 0) {
          read_replay_slot(oid, wid, idx);
        } else {
          read_replay_register(oid, wid, idx, count + 1);
        }
      });
    }
  } else if (own_slot.has_same_data_content_as(rr_slot)) {
    SPDLOG_LOGGER_TRACE(logger, "Match read for ({},{},{})", wid, oid, idx);
    p.add_match(idx, wid);
    // ensure we free the tmp slot before delivering so it can be reused
    // concurrently
    if (rid != self_id) {
      replay_r_buf->free(wid, oid, rid, idx, count);
    }

    try_deliver(own_slot, oid, idx);
  } else {
    SPDLOG_LOGGER_CRITICAL(logger, "CONFLICTING SLOTS r,o,i=({},{},{})", wid,
                           oid, idx);
    // Only now we'll verify the signatures, as the operation is more costly
    // than comparing the slot contents. Anyhow, we want to verify the
    // signature in order to know if the origin broadcaster tried to
    // equivocate or we can count the current processing replayer towards
    // the quorum.
    auto const &[processed_sig, ok] = p.processed_sig(idx);

    if (!ok) {
      if (rid != self_id) {
        replay_r_buf->free(wid, oid, rid, idx, count);
        return;
      }
    }
    if (processed_sig) {
      SPDLOG_LOGGER_TRACE(logger, "Local signature for ({},{},{}) is processed",
                          wid, oid, idx);

      auto const &[sig_valid, ok2] = p.sig_valid(idx);

      if (!sig_valid || !ok2) {
        SPDLOG_LOGGER_TRACE(logger, "Local signature for ({},{},{}) invalid",
                            wid, oid, idx);
        replay_r_buf->free(wid, oid, rid, idx, count);
        // Note: the validating worker thread does decrement the
        // pending_remote_slots as well as sets the maximum value for the
        // pending_slots_at entry for this origin s.t. we don't consider it
        // anymore as it has become byzantine by not providing a valid
        // signature
        return;
      }

      SPDLOG_LOGGER_TRACE(logger, "Local signature for ({},{},{}) is valid",
                          wid, oid, idx);
      if (!rr_slot.has_signature()) {
        SPDLOG_LOGGER_TRACE(logger,
                            "Remote signature for ({},{},{}) not present", wid,
                            oid, idx);
        // check if the signature was already replayed at the time of posting
        // the read corresponding to this WC. If not, we need to re-post the
        // read in order to not violate safety.
        if (!had_sig) {
          SPDLOG_LOGGER_TRACE(logger,
                              "Will retry reading ({},{},{}) as sig was "
                              "missing when posting read",
                              wid, oid, idx);
          // we need to re-post a read once more as the remote might have
          // also read an slot without a signature in our buffer in which
          // case we could violate agreement when adding the replayer
          // immediatelly to the quorum. This way we ensure that at least
          // one process will see the signature of the other and in case
          // of an attempt to equivocate by the broadcaster one process
          // will not add the other to the quorum and thus never deliver
          // the conflicting message.

          // if (rid != self_id) {
          //   replay_r_buf->free(wid, oid, rid, idx, count);
          // }

          post_reader.enqueue([=]() {
            if (wid == rid && count == 0) {
              read_replay_slot(oid, wid, idx);
            } else {
              read_replay_register(oid, wid, idx, count + 1);
            }
          });
        } else {
          SPDLOG_LOGGER_TRACE(
              logger,
              "No remote signature for ({},{},{}) but valid local "
              "sig was present. Adding to empty-read quorum",
              wid, oid, idx);
          // We resend the request already once more to see if in the
          // meanwhile the signature was written at the remote process
          // r_id. If it still not present, then we can include this
          // replayer to the quorum as if he is correct, then he will
          // re-try also once and then see that we have a valid signature
          // so he won't ever deliver his version of the message at the
          // current index.
          p.add_empty(idx, wid);
          replay_r_buf->free(wid, oid, rid, idx, count);
          try_deliver(own_slot, oid, idx);
        }
      } else {
        verify_pool.enqueue(
            [=]() { verify_and_act_on_remote_sig(oid, wid, rid, idx, count); });
      }
    } else {
      SPDLOG_LOGGER_TRACE(logger, "No local processed signature for ({},{},{})",
                          wid, oid, idx);
      if (!rr_slot.has_signature()) {
        SPDLOG_LOGGER_TRACE(
            logger, "Also no remote signature for ({},{},{}). Re-posting read!",
            wid, oid, idx);
        // At this point we don't have any signature but conflicting slots.
        // We need at least one signature in order to be able to know if the
        // broadcaster or replayer is byzantine. Therefore, we re-post a
        // remote read.
        // if (rid != self_id) {
        //   replay_r_buf->free(wid, oid, rid, idx, count);
        // }

        post_reader.enqueue([=]() {
          if (wid == oid && count == 0) {
            read_replay_slot(oid, wid, idx);
          } else {
            read_replay_register(oid, wid, idx, count + 1);
          }
        });
        // NOTE: when re-posting a read infinitelly often we open up an
        // attack vector for a byzantine broadcaster (or replayer) who never
        // includes a signature. This will slow us down as we need to
        // process messages that will never be delivered.
      } else {
        SPDLOG_LOGGER_TRACE(logger, "Remote signature for ({},{},{}) exists",
                            wid, oid, idx);

        verify_pool.enqueue(
            [=]() { verify_and_act_on_remote_sig(oid, wid, rid, idx, count); });
      }
    }
  }
}

inline bool NonEquivocatingBroadcast::verify_slot(
    MemorySlot &slot, dory::crypto::dalek::pub_key &key) {
  auto sig = reinterpret_cast<unsigned char *>(
      const_cast<uint8_t *>(slot.signature()));
  auto msg = reinterpret_cast<unsigned char *>(slot.addr());

  return dory::crypto::dalek::verify(sig, msg, SLOT_SIGN_DATA_SIZE, key);
}

inline void NonEquivocatingBroadcast::post_write(int pid, uint64_t wrid,
                                                 uintptr_t lbuf, uint32_t lsize,
                                                 uint32_t lkey,
                                                 size_t roffset) {
  while (pending_writes + 1 > dory::ControlBlock::CQDepth)
    ;

  if (auto rc = remotes.broadcast_connection(pid)) {
    std::unique_lock lock(remotes.get_bcast_mux(pid));

    auto &p = pending_write_ids_at[pid];

    std::unique_lock slock(p.first);

    if (!p.second.insert(wrid).second) {
      SPDLOG_LOGGER_WARN(logger, "WRITE: Didn't insert {}, already present",
                         wrid);
    }
    // we decrement below if it fails
    pending_writes++;
    auto ret = rc->get().postSendSingle(ReliableConnection::RdmaWrite, wrid,
                                        reinterpret_cast<void *>(lbuf), lsize,
                                        lkey, rc->get().remoteBuf() + roffset);
    if (!ret) {
      auto const &[is_sig, has_sig, target, receiver, idx] =
          unpack_write_id(wrid);

      dory::IGNORE(is_sig);
      dory::IGNORE(has_sig);

      if (!p.second.erase(wrid)) {
        SPDLOG_LOGGER_CRITICAL(logger, "WRITE: Did not erase {}", wrid);
      }
      pending_writes--;

      SPDLOG_LOGGER_WARN(
          logger,
          "POST WRITE at {} for ({},{}) failed, current pending writes: {}",
          receiver, target, idx, pending_writes);
      // re-post
      post_writer.enqueue([=]() {
        SPDLOG_LOGGER_DEBUG(logger, "Handling re-post after post fail");
        post_write(pid, wrid, lbuf, lsize, lkey, roffset);
      });
    }
  }
}

inline void NonEquivocatingBroadcast::post_read(int pid, uint64_t wrid,
                                                uintptr_t lbuf, uint32_t lsize,
                                                uint32_t lkey, size_t roffset) {
  while (pending_reads + 1 > dory::ControlBlock::CQDepth)
    ;

  if (auto rc = remotes.replay_connection(pid)) {
    std::unique_lock lock(remotes.get_replay_mux(pid));

    // pending_reads_at[pid]++;
    pending_reads++;
    auto &p = pending_read_ids_at[pid];
    std::unique_lock slock(p.first);
    if (!p.second.insert(wrid).second) {
      SPDLOG_LOGGER_WARN(logger, "READ: Didnt insert {}, already present",
                         wrid);
    }

    auto ret = rc->get().postSendSingle(ReliableConnection::RdmaRead, wrid,
                                        reinterpret_cast<void *>(lbuf), lsize,
                                        lkey, rc->get().remoteBuf() + roffset);
    if (!ret) {
      auto const &[had_sig, count, rid, wid, tid, mid] = unpack_read_id(wrid);
      dory::IGNORE(had_sig);

      if (!p.second.erase(wrid)) {
        SPDLOG_LOGGER_CRITICAL(logger, "WRITE: Did not erase {}", wrid);
      }
      pending_reads--;

      SPDLOG_LOGGER_WARN(
          logger,
          "POST READ at {} for (c={},w={},t={},mid={}) failed, current "
          "pending reads: {}, read ids: {}",
          rid, count, wid, tid, mid, pending_reads, p.second.size());

      // re-post
      post_reader.enqueue([=]() {
        SPDLOG_LOGGER_INFO(logger, "READ: handling re-post after post fail");
        post_read(pid, wrid, lbuf, lsize, lkey, roffset);
      });
    }
  }
}

inline void NonEquivocatingBroadcast::deliver(MemorySlot &slot, int origin) {
  if (origin != self_id) {
    pending_slots_at[origin]--;

    pending_remote_slots--;
  }

  std::unique_lock<std::mutex> lock(deliver_mux);

  if (origin != self_id) {
    ttd[static_cast<size_t>(process_pos[origin]) * max_msg_count + slot.id()]
        .second = std::chrono::steady_clock::now();
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

inline void NonEquivocatingBroadcast::verify_and_act_on_remote_sig(
    int oid, int wid, int rid, uint64_t idx, int count) {
  if (!replayed.get(oid).exist(idx)) {
    return;
  }

  auto rr_slot = replay_r_buf->slot(oid, wid, rid, idx, count);
  bool replay_sig_valid = verify_slot(rr_slot, remotes.key(oid));

  if (rid != self_id) {
    replay_r_buf->free(wid, oid, rid, idx, count);
  }

  if (!replay_sig_valid) {
    SPDLOG_LOGGER_TRACE(logger, "Remote signature for ({},{},{}) not valid",
                        wid, oid, idx);
    replayed.get(oid).add_empty(idx, wid);

    auto slot = bcast_bufs.find(self_id)->second.slot(oid, idx);

    try_deliver(slot, oid, idx);
  } else {
    SPDLOG_LOGGER_TRACE(
        logger,
        "Remote signature from {} for ({},{}) is valid, Process "
        "{} tried to equivocate!",
        oid, wid, idx, oid);
    // with this we ensure we won't replay any values by this remote anymore
    // also we decrease the concurrency value as this slot won't ever be
    // delivered
    pending_slots_at[oid] = std::numeric_limits<int>::max();
    pending_remote_slots--;
  }
}

}  // namespace async
}  // namespace neb
}  // namespace dory