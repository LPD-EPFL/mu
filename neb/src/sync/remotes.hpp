#pragma once

#include <functional>
#include <map>
#include <vector>

#include <dory/conn/exchanger.hpp>
#include <dory/conn/rc.hpp>
#include <dory/crypto/sign/dalek.hpp>
#include <dory/shared/logger.hpp>

#include "../shared/types.hpp"

namespace dory {
namespace neb {

class RemoteProcesses {
 public:
  RemoteProcesses(std::vector<int> process_ids, int self_id) {
    for (auto pid : process_ids) {
      if (pid != self_id) remote_ids.push_back(pid);
    }
  }

  void set_connections(ConnectionExchanger &bcast_ce,
                       ConnectionExchanger &replay_ce) {
    bcast_conn.merge(bcast_ce.connections());
    replay_conn.merge(replay_ce.connections());
  }

  void set_keys(std::map<int, dory::crypto::dalek::pub_key> &keys) {
    remote_keys.merge(keys);
  }

  void remove_remote(int pid) {
    {
      std::unique_lock lock(bcast_mux);

      auto rit = bcast_conn.find(pid);
      if (rit != bcast_conn.end()) {
        bcast_conn.erase(rit);
      }
    }

    {
      std::unique_lock lock(replay_mux);

      auto bit = replay_conn.find(pid);
      if (bit != replay_conn.end()) {
        replay_conn.erase(bit);
      }
    }

    {
      std::unique_lock lock(ids_mux);

      auto it = std::find(remote_ids.begin(), remote_ids.end(), pid);
      if (it != remote_ids.end()) {
        remote_ids.erase(it);
      }
    }
  }

  size_t size() {
    std::shared_lock lock(ids_mux);

    return remote_ids.size();
  }

  size_t replay_quorum_size() { return size() - 1; }

  dory::crypto::dalek::pub_key &key(int pid) { return remote_keys[pid]; }

  optional_ref<ReliableConnection> broadcast_connection(int pid) {
    std::shared_lock lock(bcast_mux);

    auto it = bcast_conn.find(pid);

    if (it == bcast_conn.end()) {
      return std::nullopt;
    }

    return optional_ref<ReliableConnection>(it->second);
  }

  optional_ref<ReliableConnection> replay_connection(int pid) {
    std::shared_lock lock(replay_mux);

    auto it = replay_conn.find(pid);

    if (it == replay_conn.end()) {
      return std::nullopt;
    }

    return optional_ref<ReliableConnection>(it->second);
  }

  int id(size_t idx) {
    std::shared_lock lock(ids_mux);

    try {
      return remote_ids.at(idx);
    } catch (...) {
      return -1;
    }
  }

  ShareLockedRef<std::vector<int>> ids() {
    return ShareLockedRef(remote_ids, ids_mux);
  }

  ShareLockedRef<std::map<int, ReliableConnection>> replay_connections() {
    return ShareLockedRef(replay_conn, replay_mux);
  }

  ShareLockedRef<std::map<int, ReliableConnection>> broadcast_connections() {
    return ShareLockedRef(bcast_conn, bcast_mux);
  }

 private:
  std::vector<int> remote_ids;
  std::shared_mutex ids_mux;

  // broadcast reliable connections
  std::map<int, ReliableConnection> bcast_conn;
  std::shared_mutex bcast_mux;
  // replay reliable connections
  std::map<int, ReliableConnection> replay_conn;
  std::shared_mutex replay_mux;

  std::map<int, dory::crypto::dalek::pub_key> remote_keys;
};
}  // namespace neb
}  // namespace dory
