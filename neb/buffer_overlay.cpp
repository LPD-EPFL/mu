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

std::unique_ptr<MemorySlot> BroadcastBuffer::slot(uint64_t index) const {
  return std::make_unique<MemorySlot>(&buf[get_byte_offset(index)]);
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

std::unique_ptr<MemorySlot> ReplayBufferWriter::slot(int proc_id,
                                                     uint64_t index) const {
  return std::make_unique<MemorySlot>(&buf[get_byte_offset(proc_id, index)]);
}

/* -------------------------------------------------------------------------- */

ReplayBufferReader::ReplayBufferReader(uintptr_t addr, size_t buf_size,
                                       uint32_t lkey, std::vector<int> procs)
    : lkey(lkey),
      buf(reinterpret_cast<volatile const uint8_t *const>(addr)),
      num_proc(procs.size()),
      num_entries_per_proc(buf_size / MEMORY_SLOT_SIZE / procs.size() /
                           procs.size()) {
  std::sort(std::begin(procs), std::end(procs));

  for (size_t i = 0; i < procs.size(); i++) {
    process_index.insert(std::pair<int, size_t>(procs[i], i));
  }
}

uint64_t ReplayBufferReader::get_byte_offset(int origin_id, int replayer_id,
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

  auto o_index = process_index.find(origin_id)->second;
  auto r_index = process_index.find(replayer_id)->second;

  auto origin_offset =
      o_index * num_entries_per_proc * num_proc * MEMORY_SLOT_SIZE;
  auto index_offset = index * num_proc * MEMORY_SLOT_SIZE;
  auto replayer_offset = r_index * MEMORY_SLOT_SIZE;

  return origin_offset + index_offset + replayer_offset;
}

std::unique_ptr<MemorySlot> ReplayBufferReader::slot(int origin_id,
                                                     int replayer_id,
                                                     uint64_t index) const {
  return std::make_unique<MemorySlot>(
      &buf[get_byte_offset(origin_id, replayer_id, index)]);
}