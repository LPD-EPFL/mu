#pragma once

#include <dory/extern/memcached.hpp>
#include <dory/shared/pointer-wrapper.hpp>
#include <functional>
#include <memory>
#include <utility>
#include <string>

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

  MemoryStore(std::string const &prefix);

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

  std::pair<std::string, uint16_t> ip_port_from_env_var(
      char const *const name) const;
  static constexpr auto RegIPName = "DORY_REGISTRY_IP";
  static constexpr auto MemcacheDDefaultPort = MEMCACHED_DEFAULT_PORT;  // 11211

  /**
   * TODO(Krsitian): DOC
   */
  deleted_unique_ptr<memcached_st> memc;
  std::string prefix;
};
}  // namespace dory
