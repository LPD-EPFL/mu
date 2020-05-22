
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <chrono>
#include <thread>

#include <dory/crash-consensus.hpp>
#include "timers.h"

void mkrndstr_ipa(int length, uint8_t *randomString) {
    static uint8_t charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

    if (length) {
        if (randomString) {
            int l = (int) (sizeof(charset) - 1);
            for (int n = 0; n < length; n++) {
                int key = rand() % l;
                randomString[n] = charset[key];
            }

            randomString[length] = '\0';
        }
    }
}

int main(int argc, char* argv[]) {
  if (argc < 3) {
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
    default:
      throw std::runtime_error("Invalid id");
  }

  int payload_size = atoi(argv[2]);
  std::vector<uint8_t> payload_buffer(payload_size + 2);
  uint8_t *payload = &payload_buffer[0];

  std::cout << "USING PAYLOAD SIZE = " << payload_size << std::endl;

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
  consensus.commitHandler([&commit_id]([[maybe_unused]] bool leader, [[maybe_unused]]uint8_t* buf, [[maybe_unused]]size_t len) {
    // if (len != sizeof(uint64_t)) {
    //   std::cout << "The committed value must be a uint64_t for this test"
    //             << std::endl;
    // } else {
    //   uint64_t val = *reinterpret_cast<uint64_t*>(buf);
    //   uint64_t proposer_id = val >> 60;
    //   val &= 0xfffffffffffffffUL;

    //   (void)proposer_id;

    //   // std::cout << "CID: " << commit_id
    //   //           << ", Proposer: " << proposer_id
    //   //           << ", Val: " << val << "\n";
    //   commit_id += 1;
    // }
  });

  // Wait enough time for the consensus to become ready
  std::cout << "Wait before starting to propose" << std::endl;
  // Give some slack to id 1 to become leader
  std::this_thread::sleep_for(std::chrono::seconds(5 + 3 * id));
  std::cout << "Started" << std::endl;

  if (id == 1) {
    TIMESTAMP_INIT;

    const uint64_t times = 1000000;
    std::vector<TIMESTAMP_T> timestamps_start(times);
    std::vector<TIMESTAMP_T> timestamps_end(times);



    for (uint64_t i = 0; i < times; i++) {
      mkrndstr_ipa(payload_size, payload);

      GET_TIMESTAMP(timestamps_start[i]);
      // Encode process doing the proposal
      dory::ProposeError err;
      // std::cout << "Proposing " << i << std::endl;
      if ((err = consensus.propose(payload, payload_size)) != dory::ProposeError::NoError) {
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

        // if (i % 5 == 0) {
        //   std::cout << "Passed " << i << std::endl;
        // }
      }

      GET_TIMESTAMP(timestamps_end[i]);

      auto [start, end] = consensus.proposedReplicatedRange();
      std::cout << "proposedReplicatedRange: [" << start << ", " << end << ")" << std::endl;

      // std::cout << std::endl;
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << "Finished proposing, computing rtt for proposals..."
              << std::endl;

    std::ofstream dump;
    dump.open ("dump.txt");

    for (size_t i = 0; i < timestamps_start.size(); i++) {
      dump << ELAPSED_NSEC(timestamps_start[i], timestamps_end[i]) << "\n";
    }

    dump.close();
  }

  std::cout << "Done" << std::endl;

  while (true) {
    std::this_thread::sleep_for(std::chrono::seconds(60));
  }

  return 0;
}
