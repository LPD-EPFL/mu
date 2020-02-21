#include <thread>

#include "ctrl_block.hpp"
#include "store_conn.hpp"

class NonEquivocatingBroadcast {
 public:
  /**
   * TODO(Kristian): doc
   * @param id: of the local process
   * @param num_proc: number of processes in the cluster
   *
   */
  NonEquivocatingBroadcast(size_t id, size_t num_proc);
  ~NonEquivocatingBroadcast();

  /**
   * TODO(Kristian): doc
   * @param m_id: id of the message
   * @param val: value of the message
   */
  void broadcast(size_t m_id, size_t val);
  /**
   * TODO(Kristian): doc
   *
   */
  void *deliver();

 private:
  // local id
  size_t lgid;

  // number of processes in the cluster
  size_t num_proc;

  // RDMA connector
  std::unique_ptr<ControlBlock> cb;

  // thread that tries to poll
  std::thread poller_thread;

  // starts the poller
  void start_poller();

  // ensures only one thread loops endlessly
  // TODO(Kristian): make atomic
  bool poller_running = false;

  int post_write(ibv_sge sg, size_t proc_id);

  int post_replay_read(ibv_sge sg, size_t o_id, size_t d_id);
};

// TODO(Kristian): Classify
struct neb_msg_t {
  uint64_t id;
  void *data;
  size_t len;

  size_t marshall(uint8_t *buf) {
    size_t data_size = len * sizeof(uint8_t);
    size_t msg_size = 2 * sizeof(id) + data_size;

    buf[0 * sizeof(uint64_t)] = id;
    buf[1 * sizeof(uint64_t)] = data_size;

    memcpy(&buf[2 * sizeof(uint64_t)], data, msg_size);

    return msg_size;
  };

  void unmarshall(uint8_t *buf) {
    id = (uint64_t)buf[0 * sizeof(uint64_t)];
    len = (uint64_t)buf[1 * sizeof(uint64_t)];
    data = (void *)&buf[2 * sizeof(uint64_t)];
  }

  size_t size() { return 2 * sizeof(id) + len; }
};