#include <algorithm>
#include <limits>

#include "buffer_overlay.hpp"

using namespace dory::neb;

BroadcastBuffer::BroadcastBuffer(uintptr_t addr, uint64_t buf_size,
                                 uint32_t lkey)
    : lkey(lkey),
      buf(reinterpret_cast<volatile uint8_t *const>(addr)),
      num_entries(buf_size / MEMORY_SLOT_SIZE) {}

uint64_t BroadcastBuffer::get_byte_offset(uint64_t index) const {
  if (index == 0) {
    throw std::out_of_range("Indexing starts at 1");
  }

  // internally we start indexing from 0
  index -= 1;

  if (index >= num_entries) {
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
  auto raw =
      reinterpret_cast<volatile uint64_t *>(&buf[get_byte_offset(index)]);

  raw[0] = k;

  return msg.marshall(reinterpret_cast<volatile void *>(&raw[1]));
}

/* --------------------------------------------------------------------------
 */

ReplayBufferWriter::ReplayBufferWriter(uintptr_t addr, size_t buf_size,
                                       std::vector<int> procs)
    : buf(reinterpret_cast<volatile const uint8_t *const>(addr)),
      num_entries_per_proc(buf_size / MEMORY_SLOT_SIZE / procs.size()) {
  std::sort(std::begin(procs), std::end(procs));

  for (size_t i = 0; i < procs.size(); i++) {
    process_index.insert(std::pair<int, size_t>(procs[i], i));
  }

  const auto num_entries = buf_size / MEMORY_SLOT_SIZE;
  const auto p = reinterpret_cast<uint8_t *>(addr);

  for (uint64_t i = 0; i < num_entries; i++) {
    const auto p2 = reinterpret_cast<uint64_t *>(&p[i * MEMORY_SLOT_SIZE]);
    // so we can distinguish an empty read from an unsuccessful read
    p2[0] = std::numeric_limits<uint64_t>::max();
  }
}

uint64_t ReplayBufferWriter::get_byte_offset(int proc_id,
                                             uint64_t index) const {
  if (index == 0) {
    throw std::out_of_range("Indexing starts at 1");
  }

  // internally we start indexing from 0
  index -= 1;

  if (index >= num_entries_per_proc) {
    throw std::out_of_range(
        "Attempt to access memory outside of the buffer space");
  }

  auto p_index = process_index.find(proc_id)->second;

  return p_index * num_entries_per_proc * MEMORY_SLOT_SIZE +
         index * MEMORY_SLOT_SIZE;
}

MemorySlot ReplayBufferWriter::slot(int proc_id, uint64_t index) const {
  return MemorySlot(&buf[get_byte_offset(proc_id, index)]);
}

/* -------------------------------------------------------------------------- */
ReplayBufferReader::ReplayBufferReader(std::vector<int> remote_ids,
                                       uintptr_t addr, uint32_t lkey)
    : lkey(lkey), buf(reinterpret_cast<uint8_t *>(addr)) {
  auto num_slots =
      remote_ids.size() * dory::neb::MAX_CONCURRENTLY_PENDING_SLOTS;
  // populate the free queue
  for (size_t i = 0; i < num_slots; i++) {
    free_slots.push(
        const_cast<uint8_t *>(&buf[i * dory::neb::MEMORY_SLOT_SIZE]));
  }
  // populate static entires
  for (auto &i : remote_ids) {
    for (auto &j : remote_ids) {
      if (i == j) continue;
      raws[i][j];
    }
  }
}

MemorySlot ReplayBufferReader::slot(int origin, int replayer, uint64_t index) {
  auto &p = raws[replayer][origin];

  std::unique_lock slock(p.first);
  auto it = p.second.find(index);

  if (it == p.second.end()) {
    std::unique_lock qlock(queue_mux);
    auto *node = free_slots.front();
    free_slots.pop();

    p.second.insert(std::pair<uint64_t, uint8_t *>(index, node));
    return MemorySlot(node);
  }

  return MemorySlot(it->second);
}

void ReplayBufferReader::free(int replayer, int origin, uint64_t index) {
  auto &p = raws[replayer][origin];
  uint8_t *slot = nullptr;

  {
    std::unique_lock lock(p.first);
    auto it = p.second.find(index);

    if (it == p.second.end()) {
      return;
    }

    slot = it->second;
    p.second.erase(it);
  }

  empty(slot);

  std::unique_lock lock(queue_mux);
  free_slots.push(slot);
}