#include <cstring>
#include <stdexcept>

#include "block.hpp"
#include "device.hpp"

namespace dory {
ControlBlock::ControlBlock(ResolvedPort &resolved_port)
    : resolved_port{resolved_port}, logger(std_out_logger("CB")) {}

void ControlBlock::registerPD(std::string name) {
  if (pd_map.find(name) != pd_map.end()) {
    throw std::runtime_error("Already registered protection domain named " +
                             name);
  }

  auto pd = ibv_alloc_pd(resolved_port.device().context());

  if (pd == nullptr) {
    throw std::runtime_error("Could not register the protection domain " +
                             name);
  }

  deleted_unique_ptr<struct ibv_pd> uniq_pd(pd, [](struct ibv_pd *pd) {
    auto ret = ibv_dealloc_pd(pd);
    if (ret != 0) {
      throw std::runtime_error("Could not query device: " +
                               std::string(std::strerror(errno)));
    }
  });

  pds.push_back(std::move(uniq_pd));
  pd_map.insert(std::pair<std::string, size_t>(name, pds.size() - 1));
  SPDLOG_LOGGER_INFO(logger, "PD '{}' registered", name);
}

deleted_unique_ptr<struct ibv_pd> &ControlBlock::pd(std::string name) {
  auto pd = pd_map.find(name);
  if (pd == pd_map.end()) {
    throw std::runtime_error("Protection domain named " + name +
                             " does not exist");
  }

  return pds[pd->second];
}

void ControlBlock::allocateBuffer(std::string name, size_t length,
                                  int alignment) {
  if (buf_map.find(name) != buf_map.end()) {
    throw std::runtime_error("Already registered protection domain named " +
                             name);
  }

  std::unique_ptr<uint8_t[], DeleteAligned<uint8_t>> data(
      allocate_aligned<uint8_t>(alignment, length));
  memset(data.get(), 0, length);

  raw_bufs.push_back(std::move(data));

  std::pair<size_t, size_t> index_length(raw_bufs.size() - 1, length);

  buf_map.insert(
      std::pair<std::string, std::pair<size_t, size_t>>(name, index_length));
  SPDLOG_LOGGER_INFO(logger, "Buffer '{}' of size {} allocated", name, length);
}

void ControlBlock::registerMR(std::string name, std::string pd_name,
                              std::string buffer_name, MemoryRights rights) {
  if (mr_map.find(name) != mr_map.end()) {
    throw std::runtime_error("Already registered protection domain named " +
                             name);
  }
  auto pd = pd_map.find(pd_name);
  if (pd == pd_map.end()) {
    throw std::runtime_error("No PD exists with name " + pd_name);
  }

  auto buf = buf_map.find(buffer_name);
  if (buf_map.find(buffer_name) == buf_map.end()) {
    throw std::runtime_error("No buffer exists with name " + buffer_name);
  }

  auto mr = ibv_reg_mr(pds[pd->second].get(), raw_bufs[buf->second.first].get(),
                       buf->second.second, static_cast<int>(rights));

  if (mr == nullptr) {
    throw std::runtime_error("Could not register the memory region " + name);
  }

  deleted_unique_ptr<struct ibv_mr> uniq_mr(mr, [](struct ibv_mr *mr) {
    auto ret = ibv_dereg_mr(mr);
    if (ret != 0) {
      throw std::runtime_error("Could not query device: " +
                               std::string(std::strerror(errno)));
    }
  });

  mrs.push_back(std::move(uniq_mr));
  mr_map.insert(std::pair<std::string, size_t>(name, mrs.size() - 1));
  SPDLOG_LOGGER_INFO(
      logger, "MR '{}' under PD '{}' registered with buf '{}' and rights {}",
      name, pd_name, buffer_name, rights);
}

ControlBlock::MemoryRegion ControlBlock::mr(std::string name) const {
  auto mr = mr_map.find(name);
  if (mr == mr_map.end()) {
    throw std::runtime_error("Memory region named " + name + " does not exist");
  }

  auto const &region = mrs[mr->second];

  MemoryRegion m;
  m.addr = reinterpret_cast<uintptr_t>(region->addr);
  m.size = region->length;
  m.lkey = region->lkey;
  m.rkey = region->rkey;

  return m;
}

// void ControlBlock::withdrawMRRight(std::string name) const {
//   auto mr = mr_map.find(name);
//   if (mr == mr_map.end()) {
//     throw std::runtime_error("Memory region named " + name + " does not
//     exist");
//   }

//   auto const& region = mrs[mr->second];

//   auto ret = ibv_rereg_mr(region.get(), IBV_REREG_MR_CHANGE_ACCESS, NULL,
//   NULL,
//                           region->length, IBV_ACCESS_LOCAL_WRITE);

//   if (ret != 0) {
//     throw std::runtime_error("Memory region named " + name + " cannot be
//     withdrawn");
//   }
// }

void ControlBlock::registerCQ(std::string name) {
  if (cq_map.find(name) != cq_map.end()) {
    throw std::runtime_error("Already registered protection domain named " +
                             name);
  }

  auto cq = ibv_create_cq(resolved_port.device().context(), CQDepth, nullptr,
                          nullptr, 0);

  if (cq == nullptr) {
    throw std::runtime_error("Could not register the completion queue " + name);
  }

  deleted_unique_ptr<struct ibv_cq> uniq_cq(cq, [](struct ibv_cq *cq) {
    auto ret = ibv_destroy_cq(cq);
    if (ret != 0) {
      throw std::runtime_error("Could not query device: " +
                               std::string(std::strerror(errno)));
    }
  });

  cqs.push_back(std::move(uniq_cq));
  cq_map.insert(std::pair<std::string, size_t>(name, cqs.size() - 1));
  SPDLOG_LOGGER_INFO(logger, "CQ '{}' registered", name);
}

deleted_unique_ptr<struct ibv_cq> &ControlBlock::cq(std::string name) {
  auto cq = cq_map.find(name);
  if (cq == cq_map.end()) {
    throw std::runtime_error("Completion queue named " + name +
                             " does not exist");
  }

  return cqs[cq->second];
}

int ControlBlock::port() const { return resolved_port.portID(); }

int ControlBlock::lid() const { return resolved_port.portLID(); }

// TODO(Kristian): return num and remove the erase
bool ControlBlock::pollCqIsOK(deleted_unique_ptr<struct ibv_cq> &cq,
                              std::vector<struct ibv_wc> &entries) {
  auto num = ibv_poll_cq(cq.get(), entries.size(), &entries[0]);

  if (num >= 0) {
    entries.erase(entries.begin() + num, entries.end());
    return true;
  } else {
    return false;
  }
}
}  // namespace dory
