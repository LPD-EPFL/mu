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
#include "memory.hpp"
#include "pinning.hpp"
#include "response-tracker.hpp"
#include "slow-path.hpp"

#include <random>  // TODO: Remove if leader-switch is finished
#include "leader-switch.hpp"
#include "follower.hpp"
#include "readerwriterqueue.h"


namespace dory {
class RdmaConsensus {
 public:
  RdmaConsensus(int my_id, std::vector<int> &remote_ids);
  ~RdmaConsensus();

  bool propose(uint8_t *buf, size_t len);

 private:
  void run();

 private:
  int my_id;
  std::vector<int> remote_ids;

  size_t allocated_size;
  size_t alignment;

  std::thread consensus_thd;
  std::thread permissions_thd;

  std::atomic<bool> am_I_leader;

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
  std::unique_ptr<SequentialQuorumWaiter> sqw;
  std::unique_ptr<FixedSizeMajorityOperation<SequentialQuorumWaiter, WriteLogMajorityError>> majW;

  std::vector<uintptr_t> to_remote_memory, dest;
  BlockingIterator iter;
  LiveIterator commit_iter;

  Follower follower;


  // Used by consensus
  bool encountered_error = false;
  bool became_leader = true;
  bool fast_path = false;
  uint64_t proposal_nr = 0;
};
}  // namespace dory