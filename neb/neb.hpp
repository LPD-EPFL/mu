#pragma once

#include <cstdint>
#include <cstring>
#include <memory>
#include <set>
#include <thread>
#include <vector>

#include <dory/conn/exchanger.hpp>
#include <dory/conn/rc.hpp>
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
   * @param deliver_cb: callback to call upon delivery of a message
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
   * @param uint64_t: message key
   * @param msg: message to broadcast
   */
  void broadcast(uint64_t k, Broadcastable &msg);

  /**
   * Begin operation. Needs to be called after providing the RC connected QPs.
   **/
  void start();

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

  // callback to call for delivery of a message
  deliver_callback deliver;

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

  // thread that polls replay read responses and tries to deliver messages
  // from remote processes.
  std::thread replay_reader_thread;

  // tracks the current state of the next messages to be delivered by remote
  // processes
  std::map<int, uint64_t> next;

  // stores quorum information on currently replayed but not delivered messages
  std::map<int, std::map<uint64_t, MessageTracker>> replayed;

  // mutex to protect the replayed map
  std::mutex rep_mux;

  // tracks the pending number of rdma reads at any time
  size_t pending_reads;

  /**
   * Starts the replayer thread which tries to deliver messages by remote
   * processes
   **/
  void start_replayer(std::shared_future<void> f);

  /**
   * Starts the reader thread which consumes WC by the replay read CQ.
   **/
  void start_reader(std::shared_future<void> f);

  /**
   * Consumes and handles the work completion of the replay buffer read CQ.
   * Upon successful and matching replay read of all relevant remote processes
   * this routine also delivers messages by calling the deliver callback.
   **/
  inline void consume(dory::deleted_unique_ptr<ibv_cq> &cq);

  /**
   * Polls the broacast buffers and replays written values to the replay buffer.
   * Also it triggers rdma reads of remote replay buffers which get handled by
   * the consumer of the replay buffer completion queue in a separate routine.
   **/
  inline void poll_bcast_bufs();

  // TODO(Kristian): make atomic
  volatile bool connected = false;
  volatile bool started = false;
  volatile bool poller_running = false;
  volatile bool reader_running = false;

  std::promise<void> exit_signal;
};
}  // namespace dory
