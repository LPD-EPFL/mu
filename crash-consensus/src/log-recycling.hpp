#pragma once

#include <cstdint>
#include <vector>

#include "context.hpp"
#include "message-identifier.hpp"

namespace dory {
struct LogRecyclingRequest {
  int requestor;
  uint64_t request_id;
  uint64_t commit_up_to;

  LogRecyclingRequest() {}

  LogRecyclingRequest(int requestor, uint64_t request_id, uint64_t commit_up_to)
      : requestor(requestor),
        request_id(request_id),
        commit_up_to(commit_up_to) {}
};

class LogRecycling {
 public:
  LogRecycling(ReplicationContext *context, ScratchpadMemory &scratchpad)
      : r_ctx{context},
        c_ctx{&context->cc},
        scratchpad{scratchpad},
        req_nr(c_ctx->my_id) {
    modulo = Identifiers::maxID(c_ctx->my_id, c_ctx->remote_ids);
  }

  LogRecyclingRequest generateRequest(uint64_t fuo) {
    LogRecyclingRequest req(c_ctx->my_id, req_nr, fuo);
    req_nr += modulo;
    return req;
  }

  bool waitForReplies(/*Leader current_leader, std::atomic<Leader> &leader*/) {
    auto &slots = scratchpad.readLogRecyclingSlots();
    auto ids = c_ctx->remote_ids;

    while (true) {
      int eliminated_one = -1;
      for (size_t i = 0; i < ids.size(); i++) {
        auto pid = ids[i];
        uint64_t volatile *temp = reinterpret_cast<uint64_t *>(slots[pid]);
        uint64_t val = *temp;

        if (val + modulo == req_nr) {
          eliminated_one = i;
          break;
        }
      }

      if (eliminated_one >= 0) {
        ids[eliminated_one] = ids[ids.size() - 1];
        ids.pop_back();

        if (ids.empty()) {
          return true;
        }
      }

      /*
        if (leader.load().requester != current_leader.requester) {
          return false;
        }
        */
    }
  }

 private:
  ReplicationContext *r_ctx;
  ConnectionContext *c_ctx;
  ScratchpadMemory &scratchpad;
  uint64_t req_nr;
  int modulo;
};

}  // namespace dory