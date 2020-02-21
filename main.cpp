#include <cstdlib>

#include "neb.hpp"
#include "util.hpp"

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

  hrd_ibv_devinfo();

  std::unique_ptr<NonEquivocatingBroadcast> neb;

  neb = std::make_unique<NonEquivocatingBroadcast>(lgid, num_proc);

  neb->broadcast(1, 1337);

  std::this_thread::sleep_for(std::chrono::seconds(10));

  return 0;
}
