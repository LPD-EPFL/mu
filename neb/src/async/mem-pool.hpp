#include <utility>
#include <vector>

#include "mem-slot.hpp"

inline uint64_t get_key(uint64_t index, int count) {
  return uint64_t(count) << 32 | index;
}

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
  MemSlotPool(std::vector<int> remote_ids, intptr_t addr, size_t size,
              uint32_t lkey)
      : local_mr_key(lkey), buf(reinterpret_cast<uint8_t *>(addr)) {
    auto num_slots = size / dory::neb::MEMORY_SLOT_SIZE;

    // std::cout << "Mempool has: " << num_slots << " entries available"
    //           << std::endl;

    // populate the free queue
    for (size_t i = 0; i < num_slots; i++) {
      free_slots.push(
          const_cast<uint8_t *>(&buf[i * dory::neb::MEMORY_SLOT_SIZE]));
    }
    // populate static entires
    for (auto &i : remote_ids) {
      for (auto &j : remote_ids) {
        for (auto &k : remote_ids) {
          raws[i][j][k];
        }
      }
    }
  }

  /**
   * @param origin: id of the process who's value is replayed
   * @param replayer: id of the process who replayed the value
   * @param replicator: id of the process who is replicating the replayed value
   * @param index: index of the entry
   **/
  MemorySlot slot(int origin, int replayer, int replicator, uint64_t index,
                  int count) {
    auto &p = raws[replayer][origin][replicator];

    std::unique_lock slock(p.first);
    auto key = get_key(index, count);

    auto it = p.second.find(key);

    if (it == p.second.end()) {
      std::unique_lock qlock(queue_mux);

      if (free_slots.size() == 0) {
        throw std::runtime_error("Memory Pool exhausted");
      }

      auto *node = free_slots.front();

      free_slots.pop();

      p.second.insert({key, node});

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
  void free(int replayer, int origin, int replicator, uint64_t index,
            int count) {
    auto &p = raws[replayer][origin][replicator];
    uint8_t *slot = nullptr;

    {
      std::unique_lock lock(p.first);
      auto key = get_key(index, count);

      auto it = p.second.find(key);

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
      int,
      std::unordered_map<
          // origin id
          int, std::unordered_map<
                   // replicator id
                   int, std::pair<std::shared_mutex,
                                  // key(index+count)
                                  std::unordered_map<uint64_t, uint8_t *>>>>>
      raws;
};