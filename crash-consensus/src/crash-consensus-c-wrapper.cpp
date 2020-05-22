#include "consensus.hpp"
#include "crash-consensus.h"

consensus_t new_consensus(int my_id, int *remote_ids, int remote_ids_num) {
  std::vector<int> rem_ids;
  for (int i = 0; i < remote_ids_num; i++) {
    rem_ids.push_back(remote_ids[i]);
  }

  return reinterpret_cast<void *>(new dory::RdmaConsensus(my_id, rem_ids));
}

void free_consensus(consensus_t c) {
  auto cons = reinterpret_cast<dory::RdmaConsensus *>(c);
  delete cons;
}

void consensus_attach_commit_handler(consensus_t c, committer_t f,
                                     void *committer_ctx) {
  auto cons = reinterpret_cast<dory::RdmaConsensus *>(c);
  cons->commitHandler(
      [f, committer_ctx](bool leader, uint8_t *buf, size_t len) {
        f(leader, buf, len, committer_ctx);
      });
}

void consensus_spawn_thread(consensus_t c) {
  auto cons = reinterpret_cast<dory::RdmaConsensus *>(c);
  cons->handover.store(false);
  cons->handover_thd = std::thread([cons]() {
    while (true) {
      while (cons->handover.load() == false) {
      }

      cons->handover_ret =
          cons->propose(cons->handover_buf, cons->handover_buf_len);

      cons->handover.store(false);
    }
  });

  if (cons->threadConfig.pinThreads) {
    dory::pinThreadToCore(cons->handover_thd, cons->threadConfig.handoverThreadCoreID);
  }

  if (dory::ConsensusConfig::nameThreads) {
    dory::setThreadName(cons->handover_thd, dory::ConsensusConfig::handoverThreadName);
  }
}

int consensus_potential_leader(consensus_t c) {
  return reinterpret_cast<dory::RdmaConsensus *>(c)->potentialLeader();
}

ConsensusProposeError consensus_propose(consensus_t c, uint8_t *buf,
                                        size_t len) {
  return static_cast<ConsensusProposeError>(
      reinterpret_cast<dory::RdmaConsensus *>(c)->propose(buf, len));
}

ConsensusProposeError consensus_propose_thread(consensus_t c, uint8_t *buf,
                                               size_t len) {
  auto cons = reinterpret_cast<dory::RdmaConsensus *>(c);
  cons->handover_buf = buf;
  cons->handover_buf_len = len;
  cons->handover.store(true);
  while (cons->handover.load() == true) {
  }

  return static_cast<ConsensusProposeError>(cons->handover_ret);
}