#pragma once

#include <dory/shared/pointer-wrapper.hpp>
#include <functional>
#include <libmemcached/memcached.h>
#include <memory>

namespace dory {
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
   */
  void set(std::string const &key, std::string const &value);

  // /**
  //  * TODO(Kristian): DOC
  //  * @param qp_name
  //  */
  // void set_qp_ready(const char *qp_name);

  /**
   * TODO(Kristian): DOC
   * @param key
   * @param value
   * @return
   */
  bool get(std::string const &key, std::string &value);

  // /**
  //  * TODO(Kristian): DOC
  //  * @param qp_name
  //  */
  // hrd_qp_attr_t *get_qp(const char *qp_name);

  // /**
  //  * TODO(Kristian): DOC
  //  * @param qp_name
  //  * @param
  //  */
  // void wait_till_ready(const char *qp_name);

private:
  MemoryStore();
  // void operator=(MemoryStore const &);

  char const *env(char const *const name) const;
  static constexpr auto RegIP = "DORY_REGISTRY_IP";
  static constexpr auto MemcacheDPort = MEMCACHED_DEFAULT_PORT; // 11211

  /**
   * TODO(Krsitian): DOC
   */
  deleted_unique_ptr<memcached_st> memc;
};
} // namespace dory