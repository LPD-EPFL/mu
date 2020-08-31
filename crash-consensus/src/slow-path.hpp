#pragma once

#include "error.hpp"
#include "log.hpp"
#include "message-identifier.hpp"

#include <iterator>
#include <set>

#include "context.hpp"
#include "remote-log-reader.hpp"

#include "fixed-size-majority.hpp"

namespace dory {
class CatchUpWithFollowers {
 public:
  CatchUpWithFollowers(ReplicationContext *context,
                       ScratchpadMemory &scratchpad)
      : r_ctx{context},
        c_ctx{&context->cc},
        scratchpad{scratchpad},
        proposal_nr(c_ctx->my_id) {
    proposal_offset = r_ctx->log.offset(Log::MinProposal).first;
    proposal_size = r_ctx->log.offset(Log::MinProposal).second;
    if (r_ctx->log.offset(Log::MinProposal).second != sizeof(uint64_t)) {
      throw std::runtime_error("Advertised proposal number must be `uint64_t`");
    }

    quorum_size = quorum::majority(c_ctx->remote_ids.size() + 1) - 1;
    modulo = Identifiers::maxID(c_ctx->my_id, c_ctx->remote_ids);

    SequentialQuorumWaiter waiterRead(quorum::ProposalRd, c_ctx->remote_ids,
                                      quorum_size, 1);
    majR = MajorityReader(c_ctx, waiterRead, c_ctx->remote_ids);

    // ModuloQuorumWaiter waiterWrite(quorum::ProposalWr, ctx->remote_ids,
    // quorum_size, ctx->my_id, modulo);
    SequentialQuorumWaiter waiterWrite(quorum::ProposalWr, c_ctx->remote_ids,
                                       quorum_size, 1);
    majW = MajorityWriter(c_ctx, waiterWrite, c_ctx->remote_ids);

    remote_mem_locations.resize(Identifiers::maxID(c_ctx->remote_ids) + 1);
    std::fill(remote_mem_locations.begin(), remote_mem_locations.end(),
              r_ctx->log_offset + proposal_offset);

    for (auto addr : scratchpad.readProposalNrSlots()) {
      local_memory_locations.push_back(reinterpret_cast<void *>(addr));
    }

    fuo_offset = r_ctx->log.offset(Log::FUO).first;
    fuo_size = r_ctx->log.offset(Log::FUO).second;
    if (r_ctx->log.offset(Log::FUO).second != sizeof(uint64_t)) {
      throw std::runtime_error("Advertised proposal number must be `uint64_t`");
    }

    // Wait for response from everyone, but tolerate failures
    SequentialQuorumWaiter waiterFUORead(quorum::FUORd, c_ctx->remote_ids,
                                         c_ctx->remote_ids.size(), 1);
    majFUOR = FUOMajorityReader(c_ctx, waiterFUORead, c_ctx->remote_ids);

    SequentialQuorumWaiter waiterFUODiffWrite(
        quorum::FUODiffWr, c_ctx->remote_ids, quorum_size, 1);

    majFUOW =
        FUODiffMajorityWriter(c_ctx, waiterFUODiffWrite, c_ctx->remote_ids, 0);

    fuo_remote_mem_locations.resize(Identifiers::maxID(c_ctx->remote_ids) + 1);
    std::fill(fuo_remote_mem_locations.begin(), fuo_remote_mem_locations.end(),
              r_ctx->log_offset + fuo_offset);

    for (auto addr : scratchpad.readFUOSlots()) {
      fuo_local_memory_locations.push_back(reinterpret_cast<void *>(addr));
    }

    need_update_pids.reserve(Identifiers::maxID(c_ctx->remote_ids));
    need_update_fuo_diff.resize(Identifiers::maxID(c_ctx->remote_ids) + 1);
    need_update_fuo_local_offset.resize(Identifiers::maxID(c_ctx->remote_ids) +
                                        1);
    need_update_fuo_remote_offset.resize(Identifiers::maxID(c_ctx->remote_ids) +
                                         1);
    need_update_quorum = 0;
  }

  void recoverFromError(std::unique_ptr<MaybeError> &supplied_error) {
    switch (supplied_error->type()) {
      case ReadProposalMajorityError::value:
        majR.recoverFromError(supplied_error);
        break;
      case WriteProposalMajorityError::value:
        majW.recoverFromError(supplied_error);
        break;
      case ReadFUOMajorityError::value:
        majFUOR.recoverFromError(supplied_error);
        break;
      case WriteFUODiffMajorityError::value:
        majFUOW.recoverFromError(supplied_error);
        break;
      case CatchProposalRetryError::value:
        // std::cout << "Nothing to recover" << std::endl;
        break;
      default:
        throw std::runtime_error("Unimplemented handing of this error");
    }
  }

