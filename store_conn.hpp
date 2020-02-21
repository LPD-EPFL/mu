#pragma once

#include <unistd.h>
#include <libmemcached/memcached.h>
#include "util.hpp"

/**
 * Acts as a central public registry for all processes.
 *
 * The `MemoryStore` is a lazy initialized singleton instance and can get
 * accessed though `MemoryStore::getInstance()`.
 */
class MemoryStore {
 public:
  static MemoryStore &getInstance() {
    static MemoryStore instance;

    return instance;
  }

  /**
   * TODO(Kristian): DOC
   * @param key
   * @param value
   * @param len
   */
  void set(const char *key, void *value, size_t len);

  /**
   * TODO(Kristian): DOC
   * @param qp_name
   */
  void set_qp_ready(const char *qp_name);

  /**
   * TODO(Kristian): DOC
   * @param key
   * @param value
   * @return
   */
  int get(const char *key, void **value);

  /**
   * TODO(Kristian): DOC
   * @param qp_name
   */
  hrd_qp_attr_t *get_qp(const char *qp_name);

  /**
   * TODO(Kristian): DOC
   * @param qp_name
   * @param
   */
  void wait_till_ready(const char *qp_name);

 private:
  MemoryStore();
  void operator=(MemoryStore const &);

  /**
   * TODO(Krsitian): DOC
   */
  std::unique_ptr<memcached_st, void (*)(memcached_st *)> memc;
};
