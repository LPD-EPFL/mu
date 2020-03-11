#pragma once

#include <cstring>
#include <thread>
#include <vector>

#include <dory/conn/rc.hpp>
#include <dory/ctrl/block.hpp>
#include <dory/shared/logger.hpp>

#include "buffer_overlay.hpp"

namespace dory {
class NonEquivocatingBroadcast {
 public:
  typedef void (*deliver_callback)(uint64_t k, volatile const void *m,
                                   size_t proc_id);

  /**
   * Required interface to implement for messages to get broadcasted by this
   * module
   **/
  class Broadcastable {
   public:
    /**
     * @param: pointer to buffer where to marshall the contents of the message
     * @return: size of the written bytes
     **/
    virtual size_t marshall(volatile void *buf) const = 0;
  };

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
   * @param uint64_t: message key
   * @param msg: message to broadcast
   */
  void broadcast(uint64_t k, Broadcastable &msg);

  /**
   * Connects to the remote QPs published to the memory store.
   **/
  void connect_to_remote_qps();

  /**
   * Begin operation. Needs to be called after connecting to remote qps.
   **/
  void start();

  void set_connections(ConnectionExchanger &bcast_conn,
                       ConnectionExchanger &replay_conn);

  static constexpr auto PD_NAME = "neb-primary";
  static constexpr auto REPLAY_W_NAME = "neb-replay-w";
  static constexpr auto REPLAY_R_NAME = "neb-replay-r";
  static constexpr auto BCAST_W_NAME = "neb-bcast-w";

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
  // process id
  int self_id;

  // ids of the remote processes
  std::vector<int> remote_ids;

  // number of processes in the cluster
  int num_proc;

  // callback to call for delivery of a message
  deliver_callback deliver;

  // RDMA control
  ControlBlock &cb;

  // next expected message counter for every process
  std::map<int, uint64_t> next;

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
  std::shared_ptr<spdlog::logger> logger;

  // thread that tries to poll
  std::thread poller_thread;

  void start_poller();

  // TODO(Kristian): make atomic
  volatile bool connected = false;
  volatile bool started = false;
  volatile bool poller_running = false;
  volatile bool poller_finished = false;
};
}  // namespace dory
