#pragma once

#include <functional>
#include <map>
#include <optional>
#include <shared_mutex>
#include <vector>

#include <dory/conn/exchanger.hpp>
#include <dory/conn/rc.hpp>
#include <dory/crypto/sign.hpp>
#include <dory/shared/logger.hpp>

namespace dory {
namespace neb {

template <typename T>
using optional_ref = std::optional<std::reference_wrapper<T>>;

template <typename T>
class ShareLockedRef {
 public:
  ShareLockedRef(const T &v, std::shared_mutex &mux) : v(v), lock(mux) {}

  const T &get() { return v; }

 private:
  const T &v;
  std::shared_lock<std::shared_mutex> lock;
};

class RemoteProcesses {
 public:
  RemoteProcesses(std::vector<int> remote_ids) : remote_ids{remote_ids} {}

  void set_connections(ConnectionExchanger &bcast_ce,
                       ConnectionExchanger &replay_ce) {
    bcast_conn.merge(bcast_ce.connections());
    replay_conn.merge(replay_ce.connections());
  }

  void set_keys(std::map<int, dory::crypto::pub_key> &keys) {
    remote_keys.merge(keys);
  }

  void remote_remote(int pid) {
    std::unique_lock lock(mux);

    auto rit = bcast_conn.find(pid);
    if (rit != bcast_conn.end()) {
      bcast_conn.erase(rit);
    }

    auto bit = replay_conn.find(pid);
    if (bit != replay_conn.end()) {
      replay_conn.erase(bit);
    }

    auto it = std::find(remote_ids.begin(), remote_ids.end(), pid);
    if (it != remote_ids.end()) {
      remote_ids.erase(it);
    }
  }

  size_t size() {
    std::shared_lock lock(mux);

    return remote_ids.size();
  }

  size_t replay_quorum_size() { return size() - 1; }

  dory::crypto::pub_key &key(int pid) { return remote_keys[pid]; }

  optional_ref<ReliableConnection> broadcast_conneciton(int pid) {
    std::shared_lock lock(mux);

    auto it = bcast_conn.find(pid);

    if (it == bcast_conn.end()) {
      return std::nullopt;
    }

    return optional_ref<ReliableConnection>(it->second);
  }

  optional_ref<ReliableConnection> replay_connection(int pid) {
    std::shared_lock lock(mux);

    auto it = replay_conn.find(pid);

    if (it == replay_conn.end()) {
      return std::nullopt;
    }

    return optional_ref<ReliableConnection>(it->second);
  }

  ShareLockedRef<std::vector<int>> ids() {
    return ShareLockedRef(remote_ids, mux);
  }

  ShareLockedRef<std::map<int, ReliableConnection>> replay_connections() {
    return ShareLockedRef(replay_conn, mux);
  }

  ShareLockedRef<std::map<int, ReliableConnection>> broadcast_connections() {
    return ShareLockedRef(bcast_conn, mux);
  }

 private:
  std::vector<int> remote_ids;

  // broadcast reliable connections
  std::map<int, ReliableConnection> bcast_conn;

  // replay reliable connections
  std::map<int, ReliableConnection> replay_conn;

  std::map<int, dory::crypto::pub_key> remote_keys;

  std::shared_mutex mux;
};
}  // namespace neb
}  // namespace dory
