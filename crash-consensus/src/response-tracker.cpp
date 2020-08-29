
#include "response-tracker.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>

#include "message-identifier.hpp"

namespace dory {
WaitQuorumResponse::WaitQuorumResponse(quorum::Kind kind,
                                       std::vector<int>& remote_ids,
                                       int quorum_size, uint64_t next_id)
    : kind{kind},
      quorum_size{quorum_size},
      next_id{next_id},
      left{quorum_size} {
  auto max_elem = Identifiers::maxID(remote_ids);
  scoreboard.resize(max_elem + 1);

  if (next_id == 0) {
    throw std::runtime_error("`next_id` must be positive");
  }

  for (auto& elem : scoreboard) {
    elem = next_id - 1;
  }
}

// void WaitQuorumResponse::resetTo(std::vector<int> retry_list, uint64_t next,
//                                  int wait_for) {
//   for (auto& idx : retry_list) {
//     scoreboard[idx] = next - 1;
//   }

//   next_id = next;
//   left = wait_for;
// }

// void WaitQuorumResponse::resetTo(uint64_t next_id) {
//   reset(next_id);
//   left = quorum_size;
// }

void WaitQuorumResponse::reset(uint64_t next) {
  if (next == 0) {
    throw std::runtime_error("`next_id` must be positive");
  }

  for (auto& elem : scoreboard) {
    elem = next - 1;
  }

  next_id = next;
}

bool WaitQuorumResponse::consume(std::vector<struct ibv_wc>& entries) {
  auto ret = true;
  for (auto const& entry : entries) {
    if (entry.status != IBV_WC_SUCCESS) {
      ret = false;
    } else {
      auto [k, pid, seq] = quorum::unpackAll<uint64_t, uint64_t>(entry.wr_id);

      if (k != kind) {
        std::cout << "Received unexpected/old message response ("
                  << quorum::type_str(k) << ") instead of ("
                  << quorum::type_str(kind) << ") from the completion queue"
                  << std::endl;
        continue;
      }

      auto current_seq = scoreboard[pid];
      scoreboard[pid] = current_seq == seq - 1 ? seq : 0;

      if (scoreboard[pid] == next_id) {
        left -= 1;
      }

      if (left == 0) {
        left = quorum_size;
        next_id += 1;
      }
    }
  }

  return ret;
}

// bool WaitQuorumResponse::consume(
//     std::vector<struct ibv_wc>& entries,
//     const std::function<bool(int)>& iterator_complete) {
//   auto ret = true;
//   for (auto const& entry : entries) {
//     if (entry.status != IBV_WC_SUCCESS) {
//       ret = false;
//     } else {
//       auto [k, pid, seq] = quorum::unpackAll<uint64_t,
//       uint64_t>(entry.wr_id);

//       if (k != kind) {
//         std::cout << "Mismatched message kind: (" << k << " vs " << kind
//                   << "). Received unexpected/old message response ("
//                   << quorum::type_str(k) << ") instead of ("
//                   << quorum::type_str(kind) << ") from the completion queue"
//                   << std::endl;
//         continue;
//       }

//       // Check if the iterator of this particular entry says that we can move
//       if (!iterator_complete(pid)) {
//         std::cout << "Have to retry for process " << pid << std::endl;
//       }

//       auto current_seq = scoreboard[pid];
//       scoreboard[pid] = current_seq == seq - 1 ? seq : 0;

//       if (scoreboard[pid] == next_id) {
//         left -= 1;
//       }

//       if (left == 0) {
//         left = quorum_size;
//         next_id += 1;
//       }
//     }
//   }

//   return ret;
// }

bool WaitQuorumResponse::canContinueWith(uint64_t expected) const {
  return next_id == expected;
}

int WaitQuorumResponse::maximumResponses() const {
  // The number of processes that can go to the next round:
  return std::count_if(scoreboard.begin(), scoreboard.end(),
                       [this](uint64_t i) { return i == next_id - 1; });
}
}  // namespace dory

