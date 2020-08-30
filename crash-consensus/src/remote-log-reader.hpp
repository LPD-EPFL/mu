#pragma once

#include <algorithm>
#include <functional>
#include <iostream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <vector>

#include <dory/extern/ibverbs.hpp>

#include "error.hpp"
#include "log.hpp"
#include "message-identifier.hpp"

namespace dory {
class WrappedRemoteIterator {
 public:
  WrappedRemoteIterator() {}
  WrappedRemoteIterator(RemoteIterator iter) : iter{iter} {}

  RemoteIterator& iterator() { return iter; }

  bool isPopulated() { return iter.isPopulated(iter.dest(), iter.size()); }

  bool isComplete() {
    if (iter.canMove(iter.dest(), iter.size())) {
      if (!iter.isEntryComplete(iter.dest(), iter.size())) {
        return false;
      }
      return true;
    }

    return false;
  }

 private:
  RemoteIterator iter;
};
}  // namespace dory

namespace dory {
class ToBePolled {
 public:
  using ResponseRef = std::reference_wrapper<std::vector<int>>;

  ToBePolled() {}
  ToBePolled(quorum::Kind kind, std::vector<int>& remote_ids) : kind{kind} {
    to_be_posted = std::set<int>(remote_ids.begin(), remote_ids.end());
    positive_responses.resize(remote_ids.size());
    negative_responses.resize(remote_ids.size());
  }

  void focusOnReqID(uint64_t request_id) { req_id = request_id; }

  std::set<int> const& postList() const { return to_be_posted; }

  std::set<int> const& pollList() const { return to_be_polled; }

  std::set<int> const& completedList() const { return completed; }

  void posted() {
    to_be_polled.insert(to_be_posted.begin(), to_be_posted.end());
    to_be_posted.clear();
  }

  // std::vector<int> const& actuallyPolled(std::vector<struct ibv_wc>& entries)
  // {
  std::pair<ResponseRef, ResponseRef> actuallyPolled(
      std::vector<struct ibv_wc>& entries) {
    positive_responses.clear();
    negative_responses.clear();
    for (auto const& entry : entries) {
      auto [k, pid, seq] = quorum::unpackAll<int, uint64_t>(entry.wr_id);

      // Additional check: Exists in `to_be_polled`? This way, upon failure,
      // where we remove the remoteID from both sets, we stop processing
      // elements from this remoteID that may come in the future.
      if (to_be_polled.find(pid) == to_be_polled.end()) {
        continue;
      }

      if (entry.status != IBV_WC_SUCCESS) {
        // Do not post on this remoteID again
        to_be_posted.erase(pid);
        to_be_polled.erase(pid);
        completed.erase(pid);

        negative_responses.push_back(pid);
        continue;
      }

      if (k != kind || seq != req_id) {
#ifndef NDEBUG
        std::cout << "Received unexpected (" << quorum::type_str(k)
                  << " instead of " << quorum::type_str(kind) << ")"
                  << " or remnant (" << seq << " instead of " << req_id
                  << ") message from the completion queue, concerning process "
                  << pid << std::endl;
#endif

        to_be_posted.insert(pid);
        to_be_polled.erase(pid);
        continue;
      }

      to_be_posted.insert(pid);
      to_be_polled.erase(pid);
      positive_responses.push_back(pid);
    }

    return std::make_pair(ResponseRef(positive_responses),
                          ResponseRef(negative_responses));
    // return positive_responses;
  }

  void moveToCompleted(int remoteID) {
    if (to_be_polled.find(remoteID) != to_be_polled.end()) {
      throw std::runtime_error("Can only move message readry to be posted");
    }

    if (to_be_posted.find(remoteID) == to_be_posted.end()) {
      throw std::runtime_error("Nothig to move");
    }

    completed.insert(remoteID);
    to_be_posted.erase(remoteID);
  }

  void rescheduleCompleted() {
    to_be_posted.insert(completed.begin(), completed.end());
    completed.clear();
  }

 private:
  quorum::Kind kind;
  uint64_t req_id;
  std::set<int> completed;
  std::set<int> to_be_posted;
  std::set<int> to_be_polled;
  std::vector<int> positive_responses, negative_responses;
};
}  // namespace dory