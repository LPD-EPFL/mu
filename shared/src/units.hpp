#pragma once

#include <cmath>
#include <iomanip>
#include <iostream>

namespace dory {
namespace units {
// Byte
size_t operator"" _B(unsigned long long x) { return x; }

// KibiByte
size_t operator"" _KiB(long double x) { return std::llround(x * 1024); }
size_t operator"" _KiB(unsigned long long x) { return x * 1024; }

// MebiByte
size_t operator"" _MiB(long double x) { return std::llround(x * 1024 * 1024); }
size_t operator"" _MiB(unsigned long long x) { return x * 1024 * 1024; }

// GibiByte
size_t operator"" _GiB(long double x) {
  return std::llround(x * 1024 * 1024 * 1024);
}

size_t operator"" _GiB(unsigned long long x) { return x * 1024 * 1024 * 1024; }
}  // namespace units
}  // namespace dory