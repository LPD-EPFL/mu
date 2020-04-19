#pragma once

#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <thread>
#include <vector>

#include <dory/conn/exchanger.hpp>
#include <dory/conn/rc.hpp>
#include <dory/crypto/sign.hpp>
#include <dory/ctrl/block.hpp>
#include <dory/shared/logger.hpp>
#include <dory/shared/pointer-wrapper.hpp>

#include "broadcastable.hpp"
#include "buffer_overlay.hpp"
#include "message_tracker.hpp"

namespace dory {

using namespace neb;

class NonEquivocatingBroadcast {
 public:
  using deliver_callback =
      std::function<void(uint64_t k, volatile const void *m, size_t proc_id)>;

  /**
   * @param id: of the local process
   * @param remote_ids: vector holding all remote process ids
   * @param cb: reference to the control block
   * @param deliver_cb: synchronized callback to call upon delivery of a message
   *
   */
  NonEquivocatingBroadcast(int self_id, std::vector<int> remote_ids,
                           ControlBlock &cb, deliver_callback dc);
  ~NonEquivocatingBroadcast();

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
  void set_remote_keys(std::map<int, dory::crypto::pub_key> &keys);
  void set_remote_keys(std::map<int, dory::crypto::pub_key> &&keys);

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
   * End operation.
   **/
  void end();

  static constexpr auto PD_NAME = "neb-primary";
  static constexpr auto REPLAY_W_NAME = "neb-replay-w";
  static constexpr auto REPLAY_R_NAME = "neb-replay-r";
  static constexpr auto BCAST_W_NAME = "neb-bcast-w";
  static constexpr auto BCAST_CQ_NAME = "neb-bacst-cq";
  static constexpr auto REPLAY_CQ_NAME = "neb-replay-cq";

  static inline std::string replay_str(int at, int from) {
    std::stringstream s;
    s << "neb-replay-" << at << "-" << from;
    return s.str();
  }

  static inline std::string bcast_str(int from, int to) {
    std::stringstream s;
    s << "neb-broadcast-" << from << "-" << to;
    return s.str();
  }

 private:
  // process id of the local process
  int self_id;

  // ids of the remote processes
  std::vector<int> remote_ids;

  // remote public keys orderd as `remote_id``
  std::map<int, dory::crypto::pub_key> remote_keys;

  // callback to call for delivery of a message
  deliver_callback deliver;

  // mutex for synchronizing the delivery
  std::mutex deliver_mux;

  // RDMA control
  ControlBlock &cb;

  // broadcast reliable connections
  std::map<int, ReliableConnection> bcast_conn;

  // replay reliable connections
  std::map<int, ReliableConnection> replay_conn;

  // Broadcast buffer overlays
  std::map<int, BroadcastBuffer> bcast_bufs;

  // Replay buffer overlay where to write
  std::unique_ptr<ReplayBufferWriter> replay_w_buf;

  // Replay buffer overlay where to store remote replay reads
  std::unique_ptr<ReplayBufferReader> replay_r_buf;

  // beautiful logger
  dory::logger logger;

  // own next message index;
  uint64_t own_next;

  // work completion vector for read CQ polls
  std::vector<struct ibv_wc> read_wcs;

  // work completion vector for write CQ polls
  std::vector<struct ibv_wc> write_wcs;

  // thread that polls the broadcast buffers and replays read values as well
  // as triggers rdma replay reads.
  std::thread poller_thread;

  // thread that polls on the CQs and handles the work completions
  std::thread cq_poller_thread;

  // tracks the current state of the next messages to be delivered by remote
  // processes
  std::map<int, uint64_t> next;

  // stores quorum information on currently replayed but not delivered messages
  std::map<int, std::map<uint64_t, MessageTracker>> replayed;

  // mutex to protect the replayed map
  std::mutex rep_mux;

  // tracks the pending number of rdma reads at any time
  std::atomic_size_t pending_reads;

  // tracks the pending number of rdma writes at any time
  std::atomic_uint32_t pending_writes;

  // mutex to synchronize access to remote process resources. Used when removing
  // remotes dues to crashes
  std::shared_mutex remote_mux;

  /**
   * Starts the replayer thread which tries to deliver messages by remote
   * processes
   **/
  void start_replayer(std::shared_future<void> f);

  /**
   * Starts a thread which consumes WC from the CQs.
   **/
  void start_cq_poller(std::shared_future<void> f);

  /**
   * Consumes and handles the work completion of the replay buffer read CQ.
   * Upon successful and matching replay read of all relevant remote
   *processes this routine also delivers messages by calling the deliver
   *callback.
   **/
  inline void consume_read_wcs(dory::deleted_unique_ptr<ibv_cq> &cq);

  /**
   * Consumes the work completions of the write CQ
   **/
  inline void consume_write_wcs(dory::deleted_unique_ptr<ibv_cq> &cq);

  /**
   * Polls the broacast buffers and replays written values to the replay buffer.
   * Also it triggers rdma reads of remote replay buffers which get handled by
   * the consumer of the replay buffer completion queue in a separate routine.
   **/
  inline void poll_bcast_bufs();

  /**
   * Removes the remote process from the cluster. This routine is called upon
   * read/write WC with status 12
   **/
  void remove_remote(int pid);

  volatile bool connected = false;
  volatile bool started = false;
  volatile bool bcast_poller_running = false;
  volatile bool cq_poller_running = false;

  std::promise<void> exit_signal;
};
}  // namespace dory
