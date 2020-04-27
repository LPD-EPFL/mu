#include "crash-consensus.hpp"
#include "consensus.hpp"

namespace dory {
Consensus::Consensus(int my_id, std::vector<int> &remote_ids)
    : impl{std::make_unique<RdmaConsensus>(my_id, remote_ids)} {}

Consensus::~Consensus() {}

void Consensus::commitHandler(
    std::function<void(uint8_t *buf, size_t len)> committer) {
  impl->commitHandler(committer);
}

ProposeError Consensus::propose(uint8_t *buf, size_t len) {
  int ret = impl->propose(buf, len);
  return static_cast<ProposeError>(ret);
}

int Consensus::potentialLeader() { return impl->potentialLeader(); }
}  // namespace dory