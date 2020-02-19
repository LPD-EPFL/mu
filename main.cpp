#include "neb.hpp"

#include <cstdlib>

/*
 * NOTE: we assume IDs starting from 0
 */
int main(int argc, char *argv[]) {
  rt_assert(argc > 1);

  size_t lgid;
  size_t num_proc = 4;

  lgid = atoi(argv[1]);

  // otherwise default is 4
  if (argc > 2) {
    num_proc = atoi(argv[2]);
    rt_assert(num_proc > 1, "at least two nodes are required!");
  }

  std::unique_ptr<NonEquivocatingBroadcast> neb;

  neb = std::make_unique<NonEquivocatingBroadcast>(lgid, num_proc);

  neb->broadcast(1, 1337);

  std::this_thread::sleep_for(std::chrono::seconds(10));

  // close memcached connection
  hrd_close_memcached();

  return 0;
}
