#pragma once

#include <cstddef>
#include <fstream>

template <typename T>
inline constexpr T majority(T n) {
  return (n + 1) / 2;
}

template <typename T>
inline constexpr T minority(T n) {
  return majority(n) - 1;
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
  for (auto &p : ttd) {
    auto diff = std::chrono::duration<double, std::micro>(p.second - p.first);
    if (diff.count() > 0) fs << diff.count() << "\n";
  }
}