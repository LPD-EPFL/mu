#pragma once

#include <chrono>
#include <cstddef>
#include <future>
#include <map>
#include <memory>
#include <thread>
#include <vector>

#include <dory/ctrl/block.hpp>
#include <dory/shared/logger.hpp>
#include <dory/store.hpp>
#include "rc.hpp"

namespace dory {
class ConnectionExchanger {
 private:
  static constexpr double gapFactor = 2;

 public:
  ConnectionExchanger(int my_id, std::vector<int> remote_ids, ControlBlock& cb);

  void configure(int proc_id, std::string const& pd, std::string const& mr,
                 std::string send_cp_name, std::string recv_cp_name);

  void configure_all(std::string const& pd, std::string const& mr,
                     std::string send_cp_name, std::string recv_cp_name);

  void announce(int proc_id, MemoryStore& store, std::string const& prefix);

  void announce_all(MemoryStore& store, std::string const& prefix);

  void connect(int proc_id, MemoryStore& store, std::string const& prefix,
               ControlBlock::MemoryRights rights = ControlBlock::LOCAL_READ);

  void connect_all(
      MemoryStore& store, std::string const& prefix,
      ControlBlock::MemoryRights rights = ControlBlock::LOCAL_READ);

  std::map<int, dory::ReliableConnection>& connections() { return rcs; }

 private:
  std::pair<bool, int> valid_ids() const;

 private:
  int my_id;
  std::vector<int> remote_ids;
  ControlBlock& cb;
  int max_id;
  std::map<int, dory::ReliableConnection> rcs;
  std::shared_ptr<spdlog::logger> logger;
};
}  // namespace dory
