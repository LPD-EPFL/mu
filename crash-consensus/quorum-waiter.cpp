
#include "quorum-waiter.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>

#include "branching.hpp"
#include "message-identifier.hpp"

namespace dory {
template <class ID>
SerialQuorumWaiter<ID>::SerialQuorumWaiter(quorum::Kind kind,
                                           std::vector<int>& remote_ids,
                                           int quorum_size, ID next_id,
                                           ID modulo)
    : kind{kind},
      quorum_size{quorum_size},
      next_id{next_id},
      left{quorum_size},
      modulo(modulo) {
  auto max_elem = Identifiers::maxID(remote_ids);
  scoreboard.resize(max_elem + 1);

  if (next_id == 0) {
    throw std::runtime_error("`next_id` must be positive");
  }

  for (auto& elem : scoreboard) {
    elem = next_id - modulo;
  }
}

template <class ID>
void SerialQuorumWaiter<ID>::reset(ID next) {
  if (next == 0) {
    throw std::runtime_error("`next_id` must be positive");
  }

  for (auto& elem : scoreboard) {
    elem = next - modulo;
  }

  next_id = next;
}

template <class ID>
bool SerialQuorumWaiter<ID>::consume(std::vector<struct ibv_wc>& entries,
                                     std::vector<int>& successful_ops) {
  auto ret = true;
  for (auto const& entry : entries) {
    if (entry.status != IBV_WC_SUCCESS) {
      ret = false;
    } else {
      auto [k, pid, seq] = quorum::unpackAll<uint64_t, ID>(entry.wr_id);

      if (k != kind) {
#ifdef NDEBUG
        std::cout << "Received unexpected (" << quorum::type_str(k)
                  << " instead of " << quorum::type_str(kind) << ")"
                  << " message from the completion queue, concerning process "
                  << pid << std::endl;
#endif

        continue;
      }

#ifdef NDEBUG
      if (seq != next_id) {
        std::cout << "Received remnant (" << seq << " instead of " << next_id
                  << ") message from the completion queue, concerning process "
                  << pid << std::endl;
      }
#endif

      auto current_seq = scoreboard[pid];
      scoreboard[pid] = current_seq + modulo == seq ? seq : 0;

      if (scoreboard[pid] == next_id) {
        left -= 1;
        successful_ops.push_back(pid);
      }

      if (left == 0) {
        left = quorum_size;
        next_id += modulo;
      }
    }
  }

  return ret;
}

template <class ID>
bool SerialQuorumWaiter<ID>::fastConsume(std::vector<struct ibv_wc>& entries,
                                         int num, int& ret_left) {
  for (int i = 0; i < num; i++) {
    auto& entry = entries[i];
    if (entry.status != IBV_WC_SUCCESS) {
      return false;
    } else {
      auto [k, pid, seq] = quorum::unpackAll<uint64_t, ID>(entry.wr_id);

      if (k != kind) {
#ifdef NDEBUG
        std::cout << "Received unexpected (" << quorum::type_str(k)
                  << " instead of " << quorum::type_str(kind) << ")"
                  << " message from the completion queue, concerning process "
                  << pid << std::endl;
#endif

        continue;
      }

#ifdef NDEBUG
      if (seq != next_id) {
        std::cout << "Received remnant (" << seq << " instead of " << next_id
                  << ") message from the completion queue, concerning process "
                  << pid << std::endl;
      }
#endif

      auto current_seq = scoreboard[pid];
      scoreboard[pid] = current_seq + modulo == seq ? seq : 0;

      if (scoreboard[pid] == next_id) {
        left -= 1;
        ret_left = left;
      }

      if (left == 0) {
        left = quorum_size;
        next_id += modulo;
      }
    }
  }

  return true;
}

template <class ID>
inline bool SerialQuorumWaiter<ID>::canContinueWith(ID expected) const {
  return next_id == expected;
}

template <class ID>
int SerialQuorumWaiter<ID>::maximumResponses() const {
  // The number of processes that can go to the next round:
  return std::count_if(scoreboard.begin(), scoreboard.end(),
                       [this](ID i) { return i + modulo == next_id; });
}
}  // namespace dory

// Instantiations
namespace dory {
template class SerialQuorumWaiter<uint64_t>;
template class SerialQuorumWaiter<int64_t>;
}  // namespace dory