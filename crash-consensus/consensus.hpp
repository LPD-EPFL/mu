#pragma once

// #include <ios>
// #include <iostream>

// #include <thread>
// #include <random>
// #include <chrono>

// #include <array>
// #include <atomic>
// #include <map>
// #include <sstream>

// #include <dory/conn/exchanger.hpp>
// #include <dory/conn/rc.hpp>
// #include <dory/ctrl/block.hpp>
// #include <dory/ctrl/device.hpp>
// #include <dory/shared/unused-suppressor.hpp>
// #include <dory/store.hpp>

// #include <algorithm>
// #include <functional>

// #include <dory/shared/units.hpp>

// #include "log.hpp"
// #include "response-tracker.hpp"
// #include "memory.hpp"

// #include "slow-path.hpp"
// #include "leader-switch.hpp"
// #include "timers.h"
// #include "branching.hpp"
// #include "pinning.hpp"
// #include "config.hpp"
// // #include "readerwriterqueue.h"

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>
#include "readerwriterqueue.h"

namespace dory {
struct Request {
  Request() {}
  Request(uint8_t *buf, size_t buf_len) : buf{buf}, buf_len{buf_len} {}

  uint8_t *buf;
  size_t buf_len;
};

class RdmaConsensus {
 public:
  RdmaConsensus(int my_id, std::vector<int> &remote_ids);
  ~RdmaConsensus();

  bool propose(uint8_t *buf, size_t len);

 private:
  void run();

 private:
  int my_id;
  std::vector<int> remote_ids;
  alignas(64) std::atomic<bool> request;

  size_t allocated_size;
  size_t alignment;

  alignas(1024) moodycamel::ReaderWriterQueue<bool> output_spsc;
  alignas(1024) moodycamel::ReaderWriterQueue<Request> input_spsc;

  std::thread consensus_thd;

  bool not_leader;
};
}  // namespace dory