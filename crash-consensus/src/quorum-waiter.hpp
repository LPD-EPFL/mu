#pragma once

#include <functional>
#include <iostream>
#include <vector>

#include <dory/extern/ibverbs.hpp>

#include "message-identifier.hpp"

namespace dory {
template <class ID>
class SerialQuorumWaiter {
 public:
  using ReqIDType = ID;
  SerialQuorumWaiter() = default;

  SerialQuorumWaiter(quorum::Kind kind, std::vector<int>& remote_ids,
                     int quorum_size, ID next_id, ID modulo);

  inline ID reqID() const { return next_id; }
  inline ID nextReqID() const { return next_id + modulo; }
  inline void setFastReqID(ID id) {
    fast_id = id;
  }

  inline ID fetchAndIncFastID() {
    auto ret = fast_id;
    fast_id += modulo;
    return ret;
  }

  inline ID nextFastReqID() const { return fast_id; }

  bool fastConsume(std::vector<struct ibv_wc>& entries, int num, int& ret_left);
  bool consume(std::vector<struct ibv_wc>& entries,
               std::vector<int>& successful_ops);

  bool canContinueWith(ID expected) const;
  bool canContinueWithOutstanding(int outstanding, ID expected) const;

  int maximumResponses() const;

  //  private:
  void reset(ID next);

  void changeQuorum(int size) { quorum_size = size; }

  using PackerPID = int;
  using PackerSeq = ID;

  static inline constexpr uint64_t packer(quorum::Kind k, PackerPID pid,
                                          PackerSeq seq) {
    return quorum::pack(k, pid, seq);
  }

  inline quorum::Kind kindOfOp() { return kind; }

 private:
  quorum::Kind kind;
  std::vector<ID> scoreboard;
  int quorum_size;
  ID next_id;
  ID fast_id;
  int left;
  int modulo;
};
}  // namespace dory

namespace dory {
class SequentialQuorumWaiter : public SerialQuorumWaiter<uint64_t> {
 public:
  SequentialQuorumWaiter() : SerialQuorumWaiter<uint64_t>() {}
  SequentialQuorumWaiter(quorum::Kind kind, std::vector<int>& remote_ids,
                         int quorum_size, uint64_t next_id)
      : SerialQuorumWaiter<uint64_t>(kind, remote_ids, quorum_size, next_id,
                                     1) {}
};

class ModuloQuorumWaiter : public SerialQuorumWaiter<int64_t> {
 public:
  ModuloQuorumWaiter() : SerialQuorumWaiter<int64_t>() {}
  ModuloQuorumWaiter(quorum::Kind kind, std::vector<int>& remote_ids,
                     int quorum_size, int64_t next_id, int modulo)
      : SerialQuorumWaiter<int64_t>(kind, remote_ids, quorum_size, next_id,
                                    modulo) {}
};
}  // namespace dory

namespace dory {
class FailureTracker {
 public:
  FailureTracker() {}

  FailureTracker(quorum::Kind kind, std::vector<int>& remote_ids,
                 int tolerated_failures)
      : kind{kind}, tolerated_failures{tolerated_failures}, track_id{0} {
    auto max_elem = Identifiers::maxID(remote_ids);
    failures.resize(max_elem + 1);

    reset();
  }

  void reset() {
    track_id = 0;
    failed = 0;
    std::fill(failures.begin(), failures.end(), false);
  }

  void track(uint64_t id) {
    if (track_id != 0) {
      reset();
      track_id = id;
    }
  }

  bool isUnrecoverable(std::vector<struct ibv_wc>& entries) {
    for (auto const& entry : entries) {
      if (entry.status != IBV_WC_SUCCESS) {
        auto [k, pid, seq] = quorum::unpackAll<uint64_t, uint64_t>(entry.wr_id);

        // std::cout << "(pid, seq)=(" << pid << ", " << seq << ")" <<
        // std::endl;

        if (k == kind && seq >= track_id && !failures[pid]) {
          failures[pid] = true;
          failed += 1;
          // std::cout << "Add " << pid << " to failed. Total failed=" << failed
          //           << ". Tolerated failures = " << tolerated_failures
          //           << std::endl;
        }
#ifndef NDEBUG
        else {
          std::cout << "Found unrelated remnants in the polled responses"
                    << std::endl;
        }
#endif
      }

      if (failed > tolerated_failures) {
        return true;
      }
    }

    return false;
  }

 private:
  quorum::Kind kind;
  int tolerated_failures;

  std::vector<uint64_t> failures;
  uint64_t track_id;
  int failed;
};
}  // namespace dory