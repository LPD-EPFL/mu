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
                                                        int length) {
  // omitted: check minimum alignment, check error
  T *raw = 0;
  // using posix_memalign as an example, could be made platform dependent...
  int error = posix_memalign((void **)&raw, alignment, sizeof(T) * length);
  switch (error) {
    case EINVAL:
      throw std::runtime_error(
          "Tried to allocate with improper alignment request");
      break;
    case ENOMEM:
      throw std::runtime_error("Insufficient memory");
      break;
    default:
      break;
  }

  return std::unique_ptr<T[], DeleteAligned<T>>{raw};
}

template <typename T>
using deleted_unique_ptr = std::unique_ptr<T, std::function<void(T *)>>;
}  // namespace dory
