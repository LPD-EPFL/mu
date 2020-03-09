#pragma once

#include <libmemcached/memcached.h>
#include <dory/shared/pointer-wrapper.hpp>
#include <functional>
#include <memory>

namespace dory {
/**
 * This class acts as a central public registry for all processes.
 * It provides a lazy initialized singleton instance.
 */
class MemoryStore {
 public:
  /**
   * Getter for the signleton instance.
   * @return  MemoryStore
   * */
  static MemoryStore &getInstance() {
    static MemoryStore instance;

    return instance;
  }
  /**
   * Stores the provided string `value` under `key`.
   * @param key
   * @param value
   * @throw `runtime_error`
   */
  void set(std::string const &key, std::string const &value);

  /**
   * Gets the value associated with `key` into `value`.
   * @param key
   * @param value
   * @return bool indicating the success
   * @throw `runtime_error`
   */
  bool get(std::string const &key, std::string &value);

 private:
  MemoryStore();

  char const *env(char const *const name) const;
  // env variable holind the IP of the machine running the memory store
  static constexpr auto RegIP = "DORY_REGISTRY_IP";
  static constexpr auto MemcacheDPort = MEMCACHED_DEFAULT_PORT;  // 11211
  deleted_unique_ptr<memcached_st> memc;
};
}  // namespace dory
