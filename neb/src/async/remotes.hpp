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

  size_t size() { return remote_ids.size(); }

  size_t replay_quorum_size() { return size() - 1; }

  dory::crypto::dalek::pub_key &key(int pid) { return remote_keys[pid]; }

  optional_ref<ReliableConnection> broadcast_connection(int pid) {
    auto it = bcast_conn.find(pid);

    if (it == bcast_conn.end()) {
      return std::nullopt;
    }

    return optional_ref<ReliableConnection>(it->second);
  }

  optional_ref<ReliableConnection> replay_connection(int pid) {
    auto it = replay_conn.find(pid);

    if (it == replay_conn.end()) {
      return std::nullopt;
    }

    return optional_ref<ReliableConnection>(it->second);
  }

  std::mutex &get_bcast_mux(int pid) {
    std::unique_lock lock(bcast_map_mux);
    return *(bcast_muxes.try_emplace(pid, std::make_unique<std::mutex>())
                 .first->second);
  }

  std::mutex &get_replay_mux(int pid) {
    std::unique_lock lock(replay_map_mux);
    return *(replay_muxes.try_emplace(pid, std::make_unique<std::mutex>())
                 .first->second);
  }

  std::vector<int> const &ids() const { return remote_ids; }

  std::map<int, ReliableConnection> const &replay_connections() const {
    return replay_conn;
  }

  std::map<int, ReliableConnection> const &broadcast_connections() const {
    return bcast_conn;
  }

 private:
  std::vector<int> remote_ids;

  // broadcast reliable connections
  std::map<int, ReliableConnection> bcast_conn;
  std::unordered_map<int, std::unique_ptr<std::mutex>> bcast_muxes;
  std::mutex bcast_map_mux;
  // replay reliable connections
  std::map<int, ReliableConnection> replay_conn;
  std::unordered_map<int, std::unique_ptr<std::mutex>> replay_muxes;
  std::mutex replay_map_mux;

  std::map<int, dory::crypto::dalek::pub_key> remote_keys;
};
}  // namespace neb
}  // namespace dory
