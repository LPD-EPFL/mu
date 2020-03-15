#pragma once

#include <cstdint>
#include <set>

class MessageTracker {
 public:
  enum State {
    Initial = 0,
    Replayed = 1,
    Conflict = 2,
  };

  MessageTracker() : s(State::Initial), index(1) {}

  void transition(State state) { s = state; }

  void move_forward() {
    index++;
    quorum.clear();
    s = State::Initial;
  }

  State state() { return s; }

  uint64_t idx() { return index; }

  void add_to_quorum(int pid) { quorum.insert(pid); }

  inline bool has_quorum_of(size_t size) { return quorum.size() >= size; }

 private:
  State s;
  uint64_t index;
  std::set<int> quorum;
};
