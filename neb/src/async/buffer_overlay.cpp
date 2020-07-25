#include <algorithm>
#include <limits>

#include "buffer_overlay.hpp"

using namespace dory::neb;

BroadcastBuffer::BroadcastBuffer(uintptr_t addr, uint64_t buf_size,
                                 uint32_t lkey, std::vector<int> proc_ids)
    : lkey(lkey),
      buf(reinterpret_cast<volatile uint8_t *const>(addr)),
      num_entries_per_proc(buf_size / proc_ids.size() / MEMORY_SLOT_SIZE) {
  for (size_t i = 0; i < proc_ids.size(); i++) {
    proc_idx[proc_ids[i]] = i;
  }
}

uint64_t BroadcastBuffer::get_byte_offset(int proc_id, uint64_t index) const {
  if (index == 0) {
    throw std::out_of_range("Indexing starts at 1");
  }

  // internally we start indexing from 0
  index -= 1;

  if (index >= num_entries_per_proc) {
    throw std::out_of_range(
        "Attempt to access memory outside of the buffer space");
  }

  auto slot_offset = index * MEMORY_SLOT_SIZE;
  auto proc_offset =
      proc_idx.find(proc_id)->second * num_entries_per_proc * MEMORY_SLOT_SIZE;

  return slot_offset + proc_offset;
}

MemorySlot BroadcastBuffer::slot(int proc_id, uint64_t index) const {
  return MemorySlot(&buf[get_byte_offset(proc_id, index)]);
}

size_t BroadcastBuffer::write(int proc_id, uint64_t index, uint64_t k,
                              dory::neb::Broadcastable &msg) {
  auto raw = &buf[get_byte_offset(proc_id, index)];

  *reinterpret_cast<volatile uint64_t *>(&raw[dory::neb::MSG_PAYLOAD_SIZE]) = k;

  return msg.marshall(reinterpret_cast<volatile void *>(&raw[0]));
}

std::mutex &BroadcastBuffer::get_mux(int proc_id, uint64_t index) {
  std::unique_lock lock(map_mux);
  return *(muxes[proc_id]
               .try_emplace(index, std::make_unique<std::mutex>())
               .first->second);
}