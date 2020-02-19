#include "neb.hpp"

NonEquivocatingBroadcast::~NonEquivocatingBroadcast() {
  poller_running = false;

  delete cb;

  for (size_t i = 0; i < num_proc; i++) {
    if (i == lgid) continue;

    free(r_bcst_qps[i]);
    free(r_repl_qps[i]);
  }

  free(r_bcst_qps);
  free(r_repl_qps);
}

NonEquivocatingBroadcast::NonEquivocatingBroadcast(size_t lgid, size_t num_proc)
    : lgid(lgid), num_proc(num_proc) {
  printf("neb: Begin control path\n");

  ConnectionConfig conn_config = ConnectionConfig::builder{}
                                     .set__max_rd_atomic(16)
                                     .set__sq_depth(kHrdSQDepth)
                                     .set__num_qps(num_proc)
                                     .set__use_uc(0)
                                     .set__prealloc_buf(nullptr)
                                     .set__buf_size(kAppBufSize)
                                     .set__buf_shm_key(-1)
                                     .build();

  r_bcst_qps = (hrd_qp_attr_t **)calloc(num_proc, sizeof(hrd_qp_attr_t));
  r_repl_qps = (hrd_qp_attr_t **)calloc(num_proc, sizeof(hrd_qp_attr_t));

  cb = new ControlBlock(lgid, ib_port_index, kHrdInvalidNUMANode, conn_config);

  // Announce the QPs
  for (int i = 0; i < num_proc; i++) {
    if (i == num_proc) continue;

    char srv_name[kHrdQPNameSize];
    sprintf(srv_name, "broadcast-%d-%zu", i, lgid);
    cb->publish_conn_qp(i * 2, srv_name);
    printf("neb: Node %zu published broadcast slot for node %d\n", lgid, i);

    sprintf(srv_name, "replay-%zu-%d", lgid, i);
    cb->publish_conn_qp(i * 2 + 1, srv_name);
    printf("neb: Node %zu published replay slot for node %d\n", lgid, i);
  }

  for (int i = 0; i < num_proc; i++) {
    if (i == lgid) continue;

    char clt_name[kHrdQPNameSize];
    hrd_qp_attr_t *clt_qp = nullptr;

    // connect to broadcast
    sprintf(clt_name, "broadcast-%zu-%d", lgid, i);
    printf("neb: Looking for %s server\n", clt_name);

    while (clt_qp == nullptr) {
      clt_qp = hrd_get_published_qp(clt_name);
      if (clt_qp == nullptr) usleep(200000);
    }

    printf("neb: Server %s found server! Connecting..\n", clt_qp->name);
    cb->connect_remote_qp(i * 2, clt_qp);
    hrd_publish_ready(clt_qp->name);
    r_bcst_qps[i] = clt_qp;
    printf("neb: Server %s READY\n", clt_qp->name);

    // Connect to replay
    sprintf(clt_name, "replay-%d-%zu", i, lgid);
    printf("neb: Looking for %s server\n", clt_name);

    clt_qp = nullptr;
    while (clt_qp == nullptr) {
      clt_qp = hrd_get_published_qp(clt_name);
      if (clt_qp == nullptr) usleep(200000);
    }

    printf("neb: Server %s found server! Connecting..\n", clt_qp->name);
    cb->connect_remote_qp(i * 2 + 1, clt_qp);
    hrd_publish_ready(clt_qp->name);
    r_repl_qps[i] = clt_qp;
    printf("neb: Server %s READY\n", clt_qp->name);
  }

  // Wait till qps are ready
  for (int i = 0; i < num_proc; i++) {
    if (i == lgid) continue;

    char clt_name[kHrdQPNameSize];

    sprintf(clt_name, "broadcast-%d-%zu", i, lgid);
    hrd_wait_till_ready(clt_name);

    sprintf(clt_name, "replay-%zu-%d", lgid, i);
    hrd_wait_till_ready(clt_name);
  }

  printf("neb: Broadcast and replay connections established\n");

  poller_thread = std::thread([=] { start_poller(); });
  poller_thread.detach();
}

