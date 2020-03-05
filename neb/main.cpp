#include <cstdlib>

#include "neb.hpp"
#include <dory/store.hpp>

class NebSampleMessage : public NonEquivocatingBroadcast::Broadcastable {
public:
  uint64_t val;

  size_t marshall(volatile uint8_t *buf) {
    auto b = reinterpret_cast<volatile uint64_t *>(buf);

    b[0] = val;

    return sizeof(val);
  };

  void unmarshall(const volatile uint8_t &buf) {
    val = reinterpret_cast<const volatile uint64_t &>(buf);
  }

  size_t size() { return sizeof(val); }
};

void deliver_callback(uint64_t k, const volatile uint8_t &m, size_t proc_id) {
  NebSampleMessage msg;

  msg.unmarshall(m);

  printf("main: delivered (%lu, %lu) by %lu \n", k, msg.val, proc_id);
}

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
  for (int i = 1; i <= 200; i++) {
    m.val = 1000 * lgid + i;
    neb->broadcast(i, m);
    usleep(20000);
  }

  auto &store = dory::MemoryStore::getInstance();

  store.set("Hello", "World");

  printf("main: sleep for 1 sec\n");
  std::this_thread::sleep_for(std::chrono::seconds(1));

  return 0;
}