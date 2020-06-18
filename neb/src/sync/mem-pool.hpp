#include <vector>

#include <dory/shared/branching.hpp>

#include "../shared/mem-slot.hpp"
/**
 * Synchronized fixed size memory pool meant to be used for temporal storage
 * when reading remote memory slots
 *
 * */
class MemSlotPool {
 public:
  /**
   * @param remote_ids: vector holding the remote ids
   * @param addr: address to the begining of the buffer
   * @param size: size of the buffer
   * @param lkey: local key for the memory region
   **/
  MemSlotPool(std::vector<int> remote_ids, uintptr_t addr, size_t size,
              uint32_t lkey)
      : local_mr_key(lkey), buf(reinterpret_cast<uint8_t *>(addr)) {
    auto num_slots = size / dory::neb::MEMORY_SLOT_SIZE;
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
  /**
   * @param origin_id: id of the process who's value is replayed
   * @param replayer_id: id of the process who replayed the value
   * @param index: index of the entry
   **/
  MemorySlot slot(int origin, int replayer, uint64_t index) {
    auto &p = raws[replayer][origin];

    std::unique_lock slock(p.first);
    auto it = p.second.find(index);

    if (it == p.second.end()) {
      std::unique_lock qlock(queue_mux);
      auto *node = free_slots.front();
      free_slots.pop();

      if (unlikely(node == nullptr)) {
        throw std::runtime_error("Memory Pool exhausted");
      }

      p.second.insert(std::pair<uint64_t, uint8_t *>(index, node));
      return MemorySlot(node);
    }

    return MemorySlot(it->second);
  }

  /**
   * Marks the slot corresponding to the provided arguments as free s.t. it
   * can be reused. Also empties its contents.
   * @param origin_id: id of the process who's value is replayed
   * @param replayer_id: id of the process who replayed the value
   * @param index: index of the entry
   **/
  void free(int replayer, int origin, uint64_t index) {
    auto &p = raws[replayer][origin];
    uint8_t *slot = nullptr;

    {
      std::unique_lock lock(p.first);
      auto it = p.second.find(index);

      if (unlikely(it == p.second.end())) {
        return;
      }

      slot = it->second;
      p.second.erase(it);
    }

    empty(slot);

    std::unique_lock lock(queue_mux);
    free_slots.push(slot);
  }

  uint32_t lkey() const { return local_mr_key; }

 private:
  inline void empty(uint8_t *slot) {
    std::memset(slot, 0, dory::neb::MEMORY_SLOT_SIZE);
  }

  uint32_t local_mr_key;
  volatile const uint8_t *const buf;
  std::queue<uint8_t *> free_slots;
  std::shared_mutex queue_mux;
  std::unordered_map<
      // replayer id
      int, std::unordered_map<
               // origin id
               int, std::pair<std::shared_mutex,
                              // slot index
                              std::unordered_map<uint64_t, uint8_t *>>>>
      raws;
};