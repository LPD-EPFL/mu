
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <chrono>
#include <thread>

#include <dory/crash-consensus.hpp>
#include "timers.h"

#include <cassert>
#include <condition_variable>

class Barrier
{

public:

    Barrier(std::size_t nb_threads)
        : m_mutex(),
        m_condition(),
        m_nb_threads(nb_threads)
    {
        assert(0u != m_nb_threads);
    }

    Barrier(const Barrier& barrier) = delete;

    Barrier(Barrier&& barrier) = delete;

    ~Barrier() noexcept
    {
        assert(0u == m_nb_threads);
    }

    Barrier& operator=(const Barrier& barrier) = delete;

    Barrier& operator=(Barrier&& barrier) = delete;

    void Wait()
    {
        std::unique_lock< std::mutex > lock(m_mutex);

        assert(0u != m_nb_threads);

        if (0u == --m_nb_threads)
        {
            m_condition.notify_all();
        }
        else
        {
            m_condition.wait(lock, [this]() { return 0u == m_nb_threads; });
        }
    }

private:

    std::mutex m_mutex;

    std::condition_variable m_condition;

    std::size_t m_nb_threads;
};

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

void benchmark(int id, std::vector<int> remote_ids, int times, int payload_size, int outstanding_req, Barrier &barrier, dory::ThreadBank threadBank) {
  dory::Consensus consensus(id, remote_ids, outstanding_req, threadBank);
  consensus.commitHandler([]([[maybe_unused]] bool leader, [[maybe_unused]]uint8_t* buf, [[maybe_unused]]size_t len) {});

  // Wait enough time for the consensus to become ready
  std::cout << "Wait some time" << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(5 + 3 - id));

  if (id != 1) {
    barrier.Wait();
  }

  if (id == 1) {
    TIMESTAMP_INIT;

    std::vector<uint8_t> payload_buffer(payload_size + 2);
    uint8_t *payload = &payload_buffer[0];

    std::vector<TIMESTAMP_T> timestamps_start(times);
    std::vector<TIMESTAMP_T> timestamps_end(times);
    std::vector<std::pair<int, TIMESTAMP_T>> timestamps_ranges(times);
    TIMESTAMP_T loop_time;

    mkrndstr_ipa(payload_size, payload);
    consensus.propose(payload, payload_size);

    int offset = 2;

    std::vector<std::vector<uint8_t>> payloads(8192);
    for (size_t i = 0; i < payloads.size(); i++) {
      payloads[i].resize(payload_size);
      mkrndstr_ipa(payload_size, &(payloads[i][0]));
    }

    barrier.Wait();
    std::cout << "Started" << std::endl;

    TIMESTAMP_T start_meas, end_meas;

    GET_TIMESTAMP(start_meas);
    for (int i = 0; i < times; i++) {

      // GET_TIMESTAMP(timestamps_start[i]);
      // Encode process doing the proposal
      dory::ProposeError err;
      // std::cout << "Proposing " << i << std::endl;
      if ((err = consensus.propose(&(payloads[i % 8192][0]), payload_size)) != dory::ProposeError::NoError) {
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
      }
    }
    GET_TIMESTAMP(end_meas);
    std::this_thread::sleep_for(std::chrono::seconds(8));

    std::cout << "Received " << times << " in " << ELAPSED_NSEC(start_meas, end_meas) << " ns" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(8));

    exit(0);
  }
}

int main(int argc, char* argv[]) {
  if (argc < 4) {
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
  std::cout << "USING PAYLOAD SIZE = " << payload_size << std::endl;

  int outstanding_req = atoi(argv[3]);
  std::cout << "USING OUTSTANDING_REQ = " << outstanding_req << std::endl;

  // Build the list of remote ids
  std::vector<int> remote_ids;
  for (int i = 0, min_id = minimum_id; i < nr_procs; i++, min_id++) {
    if (min_id == id) {
      continue;
    } else {
      remote_ids.push_back(min_id);
    }
  }

  Barrier barrier(2);

  const uint64_t times = int(1.5 * 1024.0 * 1024.0 * 1024.0 / (payload_size + 64));

  std::thread t1([id, remote_ids, times, payload_size, outstanding_req, &barrier]{
    benchmark(id, remote_ids, times, payload_size, outstanding_req, barrier, dory::ThreadBank::B);
  });

  cpu_set_t cpuset;
  int rc;
  CPU_ZERO(&cpuset);
  CPU_SET(10, &cpuset);
  rc = pthread_setaffinity_np(t1.native_handle(), sizeof(cpu_set_t), &cpuset);
  std::cout << "Thread pinning " << rc;
  std::this_thread::sleep_for(std::chrono::seconds(10));


  // barrier.Wait();

  // std::thread t2([id, remote_ids, times, payload_size, outstanding_req, &barrier]{
  //   benchmark(id, remote_ids, times, payload_size, outstanding_req, barrier, dory::ThreadBank::B);
  // });

  benchmark(id, remote_ids, times, payload_size, outstanding_req, barrier, dory::ThreadBank::A);

  // CPU_ZERO(&cpuset);
  // CPU_SET(16, &cpuset);
  // rc = pthread_setaffinity_np(t2.native_handle(), sizeof(cpu_set_t), &cpuset);
  // std::cout << "Thread pinning " << rc;

  t1.join();
  // t2.join();

  while (true) {
    std::this_thread::sleep_for(std::chrono::seconds(60));
  }


  return 0;
}
