#pragma once

#include <stdint.h>
#include <cstddef>

#include "../broadcastable.hpp"

#include "consts.hpp"
#include "mem-slot.hpp"

/// NOTE: Buffer entries are indexed beginning from 1! Trying to access an entry
/// at index 0 will throw a `std::out_of_range`. Internally, indexing starts at
/// 0. However, the exposed interface assumes indexes starting from 1.

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

  std::mutex &get_mux(uint64_t index);

 private:
  volatile uint8_t *const buf;
  uint64_t num_entries;
  std::unordered_map<uint64_t, std::unique_ptr<std::mutex>> muxes;
  std::mutex map_mux;
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
class ReplayBuffer {
 public:
  /**
   * @param addr: address of the buffer
   * @param buf_size: the size of the buffer in bytes
   * @param procs: a vector holding all process ids
   **/
  ReplayBuffer(uintptr_t addr, size_t buf_size, std::vector<int> procs);

  /**
   * @param proc_id: the process id
   * @param index: index of the entry
   **/
  uint64_t get_byte_offset(int proc_id, uint64_t index) const;

  std::mutex &get_mux(int origin, uint64_t index);

  /**
   * @param proc_id: the process id
   * @param index: index of the entry
   **/
  MemorySlot slot(int proc_id, uint64_t index) const;

 private:
  volatile const uint8_t *const buf;
  uint64_t num_entries_per_proc;
  std::map<int, size_t> process_index;
  std::unordered_map<int,
                     std::unordered_map<uint64_t, std::unique_ptr<std::mutex>>>
      muxes;
  std::mutex map_mux;
};