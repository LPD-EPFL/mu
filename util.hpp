#pragma once

#include <memory>
#include <cstring>
#include <cassert>
#include <stdexcept>
#include <infiniband/verbs.h>

#include "consts.hpp"

std::string link_layer_str(uint8_t link_layer);

char *get_env(const char *name);

void hrd_ibv_devinfo(void);

// Check a condition at runtime. If the condition is false, throw exception.
static inline void rt_assert(bool condition, std::string throw_str) {
  if (__builtin_expect(!!(!condition), 0)) throw std::runtime_error(throw_str);
}

// Check a condition at runtime. If the condition is false, throw exception.
static inline void rt_assert(bool condition) {
  if (__builtin_expect(!!(!condition), 0)) throw std::runtime_error("");
}

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