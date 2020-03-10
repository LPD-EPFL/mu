#pragma once

#include <cstdlib>
#include <functional>
#include <memory>

namespace dory {
template <class T>
struct DeleteAligned {
  void operator()(T *data) const { free(data); }
};

template <class T>
std::unique_ptr<T[], DeleteAligned<T>> allocate_aligned(int alignment,
                                                        size_t length) {
  T *raw = reinterpret_cast<T *>(aligned_alloc(alignment, sizeof(T) * length));
  if (raw == nullptr) {
    throw std::runtime_error("Insufficient memory");
  }

  return std::unique_ptr<T[], DeleteAligned<T>>{raw};
}

template <typename T>
using deleted_unique_ptr = std::unique_ptr<T, std::function<void(T *)>>;
}  // namespace dory
