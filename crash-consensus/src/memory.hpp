#pragma once

#include <cstdint>
#include <tuple>
#include <utility>
#include <vector>

#include "logger.hpp"

namespace dory {
struct Memory {
  Memory() : Memory(1) {}

  Memory(size_t alignment) : ptr{nullptr}, size{0}, alignment{alignment} {}

  Memory(uint8_t *ptr, size_t size, size_t alignment)
      : ptr{ptr}, size{size}, alignment{alignment} {}

  uint8_t *ptr;
  size_t size;
  size_t alignment;
};

class OverlayAllocator {
 public:
  OverlayAllocator(void *buf, size_t size);
  inline void *base() { return buf; }

  std::pair<bool, uint8_t *> allocate(size_t length, size_t alignment);
  std::tuple<bool, uint8_t *, size_t> allocateRemaining(size_t alignment);

 private:
  size_t remaining();
  uint8_t *next_aligned(size_t alignment);

 private:
  uint8_t *buf;
  size_t size;
  std::vector<std::pair<uint8_t *, size_t>> allocations;
};

class ScratchpadMemory {
 public:
  ScratchpadMemory(std::vector<int> &ids, OverlayAllocator &overlay,
                   int alignment);

  size_t requiredSize() const;
  inline size_t slotSize() const;

  // Add more entries here
  std::vector<uint8_t *> &readFUOSlots();
  std::vector<uint8_t *> &readProposalNrSlots();
  std::vector<uint8_t *> &readLogEntrySlots();
  std::vector<uint8_t *> &readLeaderChangeSlots();
  std::vector<uint8_t *> &writeLeaderChangeSlots();
  std::vector<uint8_t *> &readLogRecyclingSlots();
  uint8_t *logRecyclingResponseSlot();
  uint8_t *writeSlot();
  uint8_t *leaderRequestSlot();
  uint8_t *leaderResponseSlot();
  uint8_t *leaderHeartbeatSlot();
  std::vector<uint8_t *> &readLeaderHeartbeatSlots();

  // Add more entries here
  std::vector<ptrdiff_t> &readFUOSlotsOffsets();
  std::vector<ptrdiff_t> &readProposalNrSlotsOffsets();
  std::vector<ptrdiff_t> &readLogEntrySlotsOffsets();
  std::vector<ptrdiff_t> &readLeaderChangeSlotsOffsets();
  std::vector<ptrdiff_t> &writeLeaderChangeSlotsOffsets();
  std::vector<ptrdiff_t> &readLogRecyclingSlotsOffsets();
  ptrdiff_t logRecyclingResponseSlotOffset();
  ptrdiff_t writeSlotOffset();
  ptrdiff_t leaderRequestSlotOffset();
  ptrdiff_t leaderResponseSlotOffset();
  ptrdiff_t leaderHeartbeatSlotOffset();
  std::vector<ptrdiff_t> &readLeaderHeartbeatSlotsOffsets();

 private:
  ScratchpadMemory(std::vector<int> &ids, Memory const &mem);
  void setup();

  // Add more entries here
  void setupReadFUOSlots();
  void setupReadProposalNrSlots();
  void setupReadLogEntrySlots();
  void setupReadLeaderChangeSlots();
  void setupWriteLeaderChangeSlots();
  void setupReadLogRecyclingSlots();
  void setupLogRecyclingResponseSlot();
  void setupWriteSlot();
  void setupLeaderRequestSlot();
  void setupLeaderResponseSlot();
  void setupLeaderHeartbeatSlot();
  void setupReadLeaderHeartbeatSlots();

  void setupSlots(std::vector<uint8_t *> &slots,
                  std::vector<ptrdiff_t> &offsets);
  void setupSlot(uint8_t *&slot, ptrdiff_t &offset);
  uint8_t *align_up(uint8_t *ptr);

 private:
  int max_id;

  // Add more entries here
  std::vector<uint8_t *> read_fuo_slots;
  std::vector<uint8_t *> read_proposal_nr_slots;
  std::vector<uint8_t *> read_log_entry_slots;
  std::vector<uint8_t *> read_leader_change_slots;
  std::vector<uint8_t *> write_leader_change_slots;
  std::vector<uint8_t *> read_log_recycling_slots;
  uint8_t *log_recycling_response_slot;
  uint8_t *write_slot;
  uint8_t *leader_req_slot;
  uint8_t *leader_resp_slot;
  uint8_t *leader_heartbeat_slot;
  std::vector<uint8_t *> read_leader_heartbeat_slots;

  // Add more entries here
  std::vector<ptrdiff_t> read_fuo_slots_offsets;
  std::vector<ptrdiff_t> read_proposal_nr_slots_offsets;
  std::vector<ptrdiff_t> read_log_entry_slots_offsets;
  std::vector<ptrdiff_t> read_leader_change_slots_offsets;
  std::vector<ptrdiff_t> write_leader_change_slots_offsets;
  std::vector<ptrdiff_t> read_log_recycling_slots_offsets;
  ptrdiff_t log_recycling_response_slot_offset;
  ptrdiff_t write_slot_offset;
  ptrdiff_t leader_req_slot_offset;
  ptrdiff_t leader_resp_slot_offset;
  ptrdiff_t leader_heartbeat_slot_offset;
  std::vector<ptrdiff_t> read_leader_heartbeat_slots_offsets;

  Memory mem;
  uint8_t *next;
  uint8_t *base;

  LOGGER_DECL(logger);
};
}  // namespace dory