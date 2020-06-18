#include <vector>

/**
 * Until now this SWMR class only stores the ids of remote processes from which
 * it got a ACK for the read. As soon as the minority is reached, together with
 * the local process a read majority is gathered and one can loop over the
 * temporal slots stored in the mem-pool.
 *
 * The completed boolean indicates that all subsequent ACKs can be handled by
 * immediately releasing the slot from the mem-pool, making it available again.
 **/
class SWMRRegister {
 public:
  void set_complete() {
    completed = true;
    readlist.clear();
  }

  // processes
  std::vector<int> readlist;
  bool completed = false;
};
