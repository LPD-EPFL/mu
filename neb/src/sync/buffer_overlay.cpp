#include <algorithm>
#include <limits>

#include <dory/shared/branching.hpp>

#include "buffer_overlay.hpp"

using namespace dory::neb;

BroadcastBuffer::BroadcastBuffer(uintptr_t addr, uint64_t buf_size,
                                 uint32_t lkey)
    : lkey(lkey),
      buf(reinterpret_cast<volatile uint8_t *const>(addr)),
      num_entries(buf_size / MEMORY_SLOT_SIZE) {}

uint64_t BroadcastBuffer::get_byte_offset(uint64_t index) const {
  assert(index != 0);

  // internally we start indexing from 0
  index -= 1;

  if (unlikely(index >= num_entries)) {
    throw std::out_of_range(
        "Attempt to access memory outside of the buffer space");
  }

  return index * MEMORY_SLOT_SIZE;
}

MemorySlot BroadcastBuffer::slot(uint64_t index) const {
  return MemorySlot(&buf[get_byte_offset(index)]);
}

size_t BroadcastBuffer::write(uint64_t index, uint64_t k,
                              dory::neb::Broadcastable &msg) {
  auto raw = &buf[get_byte_offset(index)];

  *reinterpret_cast<volatile uint64_t *>(&raw[dory::neb::MSG_PAYLOAD_SIZE]) = k;

  return msg.marshall(reinterpret_cast<volatile void *>(&raw[0]));
}

ReplayBuffer::ReplayBuffer(uintptr_t addr, size_t buf_size,
                           std::vector<int> procs)
    : buf(reinterpret_cast<volatile const uint8_t *const>(addr)),
      num_entries_per_proc(buf_size / MEMORY_SLOT_SIZE / procs.size()) {
  for (size_t i = 0; i < procs.size(); i++) {
    process_index.insert(std::pair<int, size_t>(procs[i], i));
  }

  const auto num_entries = buf_size / MEMORY_SLOT_SIZE;
  const auto p = reinterpret_cast<uint8_t *>(addr);

  for (uint64_t i = 0; i < num_entries; i++) {
    const auto p2 = reinterpret_cast<uint64_t *>(
        &p[i * MEMORY_SLOT_SIZE + MSG_PAYLOAD_SIZE]);
    // so we can distinguish an empty read from an unsuccessful read
    p2[0] = std::numeric_limits<uint64_t>::max();
  }
}

uint64_t ReplayBuffer::get_byte_offset(int proc_id, uint64_t index) const {
  assert(index != 0);

  // internally we start indexing from 0
  index -= 1;

  if (unlikely(index >= num_entries_per_proc)) {
    throw std::out_of_range(
        "Attempt to access memory outside of the buffer space");
  }

  auto p_index = process_index.find(proc_id)->second;

  return p_index * num_entries_per_proc * MEMORY_SLOT_SIZE +
         index * MEMORY_SLOT_SIZE;
}

MemorySlot ReplayBuffer::slot(int proc_id, uint64_t index) const {
  return MemorySlot(&buf[get_byte_offset(proc_id, index)]);
}
