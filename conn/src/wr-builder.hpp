

#include <memory>

#include <dory/extern/ibverbs.hpp>
#include <dory/shared/pointer-wrapper.hpp>
#include "rc.hpp"

namespace dory {

class SendWrBuilder {
 public:
  SendWrBuilder& req(ReliableConnection::RdmaReq v) {
    __req = v;
    return *this;
  }
  SendWrBuilder& signaled(bool v) {
    __signaled = v;
    return *this;
  }
  SendWrBuilder& req_id(uint64_t v) {
    __req_id = v;
    return *this;
  }
  SendWrBuilder& buf(void* v) {
    __buf = v;
    return *this;
  }
  SendWrBuilder& len(uint64_t v) {
    __len = v;
    return *this;
  }
  SendWrBuilder& lkey(uint32_t v) {
    __lkey = v;
    return *this;
  }
  SendWrBuilder& remote_addr(uintptr_t v) {
    __remote_addr = v;
    return *this;
  }
  SendWrBuilder& rkey(uint32_t v) {
    __rkey = v;
    return *this;
  }
  SendWrBuilder& next(ibv_send_wr* v) {
    __next = v;
    return *this;
  }

  void build(ibv_send_wr& wr, ibv_sge& sg) const { fill(wr, sg); }

  dory::deleted_unique_ptr<struct ibv_send_wr> build() const {
    struct ibv_sge* sg = (ibv_sge*)malloc(sizeof(ibv_sge));

    struct ibv_send_wr* wr = (ibv_send_wr*)malloc(sizeof(ibv_send_wr));

    fill(*wr, *sg);

    return dory::deleted_unique_ptr<struct ibv_send_wr>(wr, wr_deleter);
  }

 private:
  void fill(ibv_send_wr& wr, ibv_sge& sg) const {
    memset(&wr, 0, sizeof(ibv_send_wr));
    memset(&sg, 0, sizeof(ibv_sge));

    sg.addr = reinterpret_cast<uintptr_t>(__buf);
    sg.length = __len;
    sg.lkey = __lkey;

    wr.wr_id = __req_id;
    wr.sg_list = &sg;
    wr.num_sge = 1;
    wr.opcode = static_cast<enum ibv_wr_opcode>(__req);  // TODO
    wr.next = __next;

    if (__signaled) {
      wr.send_flags |= IBV_SEND_SIGNALED;
    }

    if (wr.opcode == IBV_WR_RDMA_WRITE &&
        __len <= ReliableConnection::MaxInlining) {
      wr.send_flags |= IBV_SEND_INLINE;
    }

    wr.wr.rdma.remote_addr = __remote_addr;
    wr.wr.rdma.rkey = __rkey;
  }

  static void wr_deleter(struct ibv_send_wr* wr) {
    if (wr->sg_list != nullptr) {
      free(wr->sg_list);
    }

    free(wr);
  }

  ReliableConnection::RdmaReq __req;
  bool __signaled;
  uint64_t __req_id;
  void* __buf;
  uint64_t __len;
  uint32_t __lkey;
  uintptr_t __remote_addr;
  uint32_t __rkey;
  ibv_send_wr* __next;
};

// TODO(Kristian): test this
class SendWrListBuilder {
 public:
  SendWrListBuilder& prepend(dory::deleted_unique_ptr<struct ibv_send_wr> wr) {
    auto raw = wr.release();

    return prepend(raw);
  }

  SendWrListBuilder& prepend(ibv_send_wr* wr) {
    wr->next = root.release();
    root = dory::deleted_unique_ptr<struct ibv_send_wr>(wr, rec_list_deleter);

    return *this;
  }

  ibv_send_wr* get() { return root.get(); }

 private:
  static void rec_list_deleter(struct ibv_send_wr* wr) {
    if (wr == nullptr) return;

    auto next = wr->next;

    if (wr->sg_list != nullptr) {
      free(wr->sg_list);
    }

    free(wr);

    rec_list_deleter(next);
  }

  dory::deleted_unique_ptr<ibv_send_wr> root;
};

}  // namespace dory