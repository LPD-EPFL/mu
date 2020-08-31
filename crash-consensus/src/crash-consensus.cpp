#include <stdexcept>

#include "consensus.hpp"
#include "crash-consensus.hpp"

namespace dory {
Consensus::Consensus(int my_id, std::vector<int> &remote_ids,
                     int outstanding_req, ThreadBank threadBank) {
  ConsensusConfig::ThreadConfig config;

  switch (threadBank) {
    case ThreadBank::A:
      impl =
          std::make_unique<RdmaConsensus>(my_id, remote_ids, outstanding_req);
      break;
    case ThreadBank::B:
      config.consensusThreadCoreID = ConsensusConfig::consensusThreadBankB_ID;
      config.switcherThreadCoreID = ConsensusConfig::switcherThreadBankB_ID;
      config.heartbeatThreadCoreID = ConsensusConfig::heartbeatThreadBankB_ID;
      config.followerThreadCoreID = ConsensusConfig::followerThreadBankB_ID;
      config.prefix = "Secondary-";
      impl = std::make_unique<RdmaConsensus>(my_id, remote_ids, outstanding_req,
                                             config);
      break;
    default:
      throw std::runtime_error("Unreachable, software bug");
  }
}

Consensus::~Consensus() {}

void Consensus::commitHandler(
    std::function<void(bool leader, uint8_t *buf, size_t len)> committer) {
  impl->commitHandler(committer);
}

ProposeError Consensus::propose(uint8_t *buf, size_t len) {
  int ret = impl->propose(buf, len);
  return static_cast<ProposeError>(ret);
}

int Consensus::potentialLeader() { return impl->potentialLeader(); }

std::pair<uint64_t, uint64_t> Consensus::proposedReplicatedRange() {
  return impl->proposedReplicatedRange();
}
}  // namespace dory