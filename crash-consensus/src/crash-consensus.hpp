#pragma once

#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace dory {
class RdmaConsensus;

enum class ProposeError {
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

enum class ThreadBank { A, B };

class Consensus {
 public:
  Consensus(int my_id, std::vector<int> &remote_ids, int outstanding_req = 0,
            ThreadBank threadBank = ThreadBank::A);
  ~Consensus();

  void commitHandler(
      std::function<void(bool leader, uint8_t *buf, size_t len)> committer);

  ProposeError propose(uint8_t *buf, size_t len);
  int potentialLeader();
  bool blockedResponse();
  std::pair<uint64_t, uint64_t> proposedReplicatedRange();

 private:
  std::unique_ptr<RdmaConsensus> impl;
};
}  // namespace dory
