#pragma once

#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <thread>
#include <unordered_map>
#include <vector>

#include <dory/conn/exchanger.hpp>
#include <dory/conn/rc.hpp>
#include <dory/crypto/sign/dalek.hpp>
#include <dory/ctrl/block.hpp>
#include <dory/shared/logger.hpp>
#include <dory/shared/pointer-wrapper.hpp>

#include "../broadcastable.hpp"
#include "../shared/helpers.hpp"
#include "../shared/slot-tracker.hpp"
#include "../shared/thread-pool.hpp"

#include "buffer_overlay.hpp"
#include "mem-pool.hpp"
#include "mem-slot.hpp"
#include "remotes.hpp"
#include "swmr.hpp"

namespace dory {
namespace neb {
namespace async {

class NonEquivocatingBroadcast {
 public:
  using deliver_callback =
      std::function<void(uint64_t k, volatile const void *m, int proc_id)>;

  /**
   * @param id: of the local process
   * @param remote_ids: vector holding all remote process ids
   * @param cb: reference to the control block
   * @param deliver_cb: synchronized callback to call upon delivery of a message
   *
   */
  NonEquivocatingBroadcast(int self_id, std::vector<int> proc_ids,
                           ControlBlock &cb, deliver_callback dc);
  ~NonEquivocatingBroadcast();

  NonEquivocatingBroadcast &operator=(NonEquivocatingBroadcast const &) =
      delete;
  NonEquivocatingBroadcast &operator=(NonEquivocatingBroadcast &&) = delete;
  NonEquivocatingBroadcast(NonEquivocatingBroadcast const &) = delete;
  NonEquivocatingBroadcast(NonEquivocatingBroadcast &&) = delete;

  /**
   * Provides the reliable connected QPs. Requires the number of QPs to equal
   * the number of remote processes for each exchanger.
   *
   * @param bcast_conn: exchanger holding the broadcast QPs
   * @param replay_conn: exchanger holding the replay QPs
   **/
  void set_connections(ConnectionExchanger &bcast_conn,
                       ConnectionExchanger &replay_conn);

  /**
   * Provides the public keys corresponding to the remote processes
   * The order order of the `keys` vector should match the one of the
   * `remote_ids`
   * @param keys: map holding the public keys
   **/
  void set_remote_keys(std::map<int, dory::crypto::dalek::pub_key> &keys);
  void set_remote_keys(std::map<int, dory::crypto::dalek::pub_key> &&keys);

  /**
   * @param uint64_t: message key
   * @param msg: message to broadcast
   */
  void broadcast(uint64_t k, Broadcastable &msg);

  /**
   * Begin operation. Needs to be called after providing the RC connected QPs.
   **/
  void start();

  /**
   * End operation. Writes collected samples to a file
   **/
  void end();

  void resize_ttd(std::map<int, int> &num_msgs);

