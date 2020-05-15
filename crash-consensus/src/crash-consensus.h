#pragma once

#ifdef __cplusplus
#include <cstdint>
extern "C" {
#else
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#endif

typedef enum {
  ProposalNoError = 0,  // Placeholder for the 0 value
  ProposalMutexUnavailable,
  ProposalFastPath,
  ProposalFastPathRecyclingTriggered,
  ProposalSlowPathCatchFUO,
  ProposalSlowPathUpdateFollowers,
  ProposalSlowPathCatchProposal,
  ProposalSlowPathUpdateProposal,
  ProposalSlowPathReadRemoteLogs,
  ProposalSlowPathWriteAdoptedValue,
  ProposalSlowPathWriteNewValue,
  ProposalFollowerMode,
  ProposalSlowPathLogRecycled
} ConsensusProposeError;

// C Interface.
typedef void *consensus_t;
typedef void (*committer_t)(bool leader, uint8_t *buf, size_t len, void *ctx);

// Need an explicit constructor and destructor.
consensus_t new_consensus(int my_id, int *remote_ids, int remote_ids_num);
void free_consensus(consensus_t c);

void consensus_attach_commit_handler(consensus_t c, committer_t f,
                                     void *committer_ctx);
ConsensusProposeError consensus_propose(consensus_t c, uint8_t *buf,
                                        size_t len);
int consensus_potential_leader(consensus_t c);

#ifdef __cplusplus
}
#endif