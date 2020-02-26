#include "buffer_overlay.hpp"

BufferEntry::BufferEntry(volatile uint8_t* ptr) : ptr(ptr) {}

uint64_t BufferEntry::id() {
  return reinterpret_cast<volatile uint64_t*>(ptr)[0];
}

uint64_t BufferEntry::addr() { return reinterpret_cast<uint64_t>(ptr); }

volatile uint8_t* BufferEntry::content() {
  return reinterpret_cast<volatile uint8_t*>(
      &reinterpret_cast<volatile uint64_t*>(ptr)[1]);
}

volatile uint8_t* BufferEntry::signature() {
  throw std::logic_error("Signatures are not supported yet");
}

/* -------------------------------------------------------------------------- */

BroadcastBuffer::BroadcastBuffer(volatile uint8_t* ptr, size_t buf_size)
    : ptr(ptr), buf_size(buf_size), num_entries(buf_size / BUFFER_ENTRY_SIZE) {}

uint64_t BroadcastBuffer::get_byte_offset(uint64_t index) {
  // internally we start indexing from 0
  index -= 1;

  if (index >= num_entries || index < 0) {
    throw std::out_of_range(
        "Attempt to access memory outside of the buffer space");
  }

  return index * BUFFER_ENTRY_SIZE;
}

std::unique_ptr<BufferEntry> BroadcastBuffer::get_entry(uint64_t index) {
  return std::make_unique<BufferEntry>(&ptr[get_byte_offset(index)]);
}

// TODO(Kristian): eventually rather pass an object with a marshall interface
// and save creating an intermediary buffer and a copy cycle
uint64_t BroadcastBuffer::write(uint64_t index, uint64_t k,
                                volatile uint8_t* buf, size_t len) {
  auto _ptr =
      reinterpret_cast<volatile uint64_t*>(&ptr[get_byte_offset(index)]);

  _ptr[0] = k;
  memcpy((void*)&_ptr[1], (void*)buf, len);

  return reinterpret_cast<uint64_t>(_ptr);
}

/* -------------------------------------------------------------------------- */

ReplayBufferWriter::ReplayBufferWriter(volatile uint8_t* ptr, size_t buf_size,
                                       int num_proc)
    : ptr(ptr),
      buf_size(buf_size),
      num_proc(num_proc),
      num_entries_per_proc(buf_size / BUFFER_ENTRY_SIZE / num_proc) {}

uint64_t ReplayBufferWriter::get_byte_offset(size_t proc_id, uint64_t index) {
  // internally we start indexing from 0
  index -= 1;

  if (index >= num_entries_per_proc || index < 0) {
    throw std::out_of_range(
        "Attempt to access memory outside of the buffer space");
  }

  return proc_id * num_entries_per_proc * BUFFER_ENTRY_SIZE +
         index * BUFFER_ENTRY_SIZE;
}

std::unique_ptr<BufferEntry> ReplayBufferWriter::get_entry(size_t proc_id,
                                                           uint64_t index) {
  return std::make_unique<BufferEntry>(&ptr[get_byte_offset(proc_id, index)]);
}

/* -------------------------------------------------------------------------- */

ReplayBufferReader::ReplayBufferReader(volatile uint8_t* ptr, size_t buf_size,
                                       int num_proc)
    : ptr(ptr),
      buf_size(buf_size),
      num_proc(num_proc),
      num_entries_per_proc(buf_size / BUFFER_ENTRY_SIZE / num_proc / num_proc) {
}

uint64_t ReplayBufferReader::get_byte_offset(size_t origin_id,
                                             size_t replayer_id,
                                             uint64_t index) {
  // internally we start indexing from 0
  index -= 1;

  if (index >= num_entries_per_proc || index < 0) {
    throw std::out_of_range(
        "Attempt to access memory outside of the buffer space");
  }

  auto origin_offset =
      origin_id * num_entries_per_proc * num_proc * BUFFER_ENTRY_SIZE;
  auto index_offset = index * num_proc * BUFFER_ENTRY_SIZE;
  auto replayer_offset = replayer_id * BUFFER_ENTRY_SIZE;

  return origin_offset + index_offset + replayer_offset;
}

std::unique_ptr<BufferEntry> ReplayBufferReader::get_entry(size_t origin_id,
                                                           size_t replayer_id,
                                                           uint64_t index) {
  return std::make_unique<BufferEntry>(
      &ptr[get_byte_offset(origin_id, replayer_id, index)]);
}

/* -------------------------------------------------------------------------- */
