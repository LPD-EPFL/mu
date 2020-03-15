#pragma once

#include <cmath>
#include <iomanip>
#include <iostream>

namespace dory {
namespace units {
// Byte
constexpr size_t operator"" _B(unsigned long long x) { return x; }

// KibiByte
constexpr size_t operator"" _KiB(long double x) {
  return std::llround(x * 1024);
}
constexpr size_t operator"" _KiB(unsigned long long x) { return x * 1024; }

// MebiByte
constexpr size_t operator"" _MiB(long double x) {
  return std::llround(x * 1024 * 1024);
}
constexpr size_t operator"" _MiB(unsigned long long x) {
  return x * 1024 * 1024;
}

// GibiByte
constexpr size_t operator"" _GiB(long double x) {
  return std::llround(x * 1024 * 1024 * 1024);
}

constexpr size_t operator"" _GiB(unsigned long long x) {
  return x * 1024 * 1024 * 1024;
}
}  // namespace units
}  // namespace dory