  std::unique_ptr<MaybeError> catchProposal(std::atomic<Leader> &leader) {
    // Read from a majority - 1 (because we will also include ourselves)
    auto err = majR.read(local_memory_locations, proposal_size,
                         remote_mem_locations, leader);

    if (!err->ok()) {
      return err;
    }

    auto max_proposal = r_ctx->log.headerProposalAddress();
    auto &successful_pids = majR.successes();
    for (auto pid : successful_pids) {
      max_proposal = std::max(
          max_proposal,
          *reinterpret_cast<uint64_t *>(scratchpad.readProposalNrSlots()[pid]));
    }

    if (max_proposal <= proposal_nr) {
      return std::make_unique<NoError>();
    }

    proposal_nr += modulo;
    return std::make_unique<CatchProposalRetryError>(max_proposal);
  }

  std::unique_ptr<MaybeError> catchFUO(std::atomic<Leader> &leader) {
    // Read from a majority - 1 (because we will also include ourselves)
    auto err = majFUOR.read(fuo_local_memory_locations, fuo_size,
                            fuo_remote_mem_locations, leader);

    if (!err->ok()) {
      return err;
    }

    need_update_pids.clear();

    auto my_fuo = r_ctx->log.headerFirstUndecidedOffset();
    // Under normal conditions, all respond because we wait for all,
    // but we tolerate a minority to fail.
    auto &successful_pids = majFUOR.successes();

    for (auto pid : successful_pids) {
      auto remote_fuo =
          *reinterpret_cast<uint64_t *>(scratchpad.readFUOSlots()[pid]);
      if (my_fuo > remote_fuo) {
        need_update_pids.push_back(pid);
        need_update_fuo_diff[pid] = my_fuo - remote_fuo;
        need_update_fuo_local_offset[pid] = r_ctx->log.headerPtr() + remote_fuo;
        need_update_fuo_remote_offset[pid] = r_ctx->log_offset + remote_fuo;
      }
    }

    auto already_up_to_date = successful_pids.size() - need_update_pids.size();
    if (already_up_to_date >= quorum_size) {
      need_update_quorum = 0;
    } else {
      need_update_quorum = quorum_size - already_up_to_date;
    }

    return std::make_unique<NoError>();
  }

  // Update only the followers that are behind
  std::unique_ptr<MaybeError> updateFollowers(std::atomic<Leader> &leader) {
    if (need_update_pids.size() > 0) {
      SequentialQuorumWaiter waiterFUODiffWrite(
          quorum::FUODiffWr, need_update_pids, need_update_pids.size(),
          majFUOW.reqID());

      majFUOW =
          FUODiffMajorityWriter(c_ctx, waiterFUODiffWrite, need_update_pids,
                                need_update_pids.size() - need_update_quorum);

      auto err =
          majFUOW.write(need_update_fuo_local_offset, need_update_fuo_diff,
                        need_update_fuo_remote_offset, leader);

      if (!err->ok()) {
        return err;
      }
    }

    return std::make_unique<NoError>();
  }

  std::unique_ptr<MaybeError> updateWithCurrentProposal(
      std::atomic<Leader> &leader) {
    // TODO (Question)
    // We write to ourselves and then we write to a majority - 1. Is this ok?

    // TODO (Question)
    // The initial implementation does the write but it doesn't wait for a
    // majority. Instead, using the current abstractions we do wait. Can we live
    // with this cost in performance?

    r_ctx->log.updateHeaderProposal(proposal_nr);
    uint64_t *temp = reinterpret_cast<uint64_t *>(scratchpad.writeSlot());
    *temp = proposal_nr;

    // Write to a majority - 1 (because we will also include ourselves)
    auto err = majW.write(temp, proposal_size, remote_mem_locations, leader);

    if (!err->ok()) {
      return err;
    }

    return std::make_unique<NoError>();
  }

  inline uint64_t proposal() const { return proposal_nr; }

 private:
  ReplicationContext *r_ctx;
  ConnectionContext *c_ctx;
  ScratchpadMemory &scratchpad;
  uint64_t proposal_nr;

