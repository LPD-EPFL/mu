#include <algorithm>
#include <array>
#include <atomic>
#include <sstream>

#include "exchanger.hpp"

namespace dory {
ConnectionExchanger::ConnectionExchanger(int my_id, std::vector<int> remote_ids,
                                         ControlBlock& cb)
    : my_id{my_id}, remote_ids{remote_ids}, cb{cb}, LOGGER_INIT(logger, "CE") {
  auto [valid, maximum_id] = valid_ids();
  if (!valid) {
    throw std::runtime_error(
        "Ids are not natural numbers/reasonably contiguous");
  }

  max_id = maximum_id;
}

void ConnectionExchanger::configure(int proc_id, std::string const& pd,
                                    std::string const& mr,
                                    std::string send_cq_name,
                                    std::string recv_cq_name) {
  rcs.insert(
      std::pair<int, ReliableConnection>(proc_id, ReliableConnection(cb)));

  auto& rc = rcs.find(proc_id)->second;

  rc.bindToPD(pd);
  rc.bindToMR(mr);
  rc.associateWithCQ(send_cq_name, recv_cq_name);
}

void ConnectionExchanger::configure_all(std::string const& pd,
                                        std::string const& mr,
                                        std::string send_cq_name,
                                        std::string recv_cq_name) {
  for (auto const& id : remote_ids) {
    configure(id, pd, mr, send_cq_name, recv_cq_name);
  }
}

void ConnectionExchanger::addLoopback(std::string const& pd,
                                      std::string const& mr,
                                      std::string send_cq_name,
                                      std::string recv_cq_name) {
  loopback_ = std::make_unique<ReliableConnection>(cb);
  loopback_->bindToPD(pd);
  loopback_->bindToMR(mr);
  loopback_->associateWithCQ(send_cq_name, recv_cq_name);

  LOGGER_INFO(logger, "Loopback connection was added");
}

void ConnectionExchanger::connectLoopback(ControlBlock::MemoryRights rights) {
  auto infoForRemoteParty = loopback_->remoteInfo();
  loopback_->init(rights);
  loopback_->connect(infoForRemoteParty);

  LOGGER_INFO(logger, "Loopback connection was established");
}

void ConnectionExchanger::announce(int proc_id, MemoryStore& store,
                                   std::string const& prefix) {
  auto& rc = rcs.find(proc_id)->second;

  std::stringstream name;
  name << prefix << "-" << my_id << "-for-" << proc_id;
  auto infoForRemoteParty = rc.remoteInfo();
  store.set(name.str(), infoForRemoteParty.serialize());
  LOGGER_INFO(logger, "Publishing qp {}", name.str());
}

void ConnectionExchanger::announce_all(MemoryStore& store,
                                       std::string const& prefix) {
  for (int pid : remote_ids) {
    announce(pid, store, prefix);
  }
}

void ConnectionExchanger::announce_ready(MemoryStore& store,
                                         std::string const& prefix,
                                         std::string const& reason) {
  std::stringstream name;
  name << prefix << "-" << my_id << "-ready(" << reason << ")";
  store.set(name.str(), "ready(" + reason + ")");
}

void ConnectionExchanger::wait_ready(int proc_id, MemoryStore& store,
                                     std::string const& prefix,
                                     std::string const& reason) {
  auto packed_reason = "ready(" + reason + ")";
  std::stringstream name;
  name << prefix << "-" << proc_id << "-" << packed_reason;

  auto key = name.str();
  std::string value;

  while (!store.get(key, value)) {
    std::this_thread::sleep_for(retryTime);
  }

  if (value != packed_reason) {
    throw std::runtime_error("Ready announcement of message `" + key +
                             "` does not contain the value `" + packed_reason +
                             "`");
  }
}

void ConnectionExchanger::wait_ready_all(MemoryStore& store,
                                         std::string const& prefix,
                                         std::string const& reason) {
  for (int pid : remote_ids) {
    wait_ready(pid, store, prefix, reason);
  }
}

void ConnectionExchanger::connect(int proc_id, MemoryStore& store,
                                  std::string const& prefix,
                                  ControlBlock::MemoryRights rights) {
  auto& rc = rcs.find(proc_id)->second;

  std::stringstream name;
  name << prefix << "-" << proc_id << "-for-" << my_id;

  std::string ret_val;
  if (!store.get(name.str(), ret_val)) {
    LOGGER_DEBUG(logger, "Could not retrieve key {}", name.str());

    throw std::runtime_error("Cannot connect to remote qp " + name.str());
  }

  auto remoteRC = dory::RemoteConnection::fromStr(ret_val);

  rc.init(rights);
  rc.connect(remoteRC);
  LOGGER_INFO(logger, "Connected with {}", name.str());
}

void ConnectionExchanger::connect_all(MemoryStore& store,
                                      std::string const& prefix,
                                      ControlBlock::MemoryRights rights) {
  for (int pid : remote_ids) {
    connect(pid, store, prefix, rights);
  }
}

std::pair<bool, int> ConnectionExchanger::valid_ids() const {
  auto min_max_remote =
      std::minmax_element(remote_ids.begin(), remote_ids.end());
  auto min = std::min(*min_max_remote.first, my_id);
  auto max = std::max(*min_max_remote.second, my_id);

  if (min < 1) {
    return std::make_pair(false, 0);
  }

  if (double(max) > gapFactor * static_cast<double>((remote_ids.size() + 1))) {
    return std::make_pair(false, 0);
  }

  return std::make_pair(true, max);
}
}  // namespace dory
