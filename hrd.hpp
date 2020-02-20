#pragma once

#include <assert.h>
#include <errno.h>
#include <infiniband/verbs.h>
#include <libmemcached/memcached.h>
#include <malloc.h>
#include <numaif.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

#include "conn_config.hpp"
#include "consts.hpp"

#define KB(x) (static_cast<size_t>(x) << 10)
#define KB_(x) (KB(x) - 1)
#define MB(x) (static_cast<size_t>(x) << 20)
#define MB_(x) (MB(x) - 1)

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define _unused(x) ((void)(x))  // Make production build happy

/// Check a condition at runtime. If the condition is false, throw exception.
static inline void rt_assert(bool condition, std::string throw_str) {
  if (unlikely(!condition)) throw std::runtime_error(throw_str);
}

/// Check a condition at runtime. If the condition is false, throw exception.
static inline void rt_assert(bool condition) {
  if (unlikely(!condition)) throw std::runtime_error("");
}

template <typename T>
static constexpr inline bool is_power_of_two(T x) {
  return x && ((x & T(x - 1)) == 0);
}

template <uint64_t power_of_two_number, typename T>
static constexpr inline T round_up(T x) {
  static_assert(is_power_of_two(power_of_two_number),
                "PowerOfTwoNumber must be a power of 2");
  return ((x) + T(power_of_two_number - 1)) & (~T(power_of_two_number - 1));
}

/// Return nanoseconds elapsed since timestamp \p t0
static double ns_since(const struct timespec &t0) {
  struct timespec t1;
  clock_gettime(CLOCK_REALTIME, &t1);
  return (t1.tv_sec - t0.tv_sec) * 1000000000.0 + (t1.tv_nsec - t0.tv_nsec);
}

/// Registry info about a QP
struct hrd_qp_attr_t {
  char name[QP_NAME_SIZE];
  uint16_t lid;
  uint32_t qpn;
  union ibv_gid gid;  ///< GID, used for only RoCE

  // Info about the RDMA buffer associated with this QP
  uintptr_t buf_addr;
  uint32_t buf_size;
  uint32_t rkey;
};

/// InfiniBand info resolved from \p phy_port, must be filled by constructor.
class IBResolve {
 public:
  // Device index in list of verbs devices
  int device_id;
  // TODO: use smart pointer
  // The verbs device context
  struct ibv_context *ib_ctx;
  // 1-based port ID in device. 0 is invalid.
  uint8_t dev_port_id;
  // LID of phy_port. 0 is invalid.
  uint16_t port_lid;
  // GID, used only for RoCE
  union ibv_gid gid;
};

// Debug
void hrd_ibv_devinfo(void);

IBResolve hrd_resolve_port_index(size_t port_index);

// Fill @wc with @num_comps comps from this @cq. Exit on error.
static inline void hrd_poll_cq(struct ibv_cq *cq, int num_comps,
                               struct ibv_wc *wc) {
  int comps = 0;
  while (comps < static_cast<int>(num_comps)) {
    int new_comps = ibv_poll_cq(cq, num_comps - comps, &wc[comps]);
    if (new_comps != 0) {
      // Ideally, we should check from comps -> new_comps - 1
      if (wc[comps].status != 0) {
        fprintf(stderr, "Bad wc status %d\n", wc[comps].status);
        exit(0);
      }

      comps += new_comps;
    }
  }
}

// Fill @wc with @num_comps comps from this @cq. Return -1 on error, else 0.
static inline int hrd_poll_cq_ret(struct ibv_cq *cq, int num_comps,
                                  struct ibv_wc *wc) {
  int comps = 0;

  while (comps < num_comps) {
    int new_comps = ibv_poll_cq(cq, num_comps - comps, &wc[comps]);
    if (new_comps != 0) {
      // Ideally, we should check from comps -> new_comps - 1
      if (wc[comps].status != 0) {
        fprintf(stderr, "Bad wc status %d\n", wc[comps].status);
        return -1;  // Return an error so the caller can clean up
      }

      comps += new_comps;
    }
  }

  return 0;  // Success
}

// Utility functions
static inline uint32_t hrd_fastrand(uint64_t *seed) {
  *seed = *seed * 1103515245 + 12345;
  return static_cast<uint32_t>((*seed) >> 32);
}

static inline size_t hrd_get_cycles() {
  uint64_t rax;
  uint64_t rdx;
  asm volatile("rdtsc" : "=a"(rax), "=d"(rdx));
  return static_cast<size_t>((rdx << 32) | rax);
}

static inline int hrd_is_power_of_2(uint64_t n) { return n && !(n & (n - 1)); }

uint8_t *hrd_malloc_socket(int shm_key, size_t size, size_t socket_id);
int hrd_free(int shm_key, void *shm_buf);
void hrd_red_printf(const char *format, ...);
void hrd_get_formatted_time(char *timebuf);
void hrd_nano_sleep(size_t ns);
char *hrd_getenv(const char *name);
void hrd_bind_to_core(std::thread &thread, size_t n);