namespace dory {
WaitModuloQuorumResponse::WaitModuloQuorumResponse(quorum::Kind kind,
                                                   std::vector<int>& remote_ids,
                                                   int quorum_size, int modulo,
                                                   int64_t next_id)
    : kind{kind},
      quorum_size{quorum_size},
      modulo{modulo},
      next_id{next_id},
      left{quorum_size} {
  auto max_elem = Identifiers::maxID(remote_ids);
  scoreboard.resize(max_elem + 1);

  if (next_id == 0) {
    throw std::runtime_error("`next_id` must be positive");
  }

  for (auto& elem : scoreboard) {
    elem = next_id - modulo;
  }
}

// void WaitModuloQuorumResponse::resetTo(std::vector<int> retry_list,
//                                        int64_t next, int wait_for) {
//   for (auto& idx : retry_list) {
//     scoreboard[idx] = next - modulo;
//   }

//   next_id = next;
//   left = wait_for;
// }

// void WaitModuloQuorumResponse::resetTo(int64_t next_id) {
//   reset(next_id);
//   left = quorum_size;
// }

void WaitModuloQuorumResponse::reset(int64_t next) {
  if (next == 0) {
    throw std::runtime_error("`next_id` must be positive");
  }

  for (auto& elem : scoreboard) {
    elem = next - modulo;
  }

  next_id = next;
}

bool WaitModuloQuorumResponse::consume(std::vector<struct ibv_wc>& entries) {
  auto ret = true;
  for (auto const& entry : entries) {
    if (entry.status != IBV_WC_SUCCESS) {
      ret = false;
    } else {
      auto [k, pid, seq] = quorum::unpackAll<int64_t, int64_t>(entry.wr_id);

      if (k != kind) {
        std::cout << "Mismatched message kind: (" << k << " vs " << kind
                  << "). Received unexpected/old message response from the "
                     "completion queue"
                  << std::endl;
        continue;
      }

      auto current_seq = scoreboard[pid];
      scoreboard[pid] = next_id - current_seq == modulo ? seq : 0;

      if (scoreboard[pid] == next_id) {
        left -= 1;
      }

      if (left == 0) {
        left = quorum_size;
        next_id += modulo;
      }
    }
  }

  return ret;
}

// bool WaitModuloQuorumResponse::consume(
//     std::vector<struct ibv_wc>& entries,
//     const std::function<bool(int)>& iterator_complete) {
//   auto ret = true;
//   for (auto const& entry : entries) {
//     if (entry.status != IBV_WC_SUCCESS) {
//       ret = false;
//     } else {
//       auto [k, pid, seq] = quorum::unpackAll<int64_t, int64_t>(entry.wr_id);

//       if (k != kind) {
//         std::cout << "Mismatched message kind: (" << k << " vs " << kind
//                   << "). Received unexpected/old message response from the "
//                      "completion queue"
//                   << std::endl;
//         continue;
//       }

//       // Check if the iterator of this particular entry says that we can move
//       if (!iterator_complete(pid)) {
//         std::cout << "Have to retry for process " << pid << std::endl;
//       }

//       auto current_seq = scoreboard[pid];
//       scoreboard[pid] = next_id - current_seq == modulo ? seq : 0;

//       if (scoreboard[pid] == next_id) {
//         left -= 1;
//       }

//       if (left == 0) {
//         left = quorum_size;
//         next_id += modulo;
//       }
//     }
//   }

//   return ret;
// }

bool WaitModuloQuorumResponse::canContinueWith(int64_t expected) const {
  return next_id == expected;
}

int WaitModuloQuorumResponse::maximumResponses() const {
  // The number of processes that can go to the next round:
  return std::count_if(scoreboard.begin(), scoreboard.end(),
                       [this](int64_t i) { return i == next_id - modulo; });
}

std::vector<int64_t> const& WaitModuloQuorumResponse::scores() const {
  return scoreboard;
}
}  // namespace dory
