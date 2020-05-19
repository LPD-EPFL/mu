
#include <iostream>
#include <stdexcept>
#include <vector>
#include <chrono>
#include <thread>

#include <dory/crash-consensus.hpp>
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

  uint64_t commit_id = 0;
  dory::Consensus consensus(id, remote_ids);
  consensus.commitHandler([&commit_id, &id]([[maybe_unused]] bool leader, uint8_t* buf, size_t len) {
    if (len != sizeof(uint64_t)) {
      std::cout << "The committed value must be a uint64_t for this test"
                << std::endl;
    } else {
      uint64_t val = *reinterpret_cast<uint64_t*>(buf);
      uint64_t proposer_id = val >> 60;
      val &= 0xfffffffffffffffUL;

      (void)proposer_id;

      // std::cout << "CID: " << commit_id
      //           << ", Proposer: " << proposer_id
      //           << ", Val: " << val << "\n";
      commit_id += 1;
    }
  });

  // Wait enough time for the consensus to become ready
  std::cout << "Wait before starting to propose" << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(5));
  std::cout << "Started proposing" << std::endl;

  if (id == 1 || id == 2) {
    TIMESTAMP_INIT;

    const uint64_t times = 1024 * 1024;
    std::vector<TIMESTAMP_T> timestamps(times);

    for (uint64_t i = 0; i < times; i++) {
      GET_TIMESTAMP(timestamps[i]);
      // Encode process doing the proposal
      uint64_t encoded_i = i | (uint64_t(id) << 60);
      dory::ProposeError err;
      // std::cout << "Proposing " << i << std::endl;
      if ((err = consensus.propose(reinterpret_cast<uint8_t*>(&encoded_i),
                                    sizeof(encoded_i))) != dory::ProposeError::NoError) {
        // std::cout << "Proposal failed at index " << i << std::endl;
        i -= 1;
        switch (err) {
          case dory::ProposeError::FastPath:
          case dory::ProposeError::FastPathRecyclingTriggered:
          case dory::ProposeError::SlowPathCatchFUO:
          case dory::ProposeError::SlowPathUpdateFollowers:
          case dory::ProposeError::SlowPathCatchProposal:
          case dory::ProposeError::SlowPathUpdateProposal:
          case dory::ProposeError::SlowPathReadRemoteLogs:
          case dory::ProposeError::SlowPathWriteAdoptedValue:
          case dory::ProposeError::SlowPathWriteNewValue:
            std::cout << "Error: in leader mode. Code: " << static_cast<int>(err) << std::endl;
            break;

          case dory::ProposeError::SlowPathLogRecycled:
            std::cout << "Log recycled, waiting a bit..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(1));
            break;

          case dory::ProposeError::MutexUnavailable:
          case dory::ProposeError::FollowerMode:
            std::cout << "Error: in follower mode. Potential leader: " <<
            consensus.potentialLeader() << std::endl;
            break;

          default:
            std::cout << "Bug in code. You should only handle errors here"
                      << std::endl;
        }
      } else {
        // std::cout << "Proposed " << i << std::endl;

        if (i % 5 == 0) {
          std::cout << "Passed " << i << std::endl;
        }
      }

      // std::cout << std::endl;
      std::this_thread::sleep_for(std::chrono::seconds(1));
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

  while (true) {
    std::this_thread::sleep_for(std::chrono::seconds(60));
  }

  return 0;
}
