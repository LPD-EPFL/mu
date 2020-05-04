#pragma once

#include <memory>
#include <vector>
#include <functional>

namespace dory {
class RdmaConsensus;

enum class ProposeError {
  NoError = 0,  // Placeholder for the 0 value
  MutexUnavailable,
  FastPath,
  SlowPathCatchFUO,
  SlowPathUpdateFollowers,
  SlowPathCatchProposal,
  SlowPathUpdateProposal,
  SlowPathReadRemoteLogs,
  SlowPathWriteAdoptedValue,
  SlowPathWriteNewValue,
  FollowerMode
};

class Consensus {
 public:
  Consensus(int my_id, std::vector<int> &remote_ids);
  ~Consensus();

  void commitHandler(std::function<void(uint8_t *buf, size_t len)> committer);

  ProposeError propose(uint8_t *buf, size_t len);
  int potentialLeader();

 private:
  std::unique_ptr<RdmaConsensus> impl;
};
}  // namespace dory