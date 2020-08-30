#pragma once

// #include <ios>
// #include <iostream>

// #include <thread>
// #include <random>
// #include <chrono>

// #include <array>
// #include <atomic>
// #include <map>
// #include <sstream>

// #include <dory/conn/exchanger.hpp>
// #include <dory/conn/rc.hpp>
// #include <dory/ctrl/block.hpp>
// #include <dory/ctrl/device.hpp>
// #include <dory/shared/unused-suppressor.hpp>
// #include <dory/store.hpp>

// #include <algorithm>
// #include <functional>

// #include <dory/shared/units.hpp>

// #include "log.hpp"
// #include "response-tracker.hpp"
// #include "memory.hpp"

// #include "slow-path.hpp"
// #include "leader-switch.hpp"
// #include "timers.h"
// #include "branching.hpp"
// #include "pinning.hpp"
// #include "config.hpp"
// // #include "readerwriterqueue.h"

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include <dory/conn/exchanger.hpp>
#include <dory/conn/rc.hpp>
#include <dory/ctrl/block.hpp>
#include <dory/ctrl/device.hpp>
#include <dory/shared/units.hpp>
#include <dory/shared/unused-suppressor.hpp>
#include <dory/store.hpp>

#include "branching.hpp"
#include "config.hpp"
#include "log.hpp"
#include "logger.hpp"
#include "memory.hpp"
#include "pinning.hpp"
#include "response-tracker.hpp"
#include "slow-path.hpp"

#include <random>  // TODO: Remove if leader-switch is finished
#include "follower.hpp"
#include "leader-switch.hpp"
#include "log-recycling.hpp"
#include "readerwriterqueue.h"

namespace dory {
class RdmaConsensus {
 public:
  RdmaConsensus(int my_id, std::vector<int> &remote_ids, int outstanding_req = 0, ConsensusConfig::ThreadConfig threadConfig = ConsensusConfig::ThreadConfig());
  ~RdmaConsensus();

  template <typename Func>
  void commitHandler(Func f) {
    commit = std::move(f);
    follower.commitHandler(commit);
    spawn_follower();
  }

  int propose(uint8_t *buf, size_t len);

  inline int potentialLeader() { return potential_leader; }

  inline std::pair<uint64_t, uint64_t> proposedReplicatedRange() {
    return std::make_pair(majW->range_start, majW->range_end);
  }

  enum ProposeError {
    NoError = 0,  // Placeholder for the 0 value
    MutexUnavailable,
    FastPath,
    FastPathRecyclingTriggered,
    SlowPathCatchFUO,
    SlowPathUpdateFollowers,
    SlowPathCatchProposal,
    SlowPathUpdateProposal,
    SlowPathReadRemoteLogs,
    SlowPathWriteAdoptedValue,
    SlowPathWriteNewValue,
    FollowerMode,
    SlowPathLogRecycled
  };

 private:
  void spawn_follower();
  void run();

  inline int ret_error(std::unique_lock<std::mutex> &lock, ProposeError error,
                       bool ask_connection_reset = false) {
    became_leader = true;

    if (ask_connection_reset) {
      ask_reset.store(true);
      lock.unlock();

      while (ask_reset.load()) {
        ;
      }
    }

    return static_cast<int>(error);
  }

  inline int ret_no_error() { return 0; }

 public:
  std::thread handover_thd;
  std::atomic<bool> handover;
  uint8_t *handover_buf;
  size_t handover_buf_len;
  int handover_ret;

 private:
  int my_id;
  std::vector<int> remote_ids;

  size_t allocated_size;
  int alignment;

  std::thread consensus_thd;
  std::thread permissions_thd;

  std::atomic<bool> am_I_leader;

  std::function<void(bool, uint8_t *, size_t)> commit;

  Devices d;
  OpenDevice od;
  std::unique_ptr<ResolvedPort> rp;
  std::unique_ptr<ControlBlock> cb;
  std::unique_ptr<ConnectionExchanger> ce_replication;
  std::unique_ptr<ConnectionExchanger> ce_leader_election;
  std::unique_ptr<OverlayAllocator> overlay;
  std::unique_ptr<ScratchpadMemory> scratchpad;
  std::unique_ptr<Log> replication_log;
  std::unique_ptr<ConnectionContext> le_conn_ctx;
  std::unique_ptr<ConnectionContext> re_conn_ctx;
  std::unique_ptr<ReplicationContext> re_ctx;
  std::unique_ptr<LeaderElection> leader_election;
  std::unique_ptr<CatchUpWithFollowers> catchup;
  std::unique_ptr<LogSlotReader> lsr;
  std::unique_ptr<LogRecycling> log_recycling;
  std::unique_ptr<SequentialQuorumWaiter> sqw;
  std::unique_ptr<
      FixedSizeMajorityOperation<SequentialQuorumWaiter, WriteLogMajorityError>>
      majW;

  std::vector<uintptr_t> to_remote_memory, dest;
  BlockingIterator iter;
  LiveIterator commit_iter;

  Follower follower;

  // Used by consensus
  bool became_leader = true;
  bool fast_path = false;
  uint64_t proposal_nr = 0;
  int potential_leader = -1;

  std::atomic<bool> ask_reset;
  int outstanding_req;

public:
  ConsensusConfig::ThreadConfig threadConfig;

private:
  MemoryStore store;
  LOGGER_DECL(logger);
};
}  // namespace dory
