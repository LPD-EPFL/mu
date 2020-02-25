#include <cstdlib>

#include "neb.hpp"

void deliver_callback(void *data) {
  printf("Delivered:\n");
  printf("%s", (char *)data);
}

class NebSampleMessage : public NonEquivocatingBroadcast::Broadcastable {
 public:
  uint64_t val;

  size_t marshall(volatile uint8_t *buf) {
    auto b = reinterpret_cast<volatile uint64_t *>(buf);

    b[0] = val;

    return sizeof(val);
  };

  size_t size() { return sizeof(val); }
};

/*
 * NOTE: we assume IDs starting from 0
 */
int main(int argc, char *argv[]) {
  rt_assert(argc > 1);

  size_t num_proc = DEFAULT_NUM_PROCESSES;
  size_t lgid = atoi(argv[1]);

  // otherwise default is 4
  if (argc > 2) {
    num_proc = atoi(argv[2]);
    rt_assert(num_proc > 1, "at least two nodes are required!");
  }

  std::unique_ptr<NonEquivocatingBroadcast> neb;

  neb = std::make_unique<NonEquivocatingBroadcast>(lgid, num_proc,
                                                   deliver_callback);

  NebSampleMessage m;

  m.val = 1000 + lgid;

  neb->broadcast(1, m);

  std::this_thread::sleep_for(std::chrono::seconds(10));

  return 0;
}