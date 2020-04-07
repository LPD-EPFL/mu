#pragma once

namespace dory {
struct LogConfig {
  static constexpr int Alignment = 64;

  static constexpr bool is_powerof2(size_t v) {
    return v && ((v & (v - 1)) == 0);
  }

  static constexpr size_t round_up_powerof2(size_t v) {
    return (v + Alignment - 1) & (-static_cast<ssize_t>(Alignment));
  }
};
}  // namespace dory