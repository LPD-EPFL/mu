#include "neb.hpp"
/**
 * Returns the index for the `broadcast-lgid-p_idx` QP
 * * @param p_id: id of the process
 */
static inline size_t b_idx(size_t p_id) { return 2 * p_id; }

/**
 * Returns the index for the `replay-lgid-p_idx` QP
 * @param p_id: id of the process
 */
static inline size_t r_idx(size_t p_id) { return 2 * p_id + 1; }

NonEquivocatingBroadcast::~NonEquivocatingBroadcast() {
  poller_running = false;
}

/**
 * TODO(Kristian): DOC
 */
NonEquivocatingBroadcast::NonEquivocatingBroadcast(
    size_t lgid, size_t num_proc,
    void (*deliver_cb)(uint64_t k, volatile uint8_t *m, size_t proc_id))
    : lgid(lgid),
      num_proc(num_proc),
      last(std::make_unique<uint64_t[]>(num_proc)),
      deliver_callback(deliver_cb) {
  ConnectionConfig conn_config = ConnectionConfig::builder{}
                                     .max_rd_atomic(16)
                                     .sq_depth(kHrdSQDepth)
                                     .num_qps(num_proc * 2)
                                     .use_uc(0)
                                     .prealloc_buf(nullptr)
                                     .buf_size(BUFFER_SIZE)
                                     .buf_shm_key(-1)
                                     .build();

  cb = std::make_unique<ControlBlock>(lgid, ib_port_index, kHrdInvalidNUMANode,
                                      conn_config);

  // Announce the QPs
  for (size_t i = 0; i < num_proc; i++) {
    if (i == lgid) continue;

    char srv_name[QP_NAME_LENGTH];

    sprintf(srv_name, "broadcast-%zu-%zu", i, lgid);
    cb->publish_conn_qp(b_idx(i), srv_name);

    sprintf(srv_name, "replay-%zu-%zu", lgid, i);
    cb->publish_conn_qp(r_idx(i), srv_name);
  }

  // Connect to remote QPs
  for (size_t i = 0; i < num_proc; i++) {
    if (i == lgid) continue;

    char clt_name[QP_NAME_LENGTH];

    sprintf(clt_name, "broadcast-%zu-%zu", lgid, i);
    cb->connect_remote_qp(b_idx(i), clt_name);

    sprintf(clt_name, "replay-%zu-%zu", i, lgid);
    cb->connect_remote_qp(r_idx(i), clt_name);
  }

  // Wait till qps are ready
  for (size_t i = 0; i < num_proc; i++) {
    if (i == lgid) continue;

    char clt_name[QP_NAME_LENGTH];

    sprintf(clt_name, "broadcast-%zu-%zu", i, lgid);
    MemoryStore::getInstance().wait_till_ready(clt_name);

    sprintf(clt_name, "replay-%zu-%zu", lgid, i);
    MemoryStore::getInstance().wait_till_ready(clt_name);
  }

  for (size_t i = 0; i < num_proc; i++) {
    bcast_buf.push_back(
        std::make_unique<BroadcastBuffer>(cb->get_buf(b_idx(i)), BUFFER_SIZE));
  }

  replay_w_buf = std::make_unique<ReplayBufferWriter>(cb->get_buf(r_idx(lgid)),
                                                      BUFFER_SIZE, num_proc);

  replay_r_buf = std::make_unique<ReplayBufferReader>(
      &(cb->get_buf(r_idx(lgid))[BUFFER_SIZE]), BUFFER_SIZE, num_proc);

  printf("neb: Begin data path!\n");

  poller_thread = std::thread([=] { start_poller(); });
  poller_thread.detach();
}

/**
 * TODO(Kristian): DOC
 */
void NonEquivocatingBroadcast::broadcast(uint64_t k, Broadcastable &msg) {
  const auto next_idx = last[lgid] + 1;

  // TODO(Kristian): ideally directly write to the bcast_buf
  auto tmp = std::make_unique<volatile uint8_t[]>(BUFFER_ENTRY_SIZE);
  auto msg_size = msg.marshall(tmp.get());
  auto addr = bcast_buf[lgid]->write(next_idx, k, tmp.get(), msg_size);

  // Broadcast: write to every "broadcast-self-x" qp
  for (size_t i = 0; i < num_proc; i++) {
    if (i == lgid) continue;

    struct ibv_sge sg;
    memset(&sg, 0, sizeof(sg));

    sg.addr = addr;
    sg.length = msg_size + NEB_MSG_OVERHEAD;
    sg.lkey = cb->get_mr(b_idx(lgid))->lkey;

    post_write(sg, i, bcast_buf[lgid]->get_byte_offset(next_idx));
  }

  // increase the message counter
  last[lgid] = next_idx;
}

/**
 * TODO(Kristian): DOC
 */
