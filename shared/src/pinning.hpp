#pragma once

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <thread>

namespace dory {
inline void pinThreadToCore(std::thread &thd, int cpu_id) {
  // Create a cpu_set_t object representing a set of CPUs. Clear it and mark
  // only CPU i as set.
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(cpu_id, &cpuset);
  int rc =
      pthread_setaffinity_np(thd.native_handle(), sizeof(cpu_set_t), &cpuset);
  if (rc != 0) {
    throw std::runtime_error("Error calling pthread_setaffinity_np: " +
                             std::string(std::strerror(errno)));
  }
}

inline void set_thread_name(std::thread::native_handle_type pthread,
                            const char *name) {
  int rc = pthread_setname_np(pthread, name);

  if (rc != 0) {
    throw std::runtime_error("Error calling pthread_setname_np: " +
                             std::string(std::strerror(errno)));
  }
}

inline void set_thread_name(std::thread &thd, const char *name) {
  set_thread_name(thd.native_handle(), name);
}

}  // namespace dory