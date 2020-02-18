#include "hrd.hpp"

// TODO(Kristian): use smart-pointers
class NonEquivocatingBroadcast {
 public:
  NonEquivocatingBroadcast(size_t id, size_t num_p);
  ~NonEquivocatingBroadcast();

  int broadcast(size_t m_id, size_t val);
  void *deliver();

 private:
  std::thread poller_thread;
  // TODO(Kristian): make atomic
  bool running;
  size_t lgid;
  size_t num_proc;
  // TODO(Kristian): use smart pointers
  hrd_ctrl_blk_t *cb;
  hrd_qp_attr_t **bcst_qps;
  hrd_qp_attr_t **repl_qps;
  
  void run_poller();
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