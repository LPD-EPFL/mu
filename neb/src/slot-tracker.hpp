#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <set>

#include <dory/shared/unused-suppressor.hpp>

class SlotTracker {
 public:
  SlotTracker()
      : needs_valid_local_sig{true},
        sig_valid{false},
        processed_sig{false},
        delivered{false} {}

  void add_to_empty_reads(int pid) {
    std::unique_lock lock(mux);

    empty_reads.insert(pid);
  }

  void add_to_match_reads(int pid) {
    std::unique_lock lock(mux);

    match_reads.insert(pid);
  }

  void add_to_conflicts(int pid) {
    std::unique_lock lock(mux);

    conflicts.insert(pid);
  }

  bool quorum_includes(int pid) const {
    std::shared_lock lock(mux);

    return match_reads.find(pid) != match_reads.end() ||
           empty_reads.find(pid) != empty_reads.end();
  }

  bool conflict_includes(int pid) const {
    std::shared_lock lock(mux);

    return conflicts.find(pid) != conflicts.end();
  }

  bool has_quorum_of(size_t size) const {
    std::shared_lock lock(mux);

    return match_reads.size() + empty_reads.size() >= size;
  }

  bool has_matching_reads_of(size_t size) const {
    std::shared_lock lock(mux);

    return match_reads.size() >= size;
  }

  bool has_empty_reads_of(size_t size) const {
    std::shared_lock lock(mux);

    return empty_reads.size() >= size;
  }

  std::atomic<bool> needs_valid_local_sig;
  std::atomic<bool> sig_valid;
  std::atomic<bool> processed_sig;
  std::atomic<bool> delivered;

 private:
  std::set<int> match_reads;
  std::set<int> empty_reads;
  std::set<int> conflicts;
  mutable std::shared_mutex mux;
};

class PendingSlots {
 public:
  std::reference_wrapper<SlotTracker> insert(uint64_t key) {
    std::unique_lock lock(mux);

    auto [it, ok] = map.try_emplace(key, std::make_unique<SlotTracker>());

    dory::IGNORE(ok);

    return std::reference_wrapper<SlotTracker>(*(it->second));
  }

  std::optional<std::reference_wrapper<SlotTracker>> get(uint64_t key) {
    std::shared_lock lock(mux);

    auto it = map.find(key);
    if (it == map.end()) {
      return std::nullopt;
    }

    return std::optional<std::reference_wrapper<SlotTracker>>(*(it->second));
  }

  bool remove(uint64_t key) {
    std::unique_lock lock(mux);

    auto it = map.find(key);

    if (it != map.end()) {
      map.erase(it);
      return true;
    }

    return false;
  }

  using map_entry_refs =
      std::vector<std::pair<const uint64_t&, std::unique_ptr<SlotTracker>&>>;

  map_entry_refs deliverable_after_remove_of(int pid, size_t quorum_size) {
    std::shared_lock lock(mux);
    map_entry_refs deliverable;

    for (auto& [id, tracker] : map) {
      if (tracker->has_quorum_of(quorum_size) &&
          !tracker->quorum_includes(pid)) {
        deliverable.push_back({id, tracker});
      }
    }

    return deliverable;
  }

 private:
  std::map<uint64_t, std::unique_ptr<SlotTracker>> map;
  std::shared_mutex mux;
};

class RemotePendingSlots {
 public:
  RemotePendingSlots(std::vector<int>& remote_ids) {
    for (int pid : remote_ids) {
      map[pid];
    }
  }

  auto& get(int pid) { return map[pid]; }

  auto begin() { return map.begin(); }

  auto end() { return map.end(); }

 private:
  std::map<int, PendingSlots> map;
};