  /**
   * This method provides the default control path that creates all required
   * resources for a asynchronous NonEquivocatingBroadcast instance to function
   * correctly.
   *
   * One huge buffer is allocated and overlapping memory regions are created.
   * A read only region for the whole buffer, so that remote processes can
   * read any value.
   * And distinct write only regions for every remote process which are only
   * writable by that dedicated process (ensured during MR exchange).
   * The `process_ids` vector determines the positions of the write buffer, and
   * thus it's critical that all processes are provided the same vector.
   *
   * @param cb: reference to the dory-controlblock
   * @param store: reference to the dory-store
   * @param bcast_ce: reference to the broadcast connection exchanger
   * @param replay_ce: reference to the replay connection exchanger
   * @param self_id: id of the local process for which to create the resources
   * @param proc_ids: reference to a vector holding all process ids
   * @param logger: reference to a dory-logger instance
   **/
  static void run_default_async_control_path(
      dory::ControlBlock &cb, dory::MemoryStore &store,
      dory::ConnectionExchanger &bcast_ce, dory::ConnectionExchanger &replay_ce,
      int self_id, std::vector<int> &proc_ids, dory::logger &logger) {
    dory::IGNORE(logger);
    constexpr auto bcast_prefix = "neb-broadcast";
    constexpr auto replay_prefix = "neb-replay";

    cb.registerPD(PD_NAME);

    auto p_len = proc_ids.size();
    // allocate the single buffer
    cb.allocateBuffer(BUF_NAME, dory::neb::BUFFER_SIZE * p_len * p_len, 64);
    // register a MR for remote processes to read the whole buffer
    cb.registerMR(R_MR, PD_NAME, BUF_NAME, dory::ControlBlock::REMOTE_READ);

    cb.registerCQ(W_CQ_NAME);
    cb.registerCQ(R_CQ_NAME);

    for (size_t i = 0; i < p_len; i++) {
      auto pid = proc_ids[i];

      if (pid == self_id) {
        cb.registerMR(REPLAY_W_NAME, PD_NAME, BUF_NAME,
                      i * p_len * dory::neb::BUFFER_SIZE,
                      p_len * dory::neb::BUFFER_SIZE,
                      dory::ControlBlock::LOCAL_WRITE);
      } else {
        auto b_str = bcast_str(pid, self_id);
        // register a overlapping MR with write access to the specific block
        // dedicated to the remote process
        cb.registerMR(
            b_str, PD_NAME, BUF_NAME, i * p_len * dory::neb::BUFFER_SIZE,
            p_len * dory::neb::BUFFER_SIZE,
            dory::ControlBlock::LOCAL_WRITE | dory::ControlBlock::REMOTE_WRITE);

        bcast_ce.configure(pid, PD_NAME, b_str, W_CQ_NAME, W_CQ_NAME);
        bcast_ce.announce(pid, store, bcast_prefix);

        replay_ce.configure(pid, PD_NAME, R_MR, R_CQ_NAME, R_CQ_NAME);
        replay_ce.announce(pid, store, replay_prefix);
      }
    }

    store.set("dory__neb__" + std::to_string(self_id) + "__published", "1");

    SPDLOG_LOGGER_INFO(logger,
                       "Waiting for all remote processes to publish their QPs");

    for (int pid : proc_ids) {
      if (pid == self_id) continue;

      auto key = "dory__neb__" + std::to_string(pid) + "__published";
      std::string val;

      while (!store.get(key, val))
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    bcast_ce.connect_all(store, bcast_prefix, dory::ControlBlock::REMOTE_WRITE);
    replay_ce.connect_all(store, replay_prefix,
                          dory::ControlBlock::REMOTE_READ);
  }

  static constexpr auto PD_NAME = "neb-primary";
  static constexpr auto R_MR = "read-mr";
  static constexpr auto BUF_NAME = "neb-async-unicorn";
  static constexpr auto W_CQ_NAME = "neb-async-w-cq";
  static constexpr auto R_CQ_NAME = "neb-async-r-cq";
  static constexpr auto REPLAY_W_NAME = "neb-replay-w";
  static constexpr auto REPLAY_R_NAME = "neb-replay-r";

 private:
  // process id of the local process
  int self_id;

  std::vector<int> process_ids;

  std::map<int, size_t> process_pos;

  RemoteProcesses remotes;

  // callback to call for delivery of a message
  deliver_callback deliver_cb;

  // mutex for synchronizing the delivery
  std::mutex deliver_mux;

  // RDMA control
  ControlBlock &cb;

  // Broadcast buffer overlays
  std::map<int, BroadcastBuffer> bcast_bufs;

  // Replay buffer overlay where to store remote replay reads
  std::unique_ptr<MemSlotPool> replay_r_buf;

  // beautiful logger
  dory::logger logger;

  // own next message index
  uint64_t own_next;

  // work completion vector for read CQ polls
  std::vector<struct ibv_wc> read_wcs;

  // work completion vector for write CQ polls
  std::vector<struct ibv_wc> write_wcs;

  // thread that polls the broadcast buffer signatures
  std::thread bcast_content_poller;

  // thread that polls the broadcast buffer contents
  std::thread bcast_signature_poller;

  // thread that polls on the CQs and handles the work completions
  std::thread r_cq_poller_thread;

  std::thread w_cq_poller_thread;

  // tracks the current state of the next messages to be replayed
  std::map<int, std::atomic<uint64_t>> next_msg_idx;

  // tracks the current state of the next signature to replayed
  std::map<int, std::atomic<uint64_t>> next_sig;

  // tracks the pending number of rdma reads at any time
  std::atomic<size_t> pending_reads;
  std::map<int, std::pair<std::mutex, std::set<uint64_t>>> pending_write_ids_at;

  // std::map<int, std::atomic<size_t>> pending_reads_at;

  std::map<int, std::pair<std::mutex, std::set<uint64_t>>> pending_read_ids_at;
  // tracks the pending number of rdma writes at any time
  std::atomic<uint32_t> pending_writes;
  // std::map<int, std::atomic<size_t>> pending_writes_at;

  // thread pool with signature verification workers
  SpinningThreadPool verify_pool;

  // thread pool with signature creation workers
  SpinningThreadPool sign_pool;

  // thread pool for posting write WRs
  SpinningThreadPool post_writer;

  // thead pool for posting read WRs
  SpinningThreadPool post_reader;

  // stores state on currently replayed but not delivered messages
  RemotePendingSlots replayed;

  // for preventing the write CQ to be exhausted
  std::mutex read_mux;
  std::condition_variable read_cond;

  // for preventing the read CQ to be exhausted
  std::mutex write_mux;
  std::condition_variable write_cond;

  // for preventing to get above `MAX_CONCURRENTLY_PENDING_SLOTS`
  std::mutex bcast_poll_mux;
  std::condition_variable bcast_poll_cond;

  // currently pending remote slots that are note delivered yet
  std::atomic<int> pending_remote_slots;

  // protection against a byzantine broadcaster who never includes a
  // signature - checked against `MAX_CONCURRENTLY_PENDING_PER_PROCESS`
  std::map<int, std::atomic<int>> pending_slots_at;

  /**
   * Starts the replayer thread which tries to deliver messages by remote
   * processes
   **/
  void start_bcast_content_poller();

  void start_bcast_signature_poller();

  /**
   * Starts a thread which consumes WC from the CQs.
   **/
  void start_r_cq_poller();
  void start_w_cq_poller();

  /**
   * Consumes and handles the work completion of the replay buffer read CQ.
   * Upon successful and matching replay read of all relevant remote
   *processes this routine also delivers messages by calling the deliver
   *callback.
   **/
  inline void consume_read_wcs(dory::deleted_unique_ptr<ibv_cq> &cq,
                               std::mt19937 &rng);

  /**
   * Consumes the work completions of the write CQ
   **/
  inline void consume_write_wcs(dory::deleted_unique_ptr<ibv_cq> &cq);

  inline void handle_write_ack(int target, uint64_t idx);

  inline void handle_sig_write_ack(int target, uint64_t idx);

  inline void read_replay_registers_for(int target, uint64_t idx);

  inline void read_replayed_slots_at_sources_for(int origin, uint64_t idx);

  inline void read_replay_slot(int origin, int replayer, uint64_t idx);

  inline void read_replay_register(int origin, int replayer, uint64_t idx,
                                   int count);

  /**
   * Loops over the remote_ids vector and polls the broacast buffers and replays
   * written values to the replay buffer. Also it triggers rdma reads of remote
   * replay buffers which get handled by the consumer of the replay buffer
   * completion queue in a separate thread.
   **/
  inline void poll_bcast_bufs();

  inline void handle_replicator_read(bool had_sig, int count, int wid, int rid,
                                     int oid, uint64_t idx);

  inline void handle_register_read(bool had_sig, int count, int wid, int rid,
                                   int oid, uint64_t idx,
                                   optional_ref<MemorySlot> rslot);

  inline void handle_source_read(bool had_sig, int wid, int oid, uint64_t idx);

  inline void verify_and_act_on_remote_sig(int o_id, int w_id, int r_id,
                                           uint64_t idx, int count);

  inline void poll_bcast_signatures();

  inline bool verify_slot(MemorySlot &slot, dory::crypto::dalek::pub_key &key);

  inline void post_write(int pid, uint64_t wrid, uintptr_t lbuf, uint32_t lsize,
                         uint32_t lkey, size_t roffset);

  inline void post_read(int pid, uint64_t wrid, uintptr_t lbuf, uint32_t lsize,
                        uint32_t lkey, size_t roffset);

  inline void deliver(MemorySlot &slot, int origin);

  inline void try_deliver(MemorySlot &slot, int origin, uint64_t idx);

  inline void stop_operation();

  std::atomic<bool> connected = false;
  std::atomic<bool> started = false;
  std::atomic<bool> stopped = false;
  std::atomic<bool> bcast_content_poller_running = false;
  std::atomic<bool> bcast_signature_poller_running = false;
  std::atomic<bool> r_cq_poller_running = false;
  std::atomic<bool> w_cq_poller_running = false;

  std::promise<void> exit_signal;
  std::shared_future<void> sf;

  // origin -> index -> acks
  std::map<int, std::unordered_map<uint64_t, size_t>> write_acks;

  std::map<int, std::unordered_map<uint64_t, size_t>> sig_write_acks;

  // replayer -> origin -> index -> register
  std::map<int, std::map<int, std::unordered_map<uint64_t, SWMRRegister>>>
      registers;

  // holds the timepoints  when replaying and delivering a remote slot
  // ttd = time to deliver
  std::vector<std::pair<std::chrono::steady_clock::time_point,
                        std::chrono::steady_clock::time_point>>
      ttd;

  size_t max_msg_count = 100000;
};
}  // namespace async
}  // namespace neb
}  // namespace dory