#include "exchanger.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <sstream>

namespace dory {
ConnectionExchanger::ConnectionExchanger(int my_id, std::vector<int> remote_ids,
                                         ControlBlock& cb)
    : my_id{my_id}, remote_ids{remote_ids}, cb{cb} {
  auto [valid, maximum_id] = valid_ids();
  if (!valid) {
    throw std::runtime_error(
        "Ids are not natural numbers/reasonably contiguous");
  }

  max_id = maximum_id;
}

void ConnectionExchanger::configure(std::string const& pd,
                                    std::string const& mr,
                                    std::string send_cp_name,
                                    std::string recv_cp_name) {
  // Configure the connections
  for (auto const& id : remote_ids) {
    rcs.insert(std::pair<int, dory::ReliableConnection>(
        id, dory::ReliableConnection(cb)));

    auto& rc = rcs.find(id)->second;
    rc.bindToPD(pd);
    rc.bindToMR(mr);
    rc.associateWithCQ(send_cp_name, recv_cp_name);
  }
}

void ConnectionExchanger::announce(MemoryStore& store,
                                   std::string const& prefix) {
  // Announce the connections
  for (auto& [pid, rc] : rcs) {
    std::stringstream name;
    name << prefix << "-" << my_id << "-to-" << pid;
    auto infoForRemoteParty = rc.remoteInfo();
    store.set(name.str(), infoForRemoteParty.serialize());
  }
}

void ConnectionExchanger::connect(MemoryStore& store, std::string const& prefix,
                                  ControlBlock::MemoryRights rights) {
  // Establish the connections
  for (auto& [pid, rc] : rcs) {
    std::stringstream name;
    name << prefix << "-" << pid << "-to-" << my_id;

    std::string ret_val;
    if (!store.get(name.str(), ret_val)) {
      std::cout << "Could not retrieve key " << name.str() << std::endl;
    }

    auto remoteRC = dory::RemoteConnection::fromStr(ret_val);

    rc.init(rights);
    rc.connect(remoteRC);
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

  if (double(max) > gapFactor * (remote_ids.size() + 1)) {
    return std::make_pair(false, 0);
  }

  return std::make_pair(true, max);
}
}  // namespace dory
