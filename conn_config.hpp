#pragma once

#include "consts.hpp"

class ConnectionConfig {
 public:
  // Builder class
  class builder;

  // ----------------------------- Required params -----------------------------
  // number of QPs
  size_t num_qps = 0;

  // Unreliable Connection Transport flag
  bool use_uc = false;

  // Preallocated buffer
  std::shared_ptr<volatile uint8_t[]> prealloc_buf;

  // Size to use for the newly allocated buffer
  size_t buf_size;

  // Shared memory key
  int buf_shm_key;

  // ----------------------------- Optional params -----------------------------
  // Request capacity
  size_t sq_depth = kHrdSQDepth;

  // The number of RDMA Reads & atomic operations outstanding at any time
  // that can be handled by this QP as an initiator
  size_t max_rd_atomic = 16;

  ConnectionConfig(size_t num_qps, bool use_uc,
                   std::shared_ptr<volatile uint8_t[]> prealloc_buf,
                   size_t buf_size, int buf_shm_key, size_t sq_depth,
                   size_t max_rd_atomic)
      : num_qps(num_qps),
        use_uc(use_uc),
        prealloc_buf(prealloc_buf),
        buf_size(buf_size),
        buf_shm_key(buf_shm_key),
        sq_depth(sq_depth),
        max_rd_atomic(max_rd_atomic) {}

  std::string to_string() {
    std::ostringstream ret;
    ret << "[num_qps " << std::to_string(num_qps) << ", use_uc "
        << std::to_string(use_uc) << ", buf size " << std::to_string(buf_size)
        << ", shm key " << std::to_string(buf_shm_key) << ", sq_depth "
        << std::to_string(sq_depth) << ", max_rd_atomic "
        << std::to_string(max_rd_atomic) << "]";
    return ret.str();
  }
};

class ConnectionConfig::builder {
 public:
  builder &set__num_qps(size_t v) {
    num_qps = v;
    return *this;
  }

  builder &set__use_uc(bool v) {
    use_uc = v;
    return *this;
  }

  builder &set__prealloc_buf(std::shared_ptr<volatile uint8_t[]> v) {
    prealloc_buf = v;
    return *this;
  }

  builder &set__buf_size(size_t v) {
    buf_size = v;
    return *this;
  }

  builder &set__buf_shm_key(int v) {
    buf_shm_key = v;
    return *this;
  }

  builder &set__sq_depth(size_t v) {
    sq_depth = v;
    return *this;
  }

  builder &set__max_rd_atomic(size_t v) {
    max_rd_atomic = v;
    return *this;
  }

  ConnectionConfig build() const {
    return ConnectionConfig{num_qps,     use_uc,   prealloc_buf, buf_size,
                            buf_shm_key, sq_depth, max_rd_atomic};
  }

 private:
  // num_qps > 0 is used as a validity check
  size_t num_qps = 0;
  bool use_uc = false;
  std::shared_ptr<volatile uint8_t[]> prealloc_buf;
  size_t buf_size;
  int buf_shm_key;
  size_t sq_depth = kHrdSQDepth;
  size_t max_rd_atomic = 16;
};