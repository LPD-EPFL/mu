#pragma once

#include <vector>

#include "branching.hpp"
#include "context.hpp"
#include "error.hpp"
#include "quorum-waiter.hpp"
#include "timers.h"

namespace dory {
template <class QuorumWaiter, class ErrorType>
class FixedSizeMajorityOperation {
 public:
  FixedSizeMajorityOperation() {}
  FixedSizeMajorityOperation(ConnectionContext *context, QuorumWaiter qw,
                             std::vector<int> &remote_ids)
      : ctx{context}, qw{qw}, kind{qw.kindOfOp()} {
    quorum_size = quorum::majority(ctx->remote_ids.size() + 1) - 1;

    successful_ops.resize(remote_ids.size());
    successful_ops.clear();

    auto tolerated_failures = quorum::minority(ctx->remote_ids.size() + 1);
    failed_majority = FailureTracker(kind, ctx->remote_ids, tolerated_failures);
    failed_majority.track(qw.reqID());

    auto &rcs = ctx->ce.connections();
    for (auto &[pid, rc] : rcs) {
      connections.push_back(Conn(pid, &rc));
    }
  }

  FixedSizeMajorityOperation(ConnectionContext *context, QuorumWaiter qw,
                             std::vector<int> &remote_ids, int refactor_me)
      : ctx{context}, qw{qw}, kind{qw.kindOfOp()} {
    (void)refactor_me;

    successful_ops.resize(remote_ids.size());
    successful_ops.clear();

    auto tolerated_failures = 0;
    failed_majority = FailureTracker(kind, ctx->remote_ids, tolerated_failures);
    failed_majority.track(qw.reqID());

    auto &rcs = ctx->ce.connections();
    for (auto &[pid, rc] : rcs) {
      connections.push_back(Conn(pid, &rc));
    }
  }

  void recoverFromError(std::unique_ptr<MaybeError> &supplied_error) {
    if (supplied_error->type() == ErrorType::value) {
      ErrorType &error = *dynamic_cast<ErrorType *>(supplied_error.get());

      auto req_id = error.req();

      // // Just for testing:
      // failed_majority = FailureTracker(kind, ctx->remote_ids, 3);
      // quorum_size = 1;
      // qw.changeQuorum(quorum_size);

      failed_majority.reset();
      failed_majority.track(req_id);
      qw.reset(req_id);

      // TODO (question):
      // To reuse the same req_id, doe we need to make sure no outstanding
      // requests are on the wire?
    } else {
      throw std::runtime_error("Unimplemented handing of this error");
    }
  }

  std::unique_ptr<MaybeError> read(std::vector<void *> &to_local_memories,
                                   size_t size,
                                   std::vector<uintptr_t> &from_remote_memories,
                                   std::atomic<Leader> &leader) {
    return op_with_leader_bail(to_local_memories, size, from_remote_memories,
                               leader);
  }

  std::unique_ptr<MaybeError> write(void *from_local_memory, size_t size,
                                    std::vector<uintptr_t> &to_remote_memories,
                                    std::atomic<Leader> &leader) {
    return op_with_leader_bail(from_local_memory, size, to_remote_memories,
                               leader);
  }

  std::unique_ptr<MaybeError> read(
      std::vector<void *> &to_local_memories, size_t size,
      std::vector<uintptr_t> &from_remote_memories) {
    return op_without_leader(to_local_memories, size, from_remote_memories);
  }

  std::unique_ptr<MaybeError> write(
      void *from_local_memory, size_t size,
      std::vector<uintptr_t> &to_remote_memories) {
    return op_without_leader(from_local_memory, size, to_remote_memories);
  }

  bool fastWrite(void *from_local_memory, size_t size,
                 std::vector<uintptr_t> &to_remote_memories, uintptr_t offset,
                 std::atomic<Leader> &leader) {
    IGNORE(leader);

    auto req_id = qw.reqID();
    auto next_req_id = qw.nextReqID();

    for (auto &c : connections) {
      c.rc->postSendSingleCached(
          ReliableConnection::RdmaWrite,
          QuorumWaiter::packer(kind, c.pid, req_id), from_local_memory, size,
          c.rc->remoteBuf() + to_remote_memories[c.pid] + offset);
    }
    int expected_nr = quorum_size;

    auto cq = ctx->cq.get();
    entries.resize(expected_nr);
    int num = 0;
    int loops = 0;
    constexpr unsigned mask = (1 << 14) - 1;  // Must be power of 2 minus 1

    while (likely(!qw.canContinueWith(next_req_id))) {
      if (likely((num = ibv_poll_cq(cq, expected_nr, &entries[0])) >= 0)) {
        if (unlikely(!qw.fastConsume(entries, num, expected_nr))) {
          return false;
        }
      } else {
        return false;
      }
    }

    loops = (loops + 1) & mask;
    if (loops == 0) {
      auto ldr = leader.load();
      if (ldr.requester != ctx->my_id) {
        return false;
      }
    }

    return true;
  }

