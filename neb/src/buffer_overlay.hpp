#pragma once

#include <stdint.h>
#include <cstring>
#include <map>
#include <memory>
#include <queue>
#include <shared_mutex>
#include <vector>

// #include <foonathan/memory/memory_pool.hpp>
// #include <foonathan/memory/namespace_alias.hpp>

#include "broadcastable.hpp"
#include "consts.hpp"

/// NOTE: Buffer entries are indexed beginning from 1! Trying to access an entry
/// at index 0 will throw a `std::out_of_range`. Internally, indexing starts at
/// 0. However, the exposed interface assumes indexes starting from 1.

/**
 * A buffer entry has a fixed size of `MEMORY_SLOT_SIZE` which should always
 * be a multiple of 2.
 *
 * +----------+-----------+-----------+
 * | id       | content   | signature |
 * +----------+-----------+-----------+
 * | uint64_t | uint8_t[] | uint8_t[] |
 * +----------+-----------+-----------+
 *
 **/
class MemorySlot {
 public:
  // enum SlotType { BROADCAST, REPLAY, REPLAY_READ_TMP };
  /**
   * @param start: reference to the the entry
   **/
  MemorySlot(volatile const uint8_t *const buf) : buf(buf) {}

  /**
   * @returns: the message id
   **/
  uint64_t id() const {
    return *reinterpret_cast<volatile const uint64_t *>(buf);
  }

  /**
   * @returns: a pointer to the content
   **/
  volatile const uint8_t *content() const {
    return reinterpret_cast<const volatile uint8_t *>(
        &reinterpret_cast<const volatile uint64_t *>(buf)[1]);
  }

  /**
   * @returns: a pointer to the signature
   **/
  volatile const uint8_t *signature() const {
    return reinterpret_cast<const volatile uint8_t *>(
        &buf[dory::neb::SLOT_SIGNATURE_OFFSET]);
  }

  /**
   * @returns: bool indicating if the signature part is not equal to the
   * default empty val
   **/
  bool has_signature() const {
    uint8_t default_val = 0;

    auto start = signature();

    for (size_t i = 0; i < dory::crypto::sodium::SIGN_BYTES; i++) {
      if (start[i] != default_val) {
        return true;
      }
    }
    return false;
  }

  /**
   * @returns: the address of the entry
   **/
  uintptr_t addr() const { return reinterpret_cast<uintptr_t>(buf); }

  /**
   * @param entry: where to copy the own contents
   **/
  inline void copy_to(const MemorySlot &entry) const {
    std::memcpy(reinterpret_cast<void *>(entry.addr()),
                const_cast<uint8_t const *>(buf), dory::neb::MEMORY_SLOT_SIZE);
  }

  inline void copy_content_to(const MemorySlot &entry) const {
    std::memcpy(reinterpret_cast<void *>(entry.addr()),
                const_cast<uint8_t const *>(buf),
                dory::neb::SLOT_SIGN_DATA_SIZE);
  }

  inline void copy_signature_to(const MemorySlot &entry) const {
    std::memcpy(
        reinterpret_cast<void *>(entry.addr() +
                                 dory::neb::SLOT_SIGNATURE_OFFSET),
        const_cast<uint8_t const *>(buf) + dory::neb::SLOT_SIGNATURE_OFFSET,
        dory::crypto::sodium::SIGN_BYTES);
  }

  bool has_same_data_content_as(const MemorySlot &entry) const {
    return 0 == std::memcmp(const_cast<uint8_t const *>(buf),
                            const_cast<uint8_t const *>(entry.buf),
                            dory::neb::SLOT_SIGN_DATA_SIZE);
  }

  bool operator==(const MemorySlot &other) const { return buf == other.buf; }

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
  MemorySlot slot(uint64_t index) const;

  size_t write(uint64_t index, uint64_t k, dory::neb::Broadcastable &msg);

 private:
  volatile uint8_t *const buf;
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
  MemorySlot slot(int proc_id, uint64_t index) const;

 private:
  volatile const uint8_t *const buf;
  uint64_t num_entries_per_proc;
  std::map<int, size_t> process_index;
};

class ReplayBufferReader {
 public:
  uint32_t lkey;
  /**
   * @param remote_ids: vector holding the remote ids
   * @param addr: address to the begining of the buffer
   * @param size: size of the buffer
   * @param lkey: local key for the memory region
   **/
  ReplayBufferReader(std::vector<int> remote_ids, uintptr_t addr,
                     uint32_t lkey);
  /**
   * @param origin_id: id of the process who's value is replayed
   * @param replayer_id: id of the process who replayed the value
   * @param index: index of the entry
   **/
  MemorySlot slot(int origin, int replayer, uint64_t index);

  /**
   * Marks the slot corresponding to the provided arguments as free s.t. it
   * can be reused. Also empties its contents.
   * @param origin_id: id of the process who's value is replayed
   * @param replayer_id: id of the process who replayed the value
   * @param index: index of the entry
   **/
  void free(int replayer, int origin, uint64_t index);

 private:
  inline void empty(uint8_t *slot) {
    std::memset(slot, 0, dory::neb::MEMORY_SLOT_SIZE);
  }

  volatile const uint8_t *const buf;
  // size_t buf_size;
  std::queue<uint8_t *> free_slots;
  std::shared_mutex queue_mux;
  // replayer -> origin -> index
  std::unordered_map<
      int, std::unordered_map<
               int, std::pair<std::shared_mutex,
                              std::unordered_map<uint64_t, uint8_t *>>>>
      raws;
};