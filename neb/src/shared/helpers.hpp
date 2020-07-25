#pragma once

#include <cstddef>
#include <fstream>

#define BENCH(expr, str)                                                   \
  {                                                                        \
    struct timespec ts1, ts2;                                              \
    clock_gettime(CLOCK_MONOTONIC, &ts1);                                  \
    expr;                                                                  \
    clock_gettime(CLOCK_MONOTONIC, &ts2);                                  \
    SPDLOG_LOGGER_CRITICAL(logger, "benching {} took: {}ns", str,          \
                           (ts2.tv_sec * 1000000000UL + ts2.tv_nsec) -     \
                               (ts1.tv_sec * 1000000000UL + ts1.tv_nsec)); \
  }

template <typename T>
inline constexpr T majority(T n) {
  return (n + 1) / 2;
}

template <typename T>
inline constexpr T minority(T n) {
  return majority(n) - 1;
}

template <typename T>
inline constexpr T simple_majority(T n) {
  return n % 2 == 0 ? (n) / 2 + 1 : majority(n);
}

template <typename T>
inline constexpr T simple_minority(T n) {
  return simple_majority(n) - 1;
}

inline std::string replay_str(int at, int from) {
  std::stringstream s;
  s << "neb-replay-" << at << "-" << from;
  return s.str();
}

inline std::string bcast_str(int from, int to) {
  std::stringstream s;
  s << "neb-broadcast-" << from << "-" << to;
  return s.str();
}

inline void write(
    std::vector<std::pair<std::chrono::steady_clock::time_point,
                          std::chrono::steady_clock::time_point>> &ttd) {
  std::ofstream fs;
  fs.open("/tmp/neb-bench-lat");
  // bool skip = false;
  for (size_t i = 0; i < ttd.size(); i++) {
    auto &p = ttd[i];
    auto diff = std::chrono::duration<double, std::micro>(p.second - p.first);

    if (diff.count() > 0) fs << diff.count() << "\n";
  }
  fs.close();
}