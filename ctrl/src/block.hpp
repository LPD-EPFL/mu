#pragma once

#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <infiniband/verbs.h>

#include <dory/shared/pointer-wrapper.hpp>
#include "device.hpp"

namespace dory {
class ControlBlock {
 public:
  enum MemoryRights {
    LOCAL_READ = 0,
    LOCAL_WRITE = IBV_ACCESS_LOCAL_WRITE,
    REMOTE_READ = IBV_ACCESS_REMOTE_READ,
    REMOTE_WRITE = IBV_ACCESS_REMOTE_WRITE
  };

  struct MemoryRegion {
    uintptr_t addr;
    uint64_t size;
    uint32_t lkey;
    uint32_t rkey;
  };

 private:
  static constexpr int CQDepth = 128;

 public:
  ControlBlock(ResolvedPort &resolved_port);

  void registerPD(std::string name);

  deleted_unique_ptr<struct ibv_pd> &pd(std::string name);

  void allocateBuffer(std::string name, size_t length, int alignment);

  void registerMR(std::string name, std::string pd_name,
                  std::string buffer_name, MemoryRights rights = LOCAL_READ);
  // void withdrawMRRight(std::string name) const;
  MemoryRegion mr(std::string name) const;

  void registerCQ(std::string name);
  deleted_unique_ptr<struct ibv_cq> &cq(std::string name);

  int port() const;
  int lid() const;

  bool pollCqIsOK(deleted_unique_ptr<struct ibv_cq> &cq,
                  std::vector<struct ibv_wc> &entries);

 private:
  ResolvedPort resolved_port;

  std::vector<deleted_unique_ptr<struct ibv_pd>> pds;
  std::map<std::string, size_t> pd_map;

  std::vector<std::unique_ptr<uint8_t[], DeleteAligned<uint8_t>>> raw_bufs;
  std::map<std::string, std::pair<size_t, size_t>> buf_map;

  std::vector<deleted_unique_ptr<struct ibv_mr>> mrs;
  std::map<std::string, size_t> mr_map;

  std::vector<deleted_unique_ptr<struct ibv_cq>> cqs;
  std::map<std::string, size_t> cq_map;
};

inline ControlBlock::MemoryRights operator|(ControlBlock::MemoryRights a,
                                            ControlBlock::MemoryRights b) {
  return static_cast<ControlBlock::MemoryRights>(static_cast<int>(a) |
                                                 static_cast<int>(b));
}
}  // namespace dory
