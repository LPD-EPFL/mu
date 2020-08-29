#include <unistd.h>
#include <cstring>
#include <regex>
#include <stdexcept>

#include "store.hpp"

namespace dory {
MemoryStore::MemoryStore() : memc(memcached_create(nullptr), memcached_free), prefix{""} {
  if (memc.get() == nullptr) {
    throw std::runtime_error("Failed to create memcached handle");
  }

  auto [ip, port] = ip_port_from_env_var(RegIPName);
  memcached_return rc;

  deleted_unique_ptr<memcached_server_st> servers(
      memcached_server_list_append(nullptr, ip.c_str(), port, &rc),
      memcached_server_list_free);

  auto push_ret = memcached_server_push(memc.get(), servers.get());
  if (push_ret != MEMCACHED_SUCCESS) {
    throw std::runtime_error(
        "Could not add memcached server in the MemoryStore: " +
        std::string(memcached_strerror(memc.get(), push_ret)));
  }
}

MemoryStore::MemoryStore(std::string const &prefix_) : MemoryStore() {
   prefix = prefix_;
}

void MemoryStore::set(std::string const &key, std::string const &value) {
  if (key.length() == 0 || value.length() == 0) {
    throw std::runtime_error("Empty key or value");
  }

  memcached_return rc;
  std::string prefixed_key = prefix + key;
  rc = memcached_set(memc.get(), prefixed_key.c_str(), prefixed_key.length(), value.c_str(),
                     value.length(), static_cast<time_t>(0),
                     static_cast<uint32_t>(0));

  if (rc != MEMCACHED_SUCCESS) {
    throw std::runtime_error("Failed to set to the store the (K, V) = (" + key +
                             ", " + value + ")");
  }
}

bool MemoryStore::get(std::string const &key, std::string &value) {
  if (key.length() == 0) {
    throw std::runtime_error("Empty key");
  }

  memcached_return rc;
  size_t value_length;
  uint32_t flags;

  std::string prefixed_key = prefix + key;
  char *ret_value = memcached_get(memc.get(), prefixed_key.c_str(), prefixed_key.length(),
                                  &value_length, &flags, &rc);
  deleted_unique_ptr<char> ret_value_uniq(ret_value, free);

  if (rc == MEMCACHED_SUCCESS) {
    std::string ret(ret_value, value_length);
    value += ret;
    return true;
  } else if (rc == MEMCACHED_NOTFOUND) {
    return false;
  } else {
    throw std::runtime_error("Failed to get the store the K = " + key);
  }

  // Never reached
  return false;
}

std::pair<std::string, uint16_t> MemoryStore::ip_port_from_env_var(
    char const *const name) const {
  char const *env = getenv(name);
  if (env == nullptr) {
    throw std::runtime_error("Environment variable " + std::string(name) +
                             " not set");
  }

  std::string s(env);
  std::regex regex(":");

  std::vector<std::string> split_string(
      std::sregex_token_iterator(s.begin(), s.end(), regex, -1),
      std::sregex_token_iterator());

  switch (split_string.size()) {
    case 0:
      throw std::runtime_error("Environment variable " + std::string(name) +
                               " contains insufficient data");
      break;

    case 1:
      return std::make_pair(split_string[0], MemcacheDDefaultPort);
    case 2:
      return std::make_pair(split_string[0], stoi(split_string[1]));

    default:
      throw std::runtime_error("Environment variable " + std::string(name) +
                               " contains excessive data");
  }

  // Unreachable
  return std::make_pair("", 0);
}

// // To check if a queue pair with name qp_name is ready, we check if this
// // key-value mapping exists: "RESERVED_NAME_PREFIX-qp_name" -> "hrd_ready".
// void MemoryStore::wait_till_ready(const char *qp_name) {
//   char *value;
//   char exp_value[QP_NAME_SIZE];
//   sprintf(exp_value, "%s", "hrd_ready");

//   char new_name[2 * QP_NAME_SIZE];
//   sprintf(new_name, "%s", RESERVED_NAME_PREFIX);
//   strcat(new_name, qp_name);

//   int tries = 0;
//   while (true) {
//     int ret = get(new_name, reinterpret_cast<void **>(&value));
//     tries++;
//     if (ret > 0) {
//       if (strcmp(value, exp_value) == 0) {
//         free(value);

//         return;
//       }
//     }

//     free(value);
//     usleep(200000);

//     if (tries > 100) {
//       fprintf(stderr, "HRD: Waiting for QP %s to be ready\n", qp_name);
//       tries = 0;
//     }
//   }
// }

// // To advertise a queue pair with name qp_name as ready, we publish this
// // key-value mapping: "RESERVED_NAME_PREFIX-qp_name" -> "hrd_ready". This
// // requires that a qp_name never starts with RESERVED_NAME_PREFIX.
// //
// // This avoids overwriting the memcached entry for qp_name which might still
// // be needed by the remote peer.
// void MemoryStore::set_qp_ready(const char *qp_name) {
//   char value[QP_NAME_SIZE];
//   assert(qp_name != nullptr && strlen(qp_name) < QP_NAME_SIZE);

//   char new_name[2 * QP_NAME_SIZE];
//   sprintf(new_name, "%s", RESERVED_NAME_PREFIX);
//   strcat(new_name, qp_name);

//   sprintf(value, "%s", "hrd_ready");
//   set(new_name, value, strlen(value));
// }

// hrd_qp_attr_t *MemoryStore::get_qp(const char *qp_name) {
//   assert(strlen(qp_name) < QP_NAME_SIZE - 1);
//   assert(strstr(qp_name, RESERVED_NAME_PREFIX) == nullptr);

//   hrd_qp_attr_t *ret;
//   for (size_t i = 0; i < strlen(qp_name); i++) assert(qp_name[i] != ' ');

//   int ret_len = get(qp_name, reinterpret_cast<void **>(&ret));

//   // The registry lookup returns only if we get a unique QP for @qp_name, or
//   // if the memcached lookup succeeds but we don't have an entry for
//   @qp_name. rt_assert(ret_len == static_cast<int>(sizeof(*ret)) || ret_len ==
//   -1, "");

//   return ret;
// }

}  // namespace dory
