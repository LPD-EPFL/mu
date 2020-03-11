#include <algorithm>

#include "buffer_overlay.hpp"

BufferEntry::BufferEntry(volatile const uint8_t *const buf) : buf(buf) {}

uint64_t BufferEntry::id() const {
  return *reinterpret_cast<volatile const uint64_t *>(buf);
}

uintptr_t BufferEntry::addr() const { return reinterpret_cast<uint64_t>(buf); }

volatile const uint8_t *BufferEntry::content() const {
  return reinterpret_cast<const volatile uint8_t *>(
      &reinterpret_cast<const volatile uint64_t *>(buf)[1]);
}

volatile const uint8_t *BufferEntry::signature() const {
  throw std::logic_error("Signatures are not supported yet");
}

/* -------------------------------------------------------------------------- */

BroadcastBuffer::BroadcastBuffer(uintptr_t addr, uint64_t buf_size,
                                 uint32_t lkey)
    : lkey(lkey),
      buf(reinterpret_cast<volatile const uint8_t *const>(addr)),
      num_entries(buf_size / BUFFER_ENTRY_SIZE) {}

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

  return index * BUFFER_ENTRY_SIZE;
}

std::unique_ptr<BufferEntry> BroadcastBuffer::get_entry(uint64_t index) const {
  return std::make_unique<BufferEntry>(&buf[get_byte_offset(index)]);
}

/* -------------------------------------------------------------------------- */

ReplayBufferWriter::ReplayBufferWriter(uintptr_t addr, size_t buf_size,
                                       std::vector<int> procs)
    : buf(reinterpret_cast<volatile const uint8_t *const>(addr)),
      num_entries_per_proc(buf_size / BUFFER_ENTRY_SIZE / procs.size()) {
  std::sort(std::begin(procs), std::end(procs));

  for (size_t i = 0; i < procs.size(); i++) {
    process_index.insert(std::pair<int, size_t>(procs[i], i));
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

  return p_index * num_entries_per_proc * BUFFER_ENTRY_SIZE +
         index * BUFFER_ENTRY_SIZE;
}

std::unique_ptr<BufferEntry> ReplayBufferWriter::get_entry(
    int proc_id, uint64_t index) const {
  return std::make_unique<BufferEntry>(&buf[get_byte_offset(proc_id, index)]);
}

/* -------------------------------------------------------------------------- */

ReplayBufferReader::ReplayBufferReader(uintptr_t addr, size_t buf_size,
                                       uint32_t lkey, std::vector<int> procs)
    : lkey(lkey),
      buf(reinterpret_cast<volatile const uint8_t *const>(addr)),
      num_proc(procs.size()),
      num_entries_per_proc(buf_size / BUFFER_ENTRY_SIZE / procs.size() /
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
      o_index * num_entries_per_proc * num_proc * BUFFER_ENTRY_SIZE;
  auto index_offset = index * num_proc * BUFFER_ENTRY_SIZE;
  auto replayer_offset = r_index * BUFFER_ENTRY_SIZE;

  return origin_offset + index_offset + replayer_offset;
}

std::unique_ptr<BufferEntry> ReplayBufferReader::get_entry(
    int origin_id, int replayer_id, uint64_t index) const {
  return std::make_unique<BufferEntry>(
      &buf[get_byte_offset(origin_id, replayer_id, index)]);
}