  std::unique_ptr<MaybeError> fastWriteError() {
    auto req_id = qw.reqID();
    return std::make_unique<ErrorType>(req_id);
  }

  std::vector<int> &successes() { return successful_ops; }

 private:
  template <class T>
  std::unique_ptr<MaybeError> op_with_leader_bail(
      T const &local_memory, size_t size, std::vector<uintptr_t> &remote_memory,
      std::atomic<Leader> &leader) {
    successful_ops.clear();

    auto req_id = qw.reqID();
    auto next_req_id = qw.nextReqID();

    auto &rcs = ctx->ce.connections();
    for (auto &[pid, rc] : rcs) {
      if constexpr (std::is_same_v<T, std::vector<void *>>) {
        rc.postSendSingle(ReliableConnection::RdmaRead,
                          QuorumWaiter::packer(kind, pid, req_id),
                          local_memory[pid], size,
                          rc.remoteBuf() + remote_memory[pid]);
      } else {
        rc.postSendSingle(ReliableConnection::RdmaWrite,
                          QuorumWaiter::packer(kind, pid, req_id), local_memory,
                          size, rc.remoteBuf() + remote_memory[pid]);
      }
    }

    int expected_nr = rcs.size();
    int loops = 0;
    while (!qw.canContinueWith(next_req_id)) {
      entries.resize(expected_nr);
      if (ctx->cb.pollCqIsOK(ctx->cq, entries)) {
        if (!qw.consume(entries, successful_ops)) {
          if (failed_majority.isUnrecoverable(entries)) {
            return std::make_unique<ErrorType>(req_id);
          }
        }
      } else {
        std::cout << "Poll returned an error" << std::endl;
        return std::make_unique<ErrorType>(req_id);
      }

      // Workaround: When leader changes, some poll events may get lost
      // (most likely due to a bug on the driver) and we are stuck in an
      // infinite loop.
      loops += 1;
      if (loops % 1024 == 0) {
        loops = 0;
        auto ldr = leader.load();
        if (ldr.requester != ctx->my_id) {
          return std::make_unique<ErrorType>(req_id);
        }
      }
    }

    return std::make_unique<NoError>();
  }

  template <class T>
  std::unique_ptr<MaybeError> op_without_leader(
      T const &local_memory, size_t size,
      std::vector<uintptr_t> &remote_memory) {
    successful_ops.clear();

    auto req_id = qw.reqID();
    auto next_req_id = qw.nextReqID();

    auto &rcs = ctx->ce.connections();
    for (auto &[pid, rc] : rcs) {
      if constexpr (std::is_same_v<T, std::vector<void *>>) {
        rc.postSendSingle(ReliableConnection::RdmaRead,
                          QuorumWaiter::packer(kind, pid, req_id),
                          local_memory[pid], size,
                          rc.remoteBuf() + remote_memory[pid]);
      } else {
        rc.postSendSingle(ReliableConnection::RdmaWrite,
                          QuorumWaiter::packer(kind, pid, req_id), local_memory,
                          size, rc.remoteBuf() + remote_memory[pid]);
      }
    }

    int expected_nr = rcs.size();
    while (!qw.canContinueWith(next_req_id)) {
      entries.resize(expected_nr);
      if (ctx->cb.pollCqIsOK(ctx->cq, entries)) {
        if (!qw.consume(entries, successful_ops)) {
          if (failed_majority.isUnrecoverable(entries)) {
            return std::make_unique<ErrorType>(req_id);
          }
        }
      } else {
        std::cout << "Poll returned an error" << std::endl;
        return std::make_unique<ErrorType>(req_id);
      }
    }

    return std::make_unique<NoError>();
  }

 private:
  struct Conn {
    Conn(int pid, dory::ReliableConnection *rc) : pid{pid}, rc{rc} {}
    int pid;
    dory::ReliableConnection *rc;
  };

 private:
  ConnectionContext *ctx;

  QuorumWaiter qw;
  quorum::Kind kind;

  int quorum_size;

  FailureTracker failed_majority;

  std::vector<struct ibv_wc> entries;
  std::vector<int> successful_ops;
  std::vector<Conn> connections;
};
}  // namespace dory