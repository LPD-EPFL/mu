

#include <memory>

#include <dory/extern/ibverbs.hpp>
#include <dory/shared/pointer-wrapper.hpp>
#include "rc.hpp"

namespace dory {

class SendWrBuilder {
 public:
  SendWrBuilder& req(ReliableConnection::RdmaReq v) {
    req_ = v;
    return *this;
  }
  SendWrBuilder& signaled(bool v) {
    signaled_ = v;
    return *this;
  }
  SendWrBuilder& req_id(uint64_t v) {
    req_id_ = v;
    return *this;
  }
  SendWrBuilder& buf(void* v) {
    buf_ = v;
    return *this;
  }
  SendWrBuilder& len(uint32_t v) {
    len_ = v;
    return *this;
  }
  SendWrBuilder& lkey(uint32_t v) {
    lkey_ = v;
    return *this;
  }
  SendWrBuilder& remote_addr(uintptr_t v) {
    remote_addr_ = v;
    return *this;
  }
  SendWrBuilder& rkey(uint32_t v) {
    rkey_ = v;
    return *this;
  }
  SendWrBuilder& next(ibv_send_wr* v) {
    next_ = v;
    return *this;
  }

  void build(ibv_send_wr& wr, ibv_sge& sg) const { fill(wr, sg); }

  dory::deleted_unique_ptr<struct ibv_send_wr> build() const {
    struct ibv_sge* sg = reinterpret_cast<ibv_sge*>(malloc(sizeof(ibv_sge)));

    struct ibv_send_wr* wr =
        reinterpret_cast<ibv_send_wr*>(malloc(sizeof(ibv_send_wr)));

    fill(*wr, *sg);

    return dory::deleted_unique_ptr<struct ibv_send_wr>(wr, wr_deleter);
  }

 private:
  void fill(ibv_send_wr& wr, ibv_sge& sg) const {
    memset(&wr, 0, sizeof(ibv_send_wr));
    memset(&sg, 0, sizeof(ibv_sge));

    sg.addr = reinterpret_cast<uintptr_t>(buf_);
    sg.length = len_;
    sg.lkey = lkey_;

    wr.wr_id = req_id_;
    wr.sg_list = &sg;
    wr.num_sge = 1;
    wr.opcode = static_cast<enum ibv_wr_opcode>(req_);  // TODO
    wr.next = next_;

    if (signaled_) {
      wr.send_flags |= IBV_SEND_SIGNALED;
    }

    if (wr.opcode == IBV_WR_RDMA_WRITE &&
        len_ <= ReliableConnection::MaxInlining) {
      wr.send_flags |= IBV_SEND_INLINE;
    }

    wr.wr.rdma.remote_addr = remote_addr_;
    wr.wr.rdma.rkey = rkey_;
  }

  static void wr_deleter(struct ibv_send_wr* wr) {
    if (wr->sg_list != nullptr) {
      free(wr->sg_list);
    }

    free(wr);
  }

  ReliableConnection::RdmaReq req_;
  bool signaled_;
  uint64_t req_id_;
  void* buf_;
  uint32_t len_;
  uint32_t lkey_;
  uintptr_t remote_addr_;
  uint32_t rkey_;
  ibv_send_wr* next_;
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