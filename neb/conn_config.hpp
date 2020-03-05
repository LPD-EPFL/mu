#pragma once

#include <memory>
#include <sstream>

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
  std::shared_ptr<volatile uint8_t[]> prealloc_buf = nullptr;

  // Size to use for the newly allocated buffer
  size_t buf_size = 0;

  // Shared memory key
  int buf_shm_key = -1;

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
      : num_qps(num_qps), use_uc(use_uc), prealloc_buf(prealloc_buf),
        buf_size(buf_size), buf_shm_key(buf_shm_key), sq_depth(sq_depth),
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
  builder &num_qps(size_t v) {
    _num_qps = v;
    return *this;
  }

  builder &use_uc(bool v) {
    _use_uc = v;
    return *this;
  }

  builder &prealloc_buf(std::shared_ptr<volatile uint8_t[]> v) {
    _prealloc_buf = v;
    return *this;
  }

  builder &buf_size(size_t v) {
    _buf_size = v;
    return *this;
  }

  builder &buf_shm_key(int v) {
    _buf_shm_key = v;
    return *this;
  }

  builder &sq_depth(size_t v) {
    _sq_depth = v;
    return *this;
  }

  builder &max_rd_atomic(size_t v) {
    _max_rd_atomic = v;
    return *this;
  }

  ConnectionConfig build() const {
    return ConnectionConfig{_num_qps,     _use_uc,   _prealloc_buf, _buf_size,
                            _buf_shm_key, _sq_depth, _max_rd_atomic};
  }

private:
  // num_qps > 0 is used as a validity check
  size_t _num_qps = 0;
  bool _use_uc = false;
  std::shared_ptr<volatile uint8_t[]> _prealloc_buf = nullptr;
  size_t _buf_size = 0;
  int _buf_shm_key = -1;
  size_t _sq_depth = kHrdSQDepth;
  size_t _max_rd_atomic = 16;
};