#pragma once

#include <chrono>
#include <cstddef>
#include <future>
#include <map>
#include <memory>
#include <thread>
#include <vector>

#include <dory/conn/rc.hpp>
#include <dory/ctrl/block.hpp>
#include <dory/shared/pointer-wrapper.hpp>
#include <dory/shared/unused-suppressor.hpp>
#include <dory/store.hpp>

namespace dory {
struct OverlayAllocatorState {
  void *buf;
  std::size_t len;
  bool bufused;
  const std::type_info *type;
};

template <typename T>
struct OverlayAllocator {
  typedef T value_type;

  OverlayAllocator() : OverlayAllocator(nullptr, 0) {}

  OverlayAllocator(T *buf, std::size_t len)
      : state(std::make_shared<OverlayAllocatorState, OverlayAllocatorState>(
            {buf, len, false, &typeid(T)})) {}

  template <std::size_t N>
  OverlayAllocator(T (&buf)[N])
      : state(std::make_shared<OverlayAllocatorState, OverlayAllocatorState>(
            {buf, N, false, &typeid(T)})) {}

  template <typename U>
  friend struct OverlayAllocator;

  template <typename U>
  OverlayAllocator(OverlayAllocator<U> other) : state(other.state) {}

  T *allocate(std::size_t n) {
    return static_cast<T *>(state->buf);
    IGNORE(n);
  }

  void deallocate(T *p, std::size_t n) {
    IGNORE(p);
    IGNORE(n);
  }

  template <typename... Args>
  void construct(T *c, Args... args) {
    IGNORE(c);
    IGNORE(args...);
  }

  void destroy(T *c) { IGNORE(c); }

  friend bool operator==(const OverlayAllocator &a, const OverlayAllocator &b) {
    return a.state == b.state;
  }

  friend bool operator!=(const OverlayAllocator &a, const OverlayAllocator &b) {
    return a.state != b.state;
  }

 private:
  std::shared_ptr<OverlayAllocatorState> state;
};

// Usage
// auto alloc = OverlayAllocator<uint8_t>((uint8_t *)data, len);
// std::vector <uint8_t, OverlayAllocator<uint8_t>> p(len, alloc);
// p.resize(len);
}  // namespace dory

namespace dory {
class FailureDetector {
 private:
  static constexpr double gapFactor = 2;
  static constexpr std::chrono::nanoseconds heartbeatRefreshRate =
      std::chrono::nanoseconds(500);
  static constexpr int fail_retry_interval = 1024;
  static constexpr int failed_attempt_limit = 6;
  static constexpr int outstanding_multiplier = 4;
  static constexpr int history_length = 10;

 private:
  struct ReadingStatus {
    ReadingStatus()
        : value{0},
          consecutive_updates{0},
          failed_attempts{0},
          loop_modulo{0} {}

    uint64_t value;
    int consecutive_updates;
    int failed_attempts;
    int loop_modulo;
  };

 public:
  FailureDetector(int my_id, std::vector<int> remote_ids, ControlBlock &cb);
  ~FailureDetector();

  void configure(std::string const &pd, std::string const &mr,
                 std::string send_cp_name, std::string recv_cp_name);

  void announce(MemoryStore &store);

  void connect(MemoryStore &store);

  void heartbeatCounterStart(uint64_t *counter_address);

  void *allocateCountersOverlay(void *start_addr);

  void detect(deleted_unique_ptr<struct ibv_cq> &cq);

  int leaderPID();

 private:
  std::pair<bool, int> valid_ids() const;

 private:
  int my_id;
  std::vector<int> remote_ids;
  ControlBlock &cb;
  std::thread heartbeat;
  int max_id;
  std::map<int, dory::ReliableConnection> rcs;
  // std::vector <uint64_t, OverlayAllocator<uint64_t>> counters_overlay;
  // OverlayAllocator<uint64_t> alloc;

  std::promise<void> exit_signal;

  uint64_t *counters_overlay;
  // std::map<int, uint64_t> readings;
  std::vector<ReadingStatus> status;
  int outstanding;
  bool heartbeat_started;

  std::vector<struct ibv_wc> entries;
};
}  // namespace dory
