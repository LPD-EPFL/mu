#pragma once

#include <stdint.h>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

#include "consts.hpp"

/// NOTE: Buffer entries are indexed beginning from 1! Trying to access an entry
/// at index 0 will throw a `std::out_of_range`. Internally, indexing starts at
/// 0. However, the exposed interface assumes indexes starting from 1.

/**
 * A buffer entry has a fixed size of `BUFFER_ENTRY_SIZE` which should always
 * be a multiple of 2.
 *
 * +----------+-----------+-----------+
 * | id       | content   | signature |
 * +----------+-----------+-----------+
 * | uint64_t | uint8_t[] | uint8_t[] |
 * +----------+-----------+-----------+
 *
 **/
class BufferEntry {
 public:
  /**
   * @param start: reference to the the entry
   **/
  BufferEntry(volatile const uint8_t *const buf);

  /**
   * @returns: the message id
   **/
  uint64_t id() const;

  /**
   * @returns: a pointer to the content
   **/
  volatile const uint8_t *content() const;

  /**
   * @returns: a pointer to the signature
   **/
  volatile const uint8_t *signature() const;

  /**
   * @returns: the address of the entry
   **/
  uintptr_t addr() const;

 private:
  volatile const uint8_t *const buf;
};

/**
 * This buffer overlay is used to access the written messages by remote
 * processes. For every remote process there exist one broadcast buffer.
 **/
class BroadcastBuffer {
 public:
  uint32_t lkey;

  /**
   * @param addr: address of the buffer
   * @param buf_size: buffer size in bytes
   * @param lkey: local key for the memory region
   **/
  BroadcastBuffer(uintptr_t addr, uint64_t buf_size, uint32_t lkey);

  /**
   * @param index: index of the entry
   * @returns: the offset in bytes where this entry resides in the buffer
   * @thorws: std::out_of_range
   **/
  uint64_t get_byte_offset(uint64_t index) const;

  /**
   * @param index: index of the entry
   * @returns: the entry associated with the provided index
   * @thorws: std::out_of_range
   **/
  std::unique_ptr<BufferEntry> get_entry(uint64_t index) const;

 private:
  volatile const uint8_t *const buf;
  uint64_t num_entries;
};

/**
 * This buffer overlay is used to replay the read values within the
 * `BroadcastBuffer`.
 *
 * The buffer space is split among all processes in the cluster.
 *
 * We don't support writing to this buffer, as performance wise we should prefer
 * to local RDMA write from the Broadcast buffer to this replay buffer.
 **/
class ReplayBufferWriter {
 public:
  /**
   * @param addr: address of the buffer
   * @param buf_size: the size of the buffer in bytes
   * @param procs: a vector holding all process ids
   **/
  ReplayBufferWriter(uintptr_t addr, size_t buf_size, std::vector<int> procs);

  /**
   * @param proc_id: the process id
   * @param index: index of the entry
   **/
  uint64_t get_byte_offset(int proc_id, uint64_t index) const;

  /**
   * @param proc_id: the process id
   * @param index: index of the entry
   **/
  std::unique_ptr<BufferEntry> get_entry(int proc_id, uint64_t index) const;

 private:
  volatile const uint8_t *const buf;
  uint64_t num_entries_per_proc;
  std::map<int, size_t> process_index;
};

/**
 * This buffer overlay is for reading gathered remote replay entries.
 *
 * The buffer space is split among all processes in the cluster. Additionally,
 * for every index there is a slot for every process to store the replayed
 * values.
 *
 * We don't support any direct write opertaions since the RNIC will write to
 * this buffer.
 **/
class ReplayBufferReader {
 public:
  uint32_t lkey;
  /**
   * @param addr: address of the buffer
   * @param buf_size: the size of the buffer in bytes
   * @param lkey: local key for the memory region
   * @param procs: a vector holding all process ids
   **/
  ReplayBufferReader(uintptr_t addr, size_t buf_size, uint32_t lkey,
                     std::vector<int> procs);

  /**
   * @param origin_id: id of the process who's value is replayed
   * @param replayer_id: id of the process who replayed the value
   * @param index: index of the entry
   **/
  uint64_t get_byte_offset(int origin_id, int replayer_id,
                           uint64_t index) const;

  /**
   * @param origin_id: id of the process who's value is replayed
   * @param replayer_id: id of the process who replayed the value
   * @param index: index of the entry
   **/
  std::unique_ptr<BufferEntry> get_entry(int origin_id, int replayer_id,
                                         uint64_t index) const;

 private:
  volatile const uint8_t *const buf;
  uint64_t num_proc;
  uint64_t num_entries_per_proc;
  std::map<int, size_t> process_index;
};