#pragma once

#include <iostream>
#include <sstream>
#include <string>

#include <dory/ctrl/block.hpp>

namespace dory {
struct RemoteConnection {
  struct __attribute__((packed)) RemoteConnectionInfo {
    uint16_t lid;
    uint32_t qpn;

    uintptr_t buf_addr;
    uint64_t buf_size;
    uint32_t rkey;
  };

  RemoteConnection() {
    rci.lid = 0;
    rci.qpn = 0;
    rci.buf_addr = 0;
    rci.buf_size = 0;
    rci.rkey = 0;
  }

  RemoteConnection(uint16_t lid, uint32_t qpn, uintptr_t buf_addr,
                   uint64_t buf_size, uint32_t rkey) {
    rci.lid = lid;
    rci.qpn = qpn;
    rci.buf_addr = buf_addr;
    rci.buf_size = buf_size;
    rci.rkey = rkey;
  }

  RemoteConnection(RemoteConnectionInfo rci) : rci{rci} {}

  std::string serialize() const {
    std::ostringstream os;

    os << std::hex << rci.lid << ":" << rci.qpn << ":" << rci.buf_addr << ":"
       << rci.buf_size << ":" << rci.rkey;
    return os.str();
  }

  static RemoteConnection fromStr(std::string const &str) {
    RemoteConnectionInfo rci;

    std::string res(str);

    std::replace(res.begin(), res.end(), ':', ' ');  // replace ':' by ' '

    std::stringstream ss(res);

    uint16_t lid;
    uint32_t qpn;

    uintptr_t buf_addr;
    uint64_t buf_size;
    uint32_t rkey;

    ss >> std::hex >> lid;
    ss >> std::hex >> qpn;
    ss >> std::hex >> buf_addr;
    ss >> std::hex >> buf_size;
    ss >> std::hex >> rkey;

    rci.lid = lid;
    rci.qpn = qpn;
    rci.buf_addr = buf_addr;
    rci.buf_size = buf_size;
    rci.rkey = rkey;

    return RemoteConnection(rci);
  }

  // private:
  RemoteConnectionInfo rci;
};

class ReliableConnection {
 public:
  enum CQ { SendCQ, RecvCQ };

  enum RdmaReq { RdmaRead = IBV_WR_RDMA_READ, RdmaWrite = IBV_WR_RDMA_WRITE };

  static constexpr int WRDepth = 128;
  static constexpr int SGEDepth = 16;
  static constexpr int MaxInlining = 16;
  static constexpr uint32_t DefaultPSN = 3185;

  ReliableConnection(ControlBlock &cb);

  void bindToPD(std::string pd_name);

  void bindToMR(std::string mr_name);

  void associateWithCQ(std::string send_cp_name, std::string recv_cp_name);

  void reset();

  void init(ControlBlock::MemoryRights rights);

  void connect(RemoteConnection &rci);
  void reconnect();

  bool postSendSingle(RdmaReq req, uint64_t req_id, void *buf, uint64_t len,
                      uintptr_t remote_addr);

  bool postSendSingle(RdmaReq req, uint64_t req_id, void *buf, uint64_t len,
                      uint32_t lkey, uintptr_t remote_addr);

  bool pollCqIsOK(CQ cq, std::vector<struct ibv_wc> &entries);

  RemoteConnection remoteInfo() const;

  uintptr_t remoteBuf() const { return rconn.rci.buf_addr; }

 private:
  ControlBlock &cb;
  struct ibv_pd *pd;
  struct ibv_qp_init_attr create_attr;
  struct ibv_qp_attr conn_attr;
  deleted_unique_ptr<struct ibv_qp> uniq_qp;
  ControlBlock::MemoryRegion mr;
  RemoteConnection rconn;
};
}  // namespace dory
