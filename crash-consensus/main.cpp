
#include <iostream>
#include <stdexcept>
#include <vector>

#include "consensus.hpp"
#include "pinning.hpp"
#include "timers.h"

int main(int argc, char* argv[]) {
  if (argc < 2) {
    throw std::runtime_error("Provide the id of the process as argument");
  }

  constexpr int nr_procs = 3;
  constexpr int minimum_id = 1;
  int id = 0;
  switch (argv[1][0]) {
    case '1':
      id = 1;
      break;
    case '2':
      id = 2;
      break;
    case '3':
      id = 3;
      break;
    case '4':
      id = 4;
      break;
    case '5':
      id = 5;
      break;
    default:
      throw std::runtime_error("Invalid id");
  }

  // Build the list of remote ids
  std::vector<int> remote_ids;
  for (int i = 0, min_id = minimum_id; i < nr_procs; i++, min_id++) {
    if (min_id == id) {
      continue;
    } else {
      remote_ids.push_back(min_id);
    }
  }

  std::thread proposer([&] {
    dory::RdmaConsensus consensus(id, remote_ids);

    // Wait enough time for the consensus to become ready
    std::this_thread::sleep_for(std::chrono::seconds(40));

    if (id == 3) {
      TIMESTAMP_INIT;

      const uint64_t times = 1024 * 1024;
      std::vector<TIMESTAMP_T> timestamps(times);

      for (uint64_t i = 0; i < times; i++) {
        GET_TIMESTAMP(timestamps[i]);
        if (!consensus.propose(reinterpret_cast<uint8_t*>(&i), sizeof(i))) {
          std::cout << "Proposal failed at index " << i << std::endl;
        }

        // std::this_thread::sleep_for(std::chrono::seconds(1));
      }

      std::cout << "Finished proposing, computing rtt for proposals..."
                << std::endl;
      // Adjacent difference
      std::vector<uint64_t> diffs;
      auto it = timestamps.begin();
      auto prev = *it++;
      for (; it < timestamps.end(); it++) {
        diffs.push_back(ELAPSED_NSEC(prev, (*it)));
        prev = *it;
      }

      for (auto n : diffs) {
        std::cout << n << std::endl;
      }
    }

    std::this_thread::sleep_for(std::chrono::hours(1));
  });

  auto pinned_cpu = 20;
  dory::pinThreadToCore(proposer, pinned_cpu);
  dory::setThreadName(proposer, "thd_proposer");
  std::cout << "Pinning the proposer thread on coreID " << pinned_cpu
            << std::endl;

  while (true) {
    std::cout << "Sleeping" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(60));
  }

  return 0;
}