#include <dory/crash-consensus.hpp>
#include <dory/crash-consensus.h>

namespace dory {
void consensus___dummy___symbol___() {
	int x = 0, z = 0;
	int *y = nullptr;
	new_consensus(x, y, z);
}
void consensus____dummy____symbol____() {
	int x = 0;
	std::vector<int> y;
	Consensus c(x, y);
}
}

// #ifdef  __cplusplus
// # include <dory/crash-consensus.hpp>
// #endif

// #include <stdint.h>


// enum ProposeError {
//   ProposalNoError = 0,  // Placeholder for the 0 value
//   ProposalMutexUnavailable,
//   ProposalFastPath,
//   ProposalFastPathRecyclingTriggered,
//   ProposalSlowPathCatchFUO,
//   ProposalSlowPathUpdateFollowers,
//   ProposalSlowPathCatchProposal,
//   ProposalSlowPathUpdateProposal,
//   ProposalSlowPathReadRemoteLogs,
//   ProposalSlowPathWriteAdoptedValue,
//   ProposalSlowPathWriteNewValue,
//   ProposalFollowerMode,
//   ProposalSlowPathLogRecycled
// };

// // C Interface.
// typedef void* consensus_t;
// typedef void (*committer_t)(bool leader, uint8_t *buf, size_t len, void *ctx);

// // Need an explicit constructor and destructor.
// extern "C" consensus_t new_consensus(int my_id, int *remote_ids, int remote_ids_num);
// extern "C" void free_consensus(consensus_t c);

// extern "C" void consensus_attach_commit_handler(consensus_t c, committer_t f, void *committer_ctx);
// extern "C" ProposeError consensus_propose(consensus_t c, uint8_t *buf, size_t len);
// extern "C" int consensus_potential_leader(consensus_t c);


// consensus_t new_consensus(int my_id, int *remote_ids, int remote_ids_num) {
// 	std::vector<int> rem_ids;
// 	for (int i = 0; i < remote_ids_num; i++) {
// 		rem_ids.push_back(remote_ids[i]);
// 	}

//     return reinterpret_cast<void*>(new dory::Consensus(my_id, rem_ids));
// }

// void free_consensus(consensus_t c) {
// 	auto cons = reinterpret_cast<dory::Consensus*>(c);
// 	delete cons;
// }

// void consensus_attach_commit_handler(consensus_t c, committer_t f, void *committer_ctx) {
// 	auto cons = reinterpret_cast<dory::Consensus*>(c);
// 	cons->commitHandler([f, committer_ctx](bool leader, uint8_t *buf, size_t len) {
// 		f(leader, buf, len, committer_ctx);
// 	});
// }

// int consensus_potential_leader(consensus_t c) {
// 	return reinterpret_cast<dory::Consensus*>(c)->potentialLeader();
// }

// ProposeError consensus_propose(consensus_t c, uint8_t *buf, size_t len) {
// 	return static_cast<ProposeError>(reinterpret_cast<dory::Consensus*>(c)->propose(buf, len));
// }