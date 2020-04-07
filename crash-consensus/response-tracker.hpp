#pragma once

#include <dory/extern/ibverbs.hpp>
#include <functional>
#include <vector>

#include "message-identifier.hpp"

namespace dory {
class WaitQuorumResponse {
 public:
  WaitQuorumResponse() = default;

  WaitQuorumResponse(quorum::Kind kind, std::vector<int> &remote_ids,
                     int quorum_size, uint64_t next_id);

  bool consume(std::vector<struct ibv_wc> &entries);
  // bool consume(std::vector<struct ibv_wc> &entries,
  //              const std::function<bool(int)> &iterator_complete);

  bool canContinueWith(uint64_t expected) const;
  // void resetTo(uint64_t next_id);
  // void resetTo(std::vector<int> retry_list, uint64_t next, int wait_for);

  int maximumResponses() const;

 private:
  void reset(uint64_t next);

 private:
  quorum::Kind kind;
  std::vector<uint64_t> scoreboard;
  int quorum_size;
  uint64_t next_id;
  int left;
};
}  // namespace dory

namespace dory {
class WaitModuloQuorumResponse {
 public:
  WaitModuloQuorumResponse() = default;

  WaitModuloQuorumResponse(quorum::Kind kind, std::vector<int> &remote_ids,
                           int quorum_size, int modulo, int64_t next_id);

  bool consume(std::vector<struct ibv_wc> &entries);
  // bool consume(std::vector<struct ibv_wc> &entries,
  //              const std::function<bool(int)> &iterator_complete);

  bool canContinueWith(int64_t expected) const;
  // void resetTo(int64_t next_id);
  // void resetTo(std::vector<int> retry_list, int64_t next, int wait_for);

  int maximumResponses() const;
  std::vector<int64_t> const &scores() const;

 private:
  void reset(int64_t next);

 private:
  quorum::Kind kind;
  std::vector<int64_t> scoreboard;
  int quorum_size;
  int modulo;
  int64_t next_id;
  int left;
};
}  // namespace dory