int NonEquivocatingBroadcast::broadcast(size_t m_id, size_t val) {
  // TODO(Kristian): Fixme
  char text[kHrdQPNameSize];
  sprintf(text, "Hello From %zu", lgid);

  struct neb_msg_t msg = {
      .id = m_id, .data = (void *)&text, .len = sizeof(text)};

  auto *own_buf = reinterpret_cast<volatile uint8_t *>(cb->conn_buf[lgid]);

  size_t msg_size = msg.marshall((uint8_t *)own_buf);

  // Broadcast: write to every "broadcast-self-x" qp
  for (int i = 0; i < num_proc; i++) {
    if (i == lgid) continue;

    const size_t offset = 256;

    struct ibv_sge sg;
    struct ibv_send_wr wr;
    // struct ibv_wc wc;
    struct ibv_send_wr *bad_wr = nullptr;

    memset(&sg, 0, sizeof(sg));
    sg.length = msg_size;
    sg.addr = reinterpret_cast<uint64_t>(own_buf);
    sg.lkey = cb->conn_buf_mr[lgid]->lkey;

    memset(&wr, 0, sizeof(wr));
    wr.wr_id = 0;
    wr.sg_list = &sg;
    wr.num_sge = 1;
    wr.opcode = IBV_WR_RDMA_WRITE;
    wr.next = nullptr;
    // wr.send_flags = IBV_SEND_SIGNALED;
    wr.wr.rdma.remote_addr = r_bcst_qps[i]->buf_addr;
    wr.wr.rdma.rkey = r_bcst_qps[i]->rkey;

    printf("main: Write over broadcast QP to %d\n", i);

    if (ibv_post_send(cb->conn_qp[i * 2], &wr, &bad_wr)) {
      fprintf(stderr, "Error, ibv_post_send() failed\n");
      return -1;
    }

    if (bad_wr != nullptr) {
      printf("bad_wr is set!\n");
    }

    // hrd_poll_cq(cb->conn_cq[i * 2], 1, &wc);
  }
}

void NonEquivocatingBroadcast::start_poller() {
  printf("main: poller thread running\n");
  if (poller_running) return;
  poller_running = true;

  while (poller_running) {
    for (int i = 0; i < num_proc; i++) {
      if (i == lgid) continue;

      const size_t offset = 1024;

      auto *bcast_buf =
          reinterpret_cast<volatile uint8_t *>(cb->conn_buf[i * 2]);

      neb_msg_t msg;

      msg.unmarshall((uint8_t *)bcast_buf);

      // TODO(Kristian): add more checks like matching next id etc.
      if (msg.id != 0) {
        printf("main: bcast from %d = %s\n", i, (char *)msg.data);

        auto *repl_buf =
            reinterpret_cast<volatile uint8_t *>(cb->conn_buf[i * 2 + 1]);

        memcpy((void *)&repl_buf[i * msg.size()], (void *)bcast_buf,
               msg.size());

        printf("main: copying to replay-buffer\n");

        // read replay slots for origin i
        for (int j = 0; j < num_proc; j++) {
          if (j == lgid || j == i) {
            continue;
          }

          struct ibv_sge sg;
          struct ibv_send_wr wr;
          // struct ibv_wc wc;
          struct ibv_send_wr *bad_wr = nullptr;

          memset(&sg, 0, sizeof(sg));
          sg.addr =
              reinterpret_cast<uint64_t>(cb->conn_buf[i * 2 + 1]) +
              (offset + (i * num_proc + j) * msg.size()) * sizeof(uint8_t);
          sg.length = msg.size();
          sg.lkey = cb->conn_buf_mr[i * 2 + 1]->lkey;

          memset(&wr, 0, sizeof(wr));
          // wr.wr_id      = 0;
          wr.sg_list = &sg;
          wr.num_sge = 1;
          wr.opcode = IBV_WR_RDMA_READ;
          // wr.send_flags = IBV_SEND_SIGNALED;
          wr.wr.rdma.remote_addr = r_repl_qps[j]->buf_addr + i * msg.size();
          wr.wr.rdma.rkey = r_repl_qps[j]->rkey;

          printf("main: Posting replay read for %d at %d\n", i, j);

          if (ibv_post_send(cb->conn_qp[j * 2 + 1], &wr, &bad_wr)) {
            fprintf(stderr, "Error, ibv_post_send() failed\n");
            // TODO(Kristian): properly handle
            return;
          }

          // hrd_poll_cq(cb->conn_cq[j * 2 + 1], 1, &wc);

          if (bad_wr != nullptr) {
            printf("bad_wr is set!\n");
          }

          neb_msg_t r_msg;

          r_msg.unmarshall(
              (uint8_t *)&repl_buf[(offset + (i * num_proc + j) * msg.size())]);
          printf("main: replay entry for %d at %d = %s\n", i, j,
                 (char *)r_msg.data);
        }
      }
    }

    sleep(1);
  }

  printf("neb: poller thread finishing\n");
}