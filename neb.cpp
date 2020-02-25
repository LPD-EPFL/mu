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
    size_t lgid, size_t num_proc, void (*deliver_cb)(void *data))
    : lgid(lgid),
      num_proc(num_proc),
      last(std::make_unique<int[]>(num_proc)),
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
    if (i == num_proc) continue;

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

  printf("neb: Begin data path!\n");

  poller_thread = std::thread([=] { start_poller(); });
  poller_thread.detach();
}

/**
 * TODO(Kristian): DOC
 */
void NonEquivocatingBroadcast::broadcast(uint64_t msg_id, Broadcastable &msg) {
  auto *own_buf = reinterpret_cast<volatile uint64_t *>(cb->get_buf(lgid));

  size_t msg_size =
      msg.marshall(reinterpret_cast<volatile uint8_t *>(&own_buf[2]));
  own_buf[0] = msg_id;
  own_buf[1] = msg_size;

  // Broadcast: write to every "broadcast-self-x" qp
  for (size_t i = 0; i < num_proc; i++) {
    if (i == lgid) continue;

    struct ibv_sge sg;
    memset(&sg, 0, sizeof(sg));

    sg.length = msg_size + NEB_MSG_OVERHEAD;
    sg.addr = reinterpret_cast<uint64_t>(own_buf);
    sg.lkey = cb->get_mr(lgid)->lkey;

    post_write(sg, i);
  }

  return;
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

      // int k = last.get()[i];

      auto *bcast_buf =
          reinterpret_cast<volatile uint64_t *>(cb->get_buf(i * 2));

      // TODO(Kristian): add more checks like matching next id etc.
      if (bcast_buf[0] == 0) continue;
      printf("neb: bcast from %zu = %lu\n", i, bcast_buf[2]);

      int content_size = bcast_buf[1] + NEB_MSG_OVERHEAD;
      auto *repl_buf =
          reinterpret_cast<volatile uint8_t *>(cb->get_buf(r_idx(i)));

      // TODO(Kristian): make it a local RDMA write
      memcpy((void *)&repl_buf[i * BUFFER_ENTRY_SIZE], (void *)bcast_buf,
             BUFFER_ENTRY_SIZE);

      printf("neb: repl for %zu = %lu\n", i,
             reinterpret_cast<volatile uint64_t *>(
                 &repl_buf[i * BUFFER_ENTRY_SIZE])[2]);

      // read replay slots for origin i
      for (size_t j = 0; j < num_proc; j++) {
        if (j == lgid || j == i) {
          continue;
        }

        // TODO(Kristian): Remove this and split replay buffer memory in two
        // regions
        const size_t offset = 2048;

        struct ibv_sge sg;
        memset(&sg, 0, sizeof(sg));
        // TODO(Kristian): where do we want to store the read values?
        sg.addr = reinterpret_cast<uint64_t>(cb->get_buf(r_idx(i))) +
                  (offset + (i * num_proc + j) * BUFFER_ENTRY_SIZE);
        sg.length = content_size;
        sg.lkey = cb->get_mr(r_idx(i))->lkey;

        post_replay_read(sg, j, i);

        printf("neb: replay entry for %zu at %zu = %lu\n", i, j,
               reinterpret_cast<volatile uint64_t *>(
                   &repl_buf[offset + (i * num_proc + j) * BUFFER_ENTRY_SIZE
        ])[2]);

        // TODO: after proper read and validation call deliver_callback
      }
    }

    sleep(1);
  }

  printf("neb: poller thread finishing\n");
}

/**
 * TODO(Kristian): DOC
 */
int NonEquivocatingBroadcast::post_write(ibv_sge sg, size_t proc_id) {
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
  wr.wr.rdma.remote_addr = cb->get_r_qp(b_idx(proc_id))->buf_addr;
  wr.wr.rdma.rkey = cb->get_r_qp(b_idx(proc_id))->rkey;

  printf("neb: Write over broadcast QP to %lu \n", proc_id);

  if (ibv_post_send(cb->get_qp(b_idx(proc_id)), &wr, &bad_wr)) {
    fprintf(stderr, "Error, ibv_post_send() failed\n");
    // TODO(Krisitan): properly handle err
    return -1;
  }

  if (bad_wr != nullptr) {
    printf("bad_wr is set!\n");
  }

  return 0;
  // hrd_poll_cq(cb->conn_cq[i * 2], 1, &wc);
}

/**
 * TODO(Kristian): DOC
 */
int NonEquivocatingBroadcast::post_replay_read(ibv_sge sg, size_t d_id,
                                               size_t o_id) {
  struct ibv_send_wr wr;
  struct ibv_send_wr *bad_wr = nullptr;
  // struct ibv_wc wc;
  memset(&wr, 0, sizeof(wr));

  wr.sg_list = &sg;
  wr.num_sge = 1;
  wr.opcode = IBV_WR_RDMA_READ;
  // wr.send_flags = IBV_SEND_SIGNALED;
  wr.wr.rdma.remote_addr =
      cb->get_r_qp(r_idx(d_id))->buf_addr + o_id * BUFFER_ENTRY_SIZE;
  wr.wr.rdma.rkey = cb->get_r_qp(r_idx(d_id))->rkey;

  printf("neb: Posting replay read at %lu\n", d_id);

  if (ibv_post_send(cb->get_qp(r_idx(d_id)), &wr, &bad_wr)) {
    fprintf(stderr, "Error, ibv_post_send() failed\n");
    // TODO(Kristian): properly handle
    return -1;
  }

  // hrd_poll_cq(cb->get_cq(r_idx(j)), 1, &wc);

  if (bad_wr != nullptr) {
    printf("bad_wr is set!\n");
  }

  return 0;
}