void NonEquivocatingBroadcast::start_poller() {
  printf("neb: poller thread running\n");

  if (poller_running) return;

  poller_running = true;

  while (poller_running) {
    for (size_t i = 0; i < num_proc; i++) {
      if (i == lgid) continue;

      uint64_t next_index = last[i] + 1;

      auto bcast_entry = bcast_buf[i]->get_entry(next_index);

      // TODO(Kristian): eventually check for matching signature
      if (bcast_entry->id() == 0 || bcast_entry->id() != next_index) continue;

      printf("neb: bcast from %zu = (%lu, %lu)\n", i, bcast_entry->id(),
             *reinterpret_cast<volatile uint64_t *>(bcast_entry->content()));

      auto replay_entry_w = replay_w_buf->get_entry(i, next_index);

      // TODO(Kristian): make it a local RDMA write
      memcpy((void *)replay_entry_w->addr(), (void *)bcast_entry->addr(),
             BUFFER_ENTRY_SIZE);

      printf("neb: repl for %zu = (%lu, %lu)\n", i, replay_entry_w->id(),
             *reinterpret_cast<volatile uint64_t *>(replay_entry_w->content()));

      bool is_valid = true;
      // read replay slots for origin i
      // for now in every loop we trigger a blocking (until local senq queue has
      // processed the wr) read request.
      for (size_t j = 0; j < num_proc; j++) {
        if (j == lgid || j == i) {
          continue;
        }

        auto replay_entry_r = replay_r_buf->get_entry(i, j, next_index);

        struct ibv_sge sg;
        memset(&sg, 0, sizeof(sg));

        sg.addr = replay_entry_r->addr();
        sg.length = BUFFER_ENTRY_SIZE;
        sg.lkey = cb->get_mr(r_idx(lgid))->lkey;

        post_replay_read(sg, j, replay_w_buf->get_byte_offset(i, next_index));

        if (replay_entry_r->id() != 0 &&
            memcmp((void *)bcast_entry->addr(), (void *)replay_entry_r->addr(),
                   BUFFER_ENTRY_SIZE)) {
          is_valid = false;
        }

        printf(
            "neb: replay entry for %zu at %zu = (%lu,%lu)\n", i, j,
            replay_entry_r->id(),
            *reinterpret_cast<volatile uint64_t *>(replay_entry_r->content()));
      }

      if (is_valid) {
        deliver_callback(bcast_entry->id(), bcast_entry->content(), i);
        last[i] = next_index;
      }
    }
  }

  printf("neb: poller thread finishing\n");
}

/**
 * TODO(Kristian): DOC
 */
int NonEquivocatingBroadcast::post_write(ibv_sge sg, size_t dest_id,
                                         uint64_t msg_offset) {
  struct ibv_send_wr wr;
  // struct ibv_wc wc;
  struct ibv_send_wr *bad_wr = nullptr;

  memset(&wr, 0, sizeof(wr));
  wr.wr_id = 0;
  wr.sg_list = &sg;
  wr.num_sge = 1;
  wr.opcode = IBV_WR_RDMA_WRITE;
  wr.next = nullptr;
  // wr.send_flags = IBV_SEND_SIGNALED;
  wr.wr.rdma.remote_addr = cb->get_r_qp(b_idx(dest_id))->buf_addr + msg_offset;
  wr.wr.rdma.rkey = cb->get_r_qp(b_idx(dest_id))->rkey;

  printf("neb: Write over broadcast QP to %lu \n", dest_id);

  if (ibv_post_send(cb->get_qp(b_idx(dest_id)), &wr, &bad_wr)) {
    fprintf(stderr, "Error, ibv_post_send() failed\n");
    // TODO(Krisitan): properly handle err
    return -1;
  }

  if (bad_wr != nullptr) {
    printf("bad_wr is set!\n");
  }

  // hrd_poll_cq(cb->conn_cq[i * 2], 1, &wc);
  return 0;
}

/**
 * TODO(Kristian): DOC
 */
int NonEquivocatingBroadcast::post_replay_read(ibv_sge sg, size_t r_id,
                                               uint64_t msg_offset) {
  struct ibv_send_wr wr;
  struct ibv_send_wr *bad_wr = nullptr;
  struct ibv_wc wc;
  memset(&wr, 0, sizeof(wr));

  wr.sg_list = &sg;
  wr.num_sge = 1;
  wr.opcode = IBV_WR_RDMA_READ;
  // for now we want a signal so we don't flood the send queue with read
  // requests since the poller_thread is reading infinitelly until it sees a
  // value.
  wr.send_flags = IBV_SEND_SIGNALED;
  wr.wr.rdma.remote_addr = cb->get_r_qp(r_idx(r_id))->buf_addr + msg_offset;
  wr.wr.rdma.rkey = cb->get_r_qp(r_idx(r_id))->rkey;

  printf("neb: Posting replay read at %lu\n", r_id);

  if (ibv_post_send(cb->get_qp(r_idx(r_id)), &wr, &bad_wr)) {
    fprintf(stderr, "Error, ibv_post_send() failed\n");
    // TODO(Kristian): properly handle
    return -1;
  }

  hrd_poll_cq(cb->get_cq(r_idx(r_id)), 1, &wc);

  if (bad_wr != nullptr) {
    printf("bad_wr is set!\n");
  }

  return 0;
}