  using MajorityReader = FixedSizeMajorityOperation<SequentialQuorumWaiter,
                                                    ReadProposalMajorityError>;
  // using MajorityWriter = FixedSizeMajorityOperation<ModuloQuorumWaiter,
  // WriteProposalMajorityError>;
  using MajorityWriter = FixedSizeMajorityOperation<SequentialQuorumWaiter,
                                                    WriteProposalMajorityError>;

  MajorityWriter majW;
  MajorityReader majR;

  std::vector<uintptr_t> remote_mem_locations;
  std::vector<void *> local_memory_locations;

  using FUOMajorityReader =
      FixedSizeMajorityOperation<SequentialQuorumWaiter, ReadFUOMajorityError>;
  using FUODiffMajorityWriter =
      FixedSizeMajorityOperation<SequentialQuorumWaiter,
                                 WriteFUODiffMajorityError>;

  FUOMajorityReader majFUOR;
  FUODiffMajorityWriter majFUOW;

  std::vector<uintptr_t> fuo_remote_mem_locations;
  std::vector<void *> fuo_local_memory_locations;
  std::vector<int> need_update_pids;
  std::vector<size_t> need_update_fuo_diff;
  std::vector<void *> need_update_fuo_local_offset;
  std::vector<uintptr_t> need_update_fuo_remote_offset;

  size_t need_update_quorum;
  size_t quorum_size;

  uint64_t proposal_offset, proposal_size;
  uint64_t fuo_offset, fuo_size;
  int modulo;
};

class LogSlotReader {
 public:
  // TODO: Eliminate duplication in the two constructors
  LogSlotReader(ReplicationContext *context, ScratchpadMemory &scratchpad,
                uintptr_t iterators_offset)
      : r_ctx{context}, c_ctx{&context->cc}, scratchpad{scratchpad} {
    remote_iterators.resize(
        Identifiers::maxID(c_ctx->my_id, c_ctx->remote_ids) + 1);

    entry_read_req_id = 1;
    to_be_polled = ToBePolled(quorum::EntryRd, c_ctx->remote_ids);

    for (auto id : c_ctx->remote_ids) {
      auto iter = r_ctx->log.remoteIterator(id, iterators_offset);
      remote_iterators[id] = WrappedRemoteIterator(iter);
    }

    // Exclude myself from the quorum
    quorum_size = quorum::majority(c_ctx->remote_ids.size() + 1) - 1;

    successful_reads.resize(c_ctx->remote_ids.size());
    successful_reads.clear();

    tolerated_failures = quorum::minority(c_ctx->remote_ids.size() + 1);
  }

  /*
  LogSlotReader(Context *context, ScratchpadMemory &scratchpad)
      : ctx{context}, scratchpad{scratchpad} {
    remote_iterators.resize(Identifiers::maxID(ctx->my_id, ctx->remote_ids) +
  1);

    entry_read_req_id = 1;
    to_be_polled = ToBePolled(quorum::EntryRd, ctx->remote_ids);

    for (auto id : ctx->remote_ids) {
      auto iter = ctx->log.remoteIterator(id);
      remote_iterators[id] =
          IndexedRemoteIterator(iter, entry_read_req_id, iter.remoteOffset());
    }

    // Exclude myself from the quorum
    quorum_size = quorum::majority(ctx->remote_ids.size() + 1) - 1;

    successful_reads.resize(ctx->remote_ids.size());
    successful_reads.clear();

    tolerated_failures = quorum::minority(ctx->remote_ids.size() + 1);
  }
  */

  // This function is used only to test the recovery
  void addQuorumSize(int term) { quorum_size += term; }

  // This function is used only to test the recovery
  void addToleratedFailures(int term) { tolerated_failures += term; }

  void recoverFromError(std::unique_ptr<MaybeError> &supplied_error) {
    if (supplied_error->type() == MaybeError::ReadLogMajorityError) {
      ReadLogMajorityError &error =
          *dynamic_cast<ReadLogMajorityError *>(supplied_error.get());

      auto req_id = error.req();

      entry_read_req_id = req_id;

      /*
      // To reuse the same entry_read_req_id, we need to make sure no
      // outstanding requests are on the wire
      unsigned expected_nr = to_be_polled.pollList().size();
      unsigned responses = 0;
      while (responses < expected_nr) {
        entries.resize(expected_nr);
        if (ctx->cb.pollCqIsOK(ctx->cq, entries)) {
          auto [positive_resp, negative_resp] =
              to_be_polled.actuallyPolled(entries);
          responses += positive_resp.get().size() + negative_resp.get().size();
        } else {
          std::cout << "Poll returned an error" << std::endl;
        }
      }
      */
      // Reset polling tracking
      to_be_polled = ToBePolled(quorum::EntryRd, c_ctx->remote_ids);

      failed_pids.clear();
    }
  }

