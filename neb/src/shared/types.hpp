#pragma once

#include <optional>
#include <shared_mutex>

template <typename T>
using optional_ref = std::optional<std::reference_wrapper<T>>;

template <typename T>
class ShareLockedRef {
 public:
  ShareLockedRef(const T &v, std::shared_mutex &mux) : v(v), lock(mux) {}

  const T &get() { return v; }

  const T &operator*() { return v; }

 private:
  const T &v;
  std::shared_lock<std::shared_mutex> lock;
};
