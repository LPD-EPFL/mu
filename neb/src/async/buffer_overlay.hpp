#pragma once

#include <stdint.h>
#include <cstddef>
#include <map>
#include <vector>

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

  BroadcastBuffer(uintptr_t addr, uint64_t buf_size, uint32_t lkey,
                  std::vector<int> proc_ids);

  uint64_t get_byte_offset(int proc_id, uint64_t index) const;

  MemorySlot slot(int proc_id, uint64_t index) const;

  size_t write(int proc_id, uint64_t index, uint64_t k,
               dory::neb::Broadcastable &msg);

  std::mutex &get_mux(int origin, uint64_t index);

 private:
  volatile uint8_t *const buf;
  uint64_t num_entries_per_proc;
  std::map<int, size_t> proc_idx;
  std::unordered_map<int,
                     std::unordered_map<uint64_t, std::unique_ptr<std::mutex>>>
      muxes;
  std::mutex map_mux;
};
