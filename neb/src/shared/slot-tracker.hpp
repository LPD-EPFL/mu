#pragma once

#include <atomic>
#include <cstdint>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <shared_mutex>

#include <dory/shared/branching.hpp>
#include <dory/shared/unused-suppressor.hpp>

#include "types.hpp"

class SlotTracker {
 public:
  SlotTracker() : sig_valid{false}, processed_sig{false} {}

  void add_to_empty_reads(int pid) {
    std::unique_lock lock(mux);

    empty_reads.insert(pid);
  }

  void add_to_match_reads(int pid) {
    std::unique_lock lock(mux);

    match_reads.insert(pid);
    // we potentially might upgrade the status, so ensure it's not present in
    // the empty-read set
    empty_reads.erase(pid);
  }

  bool quorum_includes(int pid) const {
    std::unique_lock lock(mux);

    return match_reads.find(pid) != match_reads.end() ||
           empty_reads.find(pid) != empty_reads.end();
  }

  bool match_reads_include(int pid) const {
    std::unique_lock lock(mux);

    return match_reads.find(pid) != match_reads.end();
  }

  bool has_quorum_of(size_t size) const {
    std::unique_lock lock(mux);

    return match_reads.size() + empty_reads.size() >= size;
  }

  bool has_matching_reads_of(size_t size) const {
    std::unique_lock lock(mux);

    return match_reads.size() >= size;
  }

  bool has_empty_reads_of(size_t size) const {
    std::unique_lock lock(mux);

    return empty_reads.size() >= size;
  }

  // TODO(Kristian): atomic bit-field might be faster?
  std::atomic<bool> sig_valid;
  std::atomic<bool> processed_sig;

 private:
  std::set<int> match_reads;
  std::set<int> empty_reads;
  mutable std::mutex mux;
};

class PendingSlots {
 public:
  std::reference_wrapper<SlotTracker> insert(uint64_t key) {
    std::unique_lock lock(mux);

    auto [it, ok] = tracker_map.try_emplace(key);

    assert(ok);

    return std::reference_wrapper<SlotTracker>(it->second);
  }

  std::optional<std::reference_wrapper<SlotTracker>> get(uint64_t key) {
    std::shared_lock lock(mux);

    auto it = tracker_map.find(key);

    if (unlikely(it == tracker_map.end())) {
      return std::nullopt;
    }

    return std::optional<std::reference_wrapper<SlotTracker>>(it->second);
  }

  bool remove_if_complete(uint64_t key, size_t quorum_size) {
    std::shared_lock lock(mux);

    auto it = tracker_map.find(key);

    if (likely(it != tracker_map.end())) {
      auto& tracker = it->second;
      if (tracker.has_matching_reads_of(quorum_size) ||
          (tracker.has_quorum_of(quorum_size) && tracker.sig_valid)) {
        // unlock shared_lock so we can remove;
        lock.unlock();
        return remove(key);
      }
    }
    return false;
  }

  /**
   * Garbage collets an entry that is not required anymore. As it locks the
   * mutex it simultaneously avoids data races between two threads trying to
   * deliver a slot. The return value should be seen as an indicator for the
   * thread if he got lucky to deliver the slot or if concurrently the racing
   * thread will handle that.
   **/
  bool remove(uint64_t key) {
    std::unique_lock lock(mux);

    auto it = tracker_map.find(key);

    if (it != tracker_map.end()) {
      tracker_map.erase(it);
      return true;
    }

    return false;
  }

  bool exist(uint64_t idx) {
    std::shared_lock lock(mux);

    return tracker_map.find(idx) != tracker_map.end();
  }

  std::vector<uint64_t> deliverable_after_remove_of(int pid,
                                                    size_t quorum_size) {
    std::shared_lock lock(mux);
    std::vector<uint64_t> deliverable;

    for (auto& [id, tracker] : tracker_map) {
      if ((tracker.has_quorum_of(quorum_size) &&
           !tracker.quorum_includes(pid) && tracker.sig_valid) ||
          (tracker.has_matching_reads_of(quorum_size) &&
           !tracker.match_reads_include(pid))) {
        deliverable.push_back(id);
      }
    }

    return deliverable;
  }

 private:
  std::map<uint64_t, SlotTracker> tracker_map;
  mutable std::shared_mutex mux;
};

class RemotePendingSlots {
 public:
  RemotePendingSlots(std::vector<int>& process_ids) {
    for (int pid : process_ids) {
      map[pid];
    }
  }

  auto& get(int pid) { return map[pid]; }

  auto begin() { return map.begin(); }

  auto end() { return map.end(); }

 private:
  std::map<int, PendingSlots> map;
};