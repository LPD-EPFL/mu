#pragma once

#include <dory/conn/exchanger.hpp>
#include <dory/ctrl/block.hpp>
#include <dory/shared/pointer-wrapper.hpp>
#include <vector>

#include <atomic>
#include "log.hpp"

namespace dory {
struct Leader {
  Leader(int requester, uint64_t requester_value, int force_reset = 0) noexcept
      : used(1), force_reset(force_reset), requester(requester), requester_value(requester_value) {}

  Leader() noexcept : used(0), force_reset(0), requester(0), requester_value(0) {}

  uint8_t used : 1;
  uint8_t force_reset : 1;
  uint8_t requester : 6;
  uint64_t requester_value : 48;

  inline bool operator==(Leader const &rhs) {
    return requester == rhs.requester && requester_value == rhs.requester_value;
  }

  inline bool unused() { return used == 0; }
  inline void makeUnused() { used = 0; }
  inline bool reset() { return force_reset != 0; }

  inline bool operator!=(Leader const &rhs) { return !(*this == rhs); }
};

struct ConnectionContext {
  ConnectionContext(ControlBlock &cb, ConnectionExchanger &ce,
                    deleted_unique_ptr<struct ibv_cq> &cq,
                    std::vector<int> &remote_ids, int my_id)
      : cb{cb}, ce{ce}, cq{cq}, remote_ids{remote_ids}, my_id{my_id} {}
  ControlBlock &cb;
  ConnectionExchanger &ce;
  deleted_unique_ptr<struct ibv_cq> &cq;
  std::vector<int> &remote_ids;
  int my_id;
};

struct ReplicationContext {
  ReplicationContext(ConnectionContext &cc, Log &log, ptrdiff_t log_offset)
      : cc{cc}, log{log}, log_offset{log_offset} {}
  ConnectionContext &cc;
  Log &log;
  ptrdiff_t log_offset;
};
}  // namespace dory