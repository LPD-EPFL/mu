
#include <algorithm>
#include <array>
#include <atomic>
#include <sstream>
#include <vector>

#include <x86intrin.h> /* for clflush */

#include "failure-detector.hpp"

namespace dory {
FailureDetector::FailureDetector(int my_id, std::vector<int> remote_ids,
                                 ControlBlock &cb)
    : my_id{my_id},
      remote_ids{remote_ids},
      cb{cb},
      outstanding{0},
      heartbeat_started{false} {
  auto [valid, maximum_id] = valid_ids();
  if (!valid) {
    throw std::runtime_error(
        "Ids are not natural numbers/reasonably contiguous");
  }

  max_id = maximum_id;

  // for(auto const& id: remote_ids) {
  //   readings.insert(std::pair<int, uint64_t>(id, 0));
  // }

  status = std::vector<ReadingStatus>(max_id + 1);
}

FailureDetector::~FailureDetector() {
  if (heartbeat_started) {
    exit_signal.set_value();
    heartbeat.join();
  }
}

void FailureDetector::configure(std::string const &pd, std::string const &mr,
                                std::string send_cp_name,
                                std::string recv_cp_name) {
  // Configure the connections
  for (auto const &id : remote_ids) {
    rcs.insert(std::pair<int, dory::ReliableConnection>(
        id, dory::ReliableConnection(cb)));

    auto &rc = rcs.find(id)->second;
    rc.bindToPD(pd);
    rc.bindToMR(mr);
    rc.associateWithCQ(send_cp_name, recv_cp_name);
  }
}

void FailureDetector::announce(dory::MemoryStore &store) {
  // Announce the connections
  for (auto &[pid, rc] : rcs) {
    std::stringstream name;
    name << "qp-le-from-" << my_id << "-to-" << pid;
    auto infoForRemoteParty = rc.remoteInfo();
    store.set(name.str(), infoForRemoteParty.serialize());
  }
}

void FailureDetector::connect(dory::MemoryStore &store) {
  // Establish the connections
  for (auto &[pid, rc] : rcs) {
    std::stringstream name;
    name << "qp-le-from-" << pid << "-to-" << my_id;

    std::string ret_val;
    if (!store.get(name.str(), ret_val)) {
      std::cout << "Could not retrieve key " << name.str() << std::endl;
    }

    auto remoteRC = dory::RemoteConnection::fromStr(ret_val);

    rc.init(dory::ControlBlock::LOCAL_READ | dory::ControlBlock::LOCAL_WRITE |
            dory::ControlBlock::REMOTE_READ);
    rc.connect(remoteRC);
  }
}

void FailureDetector::heartbeatCounterStart(uint64_t *counter_address) {
  std::future<void> futureObj = exit_signal.get_future();
  heartbeat_started = true;

  // Spawn thread that increments the counter
  heartbeat =
      std::thread([counter_address, futureObj = std::move(futureObj)]() {
        // std::atomic<uint64_t> *counter = new(buf) std::atomic<uint64_t>;
        // volatile uint64_t* counter = reinterpret_cast<volatile
        // uint64_t*>(counter_address);
        uint64_t *counter = reinterpret_cast<uint64_t *>(counter_address);

        for (int i = 0;; i = (i + 1) % 1024) {
          *counter += 1;
          __sync_synchronize();
          // _mm_clflush(counter);
          std::this_thread::sleep_for(heartbeatRefreshRate);

          if (i % 1024 == 0) {
            if (futureObj.wait_for(std::chrono::seconds(0)) !=
                std::future_status::timeout) {
              break;
            }
          }
        }
      });
}

void *FailureDetector::allocateCountersOverlay(void *start_addr) {
  auto length = (max_id + 1);
  // alloc = OverlayAllocator<uint64_t>((uint64_t *)start_addr, length *
  // sizeof(uint64_t)); memset(alloc.allocate(1), 0, length * sizeof(uint64_t));

  // printf("Address for allocation: %p\n", alloc.allocate(1));

  // counters_overlay = std::vector <uint64_t,
  // OverlayAllocator<uint64_t>>(length, alloc);
  // counters_overlay.resize(length);

  // counters_overlay = p;

  counters_overlay = reinterpret_cast<uint64_t *>(start_addr);

  // Return the address after the allocated space
  return (void *)(reinterpret_cast<uintptr_t>(start_addr) +
                  length * sizeof(uint64_t));
}

void FailureDetector::detect(deleted_unique_ptr<struct ibv_cq> &cq) {
  uint64_t read_seq = 0;
  for (auto &[pid, rc] : rcs) {
    status[pid].loop_modulo =
        (status[pid].loop_modulo + 1) % fail_retry_interval;

    if (status[pid].failed_attempts > failed_attempt_limit) {
      // If it has zero updates, only schedule during the module operation
      if (status[pid].loop_modulo != 1) {
        continue;
      }
    }

    // TODO: Replace rc.remoteBuf() with something better
    auto read_to = &counters_overlay[pid];
    auto post_ret = rc.postSendSingle(ReliableConnection::RdmaRead,
                                      uint64_t(pid) << 48 | read_seq, read_to,
                                      sizeof(uint64_t), rc.remoteBuf());

    if (!post_ret) {
      std::cout << "Post returned " << post_ret << std::endl;
    } else {
      outstanding += 1;
    }
  }

  read_seq += 1;

  // If the number of outstanding requests goes out of hand, go slower
  do {
    entries.resize(outstanding);
    // std::vector<struct ibv_wc> entries(outstanding);
    // std::cout << "I have " << outstanding << " requests to be polled, max_id
    // " << max_id << std::endl;

    if (cb.pollCqIsOK(cq, entries)) {
      // std::cout << "Polled " << entries.size() << " entries" << std::endl;

      outstanding -= entries.size();

      for (auto const &entry : entries) {
        int pid = entry.wr_id >> 48;
        uint64_t seq = (entry.wr_id << 16) >> 16;
        (void)seq;
        volatile uint64_t *val =
            reinterpret_cast<uint64_t *>(&counters_overlay[pid]);

        if (status[pid].value < *val) {
          status[pid].consecutive_updates =
              std::min(status[pid].consecutive_updates + 2, history_length);
          status[pid].failed_attempts = 0;
        }

        status[pid].value = *val;

        // std::cout << "Received (pid, seq) = (" << pid << ", " << seq << "),
        // value = " << *val << std::endl;
      }

      // Reduce everything by one. The slow processes with eventually go to
      // zero.
      for (auto &[pid, rc] : rcs) {
        (void)rc;
        status[pid].consecutive_updates =
            std::max(status[pid].consecutive_updates - 1, 0);
        if (status[pid].consecutive_updates == 0) {
          status[pid].failed_attempts += 1;
        }
      }
    }
  } while (outstanding > outstanding_multiplier * max_id);
}

int FailureDetector::leaderPID() {
  auto min_max_remote =
      std::minmax_element(remote_ids.begin(), remote_ids.end());
  auto min = std::min(*min_max_remote.first, my_id);
  (void)min;
  // if (my_id == min) {
  //   return my_id;
  // }

  int leader_id = my_id;

  for (auto &pid : remote_ids) {
    // std::cout << pid << " " << status[pid].consecutive_updates << std::endl;
    if (status[pid].consecutive_updates > 2) {
      leader_id = pid;
      break;
    }
  }

  return leader_id > my_id ? my_id : leader_id;
}

std::pair<bool, int> FailureDetector::valid_ids() const {
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
