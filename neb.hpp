#include <cstring>
#include <thread>
#include <vector>

#include "buffer_overlay.hpp"
#include "ctrl_block.hpp"
#include "store_conn.hpp"

class NonEquivocatingBroadcast {
 public:
  class Broadcastable {
   public:
    virtual size_t marshall(volatile uint8_t *buf) = 0;
  };
  /**
   * TODO(Kristian): doc
   * @param id: of the local process
   * @param num_proc: number of processes in the cluster
   *
   */
  NonEquivocatingBroadcast(size_t id, size_t num_proc,
                           void (*deliver_cb)(uint64_t k, volatile uint8_t *m,
                                              size_t proc_id));
  ~NonEquivocatingBroadcast();

  /**
   * TODO(Kristian): doc
   * @param msg_id: id of the message
   * @param val: value of the message
   */
  void broadcast(uint64_t k, Broadcastable &msg);

 private:
  // local id
  size_t lgid;

  // number of processes in the cluster
  size_t num_proc;

  // last received message counter for every process
  std::unique_ptr<uint64_t[]> last;

  std::vector<std::unique_ptr<BroadcastBuffer>> bcast_buf;
  std::unique_ptr<ReplayBufferWriter> replay_w_buf;
  std::unique_ptr<ReplayBufferReader> replay_r_buf;

  // RDMA connector
  std::unique_ptr<ControlBlock> cb;

  // thread that tries to poll
  std::thread poller_thread;

  // starts the poller
  void start_poller();

  void (*deliver_callback)(uint64_t k, volatile uint8_t *m, size_t proc_id);

  // ensures only one thread loops endlessly
  // TODO(Kristian): make atomic
  bool poller_running = false;

  int post_write(ibv_sge sg, size_t dest_id, uint64_t msg_offset);

  int post_replay_read(ibv_sge sg, size_t r_id, uint64_t msg_offset);
};
