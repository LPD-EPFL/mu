#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>

#include "log-helpers.hpp"

namespace dory {
class SnapshotIterator {
 public:
  SnapshotIterator(uint8_t* entry_ptr, size_t length);

  inline bool hasNext() { return entry_ptr < end_ptr; }
  SnapshotIterator& next();

  inline uint8_t* operator*() { return entry_ptr; }

 private:
  uint8_t* entry_ptr;
  uint8_t* end_ptr;
};
}  // namespace dory

namespace dory {
class BlockingIterator {
 public:
  BlockingIterator(uint8_t* entry_ptr);

  BlockingIterator& next();
  bool sampleNext();

  inline uint8_t* operator*() { return entry_ptr; }

 private:
  uint8_t* entry_ptr;
  ptrdiff_t increment;
};
}  // namespace dory

namespace dory {
class LiveIterator {
 public:
  LiveIterator(uint8_t* base_ptr, uint8_t* entry_ptr);

  bool hasNext(ptrdiff_t limit);

  LiveIterator& next(bool check = false);

  inline uint8_t* operator*() { return entry_ptr; }

 private:
  uint8_t* base_ptr;
  uint8_t* entry_ptr;
  ptrdiff_t increment;
};
}  // namespace dory

namespace dory {
class RemoteIterator {
 public:
  RemoteIterator(size_t entry_header_size = sizeof(uint64_t));

  RemoteIterator(int remote_id, size_t remote_offset, size_t entry_header_size);

  std::pair<ptrdiff_t, size_t> lookAt(uint64_t rem_offset);

  inline size_t offset() const { return remote_offset; }

  inline size_t size() const { return remote_size; }

  inline void storeDest(uint8_t* ptr) { base_ptr = ptr; }

  inline uint8_t* dest() { return base_ptr; }

  inline int remoteID() const { return remote_id; }

  inline size_t remoteOffset() const { return remote_offset; }
  inline size_t previousRemoteOffset() const { return prev_remote_offset; }

  bool isPopulated(uint8_t* data, size_t length);

  bool canMove(uint8_t* data, size_t length);

  bool isEntryComplete(uint8_t* data, size_t length);

 private:
  int remote_id;
  size_t remote_offset, prev_remote_offset;
  size_t remote_size;
  uint8_t* base_ptr;
  LengthPredictor predictor;
};
}  // namespace dory