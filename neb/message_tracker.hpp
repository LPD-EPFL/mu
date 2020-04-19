#pragma once

#include <cstdint>
#include <set>

class MessageTracker {
 public:
  enum State {
    Conflict = 2,
  };

  MessageTracker() {}

  void add_to_quorum(int pid) { quorum.insert(pid); }

  bool includes(int pid) const { return quorum.find(pid) != quorum.end(); }

  inline bool has_quorum_of(size_t size) const { return quorum.size() >= size; }

 private:
  std::set<int> quorum;
};