  std::unique_ptr<MaybeError> readSlotAt(uint64_t remote_offset,
                                         std::atomic<Leader> &leader) {
    to_be_polled.focusOnReqID(entry_read_req_id);
    to_be_polled.rescheduleCompleted();

    successful_reads.clear();
    auto &rcs = c_ctx->ce.connections();

    auto wait_for = quorum_size;

    int loops = 0;
    do {
      ptrdiff_t offset;
      uint32_t size;

      if (!to_be_polled.postList().empty()) {
        for (auto &pid : to_be_polled.postList()) {
          auto &rc = rcs.find(pid)->second;
          auto store_addr = scratchpad.readLogEntrySlots()[pid];

          auto offset_size =
              remote_iterators[pid].iterator().lookAt(remote_offset);
          offset = offset_size.first;
          size = static_cast<uint32_t>(offset_size.second);
          remote_iterators[pid].iterator().storeDest(store_addr);

          auto ok = rc.postSendSingle(
              ReliableConnection::RdmaRead,
              quorum::pack(quorum::EntryRd, pid, entry_read_req_id), store_addr,
              size, rc.remoteBuf() + r_ctx->log_offset + offset);

          if (!ok) {
            return std::make_unique<ReadLogMajorityError>(entry_read_req_id);
          }
        }

        to_be_polled.posted();
      }

      auto expected_nr = to_be_polled.pollList().size();
      size_t responses = 0;

      // // This loop in not necessary. We believe it improves performance
      // while (responses < std::min(wait_for, expected_nr)) {
      entries.resize(expected_nr);
      if (c_ctx->cb.pollCqIsOK(c_ctx->cq, entries)) {
        auto [positive_resp, negative_resp] =
            to_be_polled.actuallyPolled(entries);

        // On the positive responses, try to move the iterator.
        for (auto pid : positive_resp.get()) {
          auto &it = remote_iterators[pid];
          if (it.isPopulated()) {
            if (it.isComplete()) {
              to_be_polled.moveToCompleted(pid);
              successful_reads.push_back(pid);
            }
          } else {
            to_be_polled.moveToCompleted(pid);
            successful_reads.push_back(-pid);
          }
        }

        // The negative responses have already been excluded from
        // `ToBePolled`.
        if (!negative_resp.get().empty()) {
          for (auto pid : negative_resp.get()) {
            failed_pids.insert(pid);
          }

          if (failed_pids.size() > tolerated_failures) {
            return std::make_unique<ReadLogMajorityError>(entry_read_req_id);
          }
        }

        responses += entries.size();
      } else {
        std::cout << "Poll returned an error" << std::endl;
        return std::make_unique<ReadLogMajorityError>(entry_read_req_id);
      }

      // Workaround: When leader changes, the some poll events may get lost
      // (most likely due to a bug on the driver) and we are stuck in an
      // infinite loop.
      loops += 1;
      if (loops % 1024 == 0) {
        loops = 0;
        auto ldr = leader.load();
        if (ldr.requester != c_ctx->my_id) {
          return std::make_unique<ReadLogMajorityError>(entry_read_req_id);
        }
      }
      // }

    } while (to_be_polled.completedList().size() < wait_for);

    entry_read_req_id += 1;
    return std::make_unique<NoError>();
  }

  std::vector<int> &successes() { return successful_reads; }

 private:
  ReplicationContext *r_ctx;
  ConnectionContext *c_ctx;
  ScratchpadMemory &scratchpad;

  std::vector<WrappedRemoteIterator> remote_iterators;
  ToBePolled to_be_polled;

  size_t quorum_size;
  size_t tolerated_failures;
  uint64_t entry_read_req_id;

  std::vector<int> successful_reads;
  std::vector<struct ibv_wc> entries;

  std::set<int> failed_pids;
};

// class EntryReader {
//   EntryReader() = default;
// };

// class UpdateFollowers {
//   UpdateFollowers() = default;
// };

// class ReplicateProposal {
//   ReplicateProposal() = default;
// };

}  // namespace dory