#include "neb.hpp"

NonEquivocatingBroadcast::~NonEquivocatingBroadcast() {
  poller_running = false;

  // close memcached connection
  hrd_close_memcached();
}

NonEquivocatingBroadcast::NonEquivocatingBroadcast(size_t lgid, size_t num_proc)
    : lgid(lgid), num_proc(num_proc) {
  ConnectionConfig conn_config = ConnectionConfig::builder{}
                                     .set__max_rd_atomic(16)
                                     .set__sq_depth(kHrdSQDepth)
                                     .set__num_qps(num_proc * 2)
                                     .set__use_uc(0)
                                     .set__prealloc_buf(nullptr)
                                     .set__buf_size(kAppBufSize)
                                     .set__buf_shm_key(-1)
                                     .build();

  cb = std::unique_ptr<ControlBlock>(
      new ControlBlock(lgid, ib_port_index, kHrdInvalidNUMANode, conn_config));

  // Announce the QPs
  for (int i = 0; i < num_proc; i++) {
    if (i == num_proc) continue;

    char srv_name[QP_NAME_SIZE];

    sprintf(srv_name, "broadcast-%d-%zu", i, lgid);
    cb->publish_conn_qp(i * 2, srv_name);

    sprintf(srv_name, "replay-%zu-%d", lgid, i);
    cb->publish_conn_qp(i * 2 + 1, srv_name);
  }

  // Connect to remote QPs
  for (int i = 0; i < num_proc; i++) {
    if (i == lgid) continue;

    char clt_name[QP_NAME_SIZE];

    sprintf(clt_name, "broadcast-%zu-%d", lgid, i);
    cb->connect_remote_qp(i * 2, clt_name);

    sprintf(clt_name, "replay-%d-%zu", i, lgid);
    cb->connect_remote_qp(i * 2 + 1, clt_name);
  }

  // Wait till qps are ready
  for (int i = 0; i < num_proc; i++) {
    if (i == lgid) continue;

    char clt_name[QP_NAME_SIZE];

    sprintf(clt_name, "broadcast-%d-%zu", i, lgid);
    hrd_wait_till_ready(clt_name);

    sprintf(clt_name, "replay-%zu-%d", lgid, i);
    hrd_wait_till_ready(clt_name);
  }

  printf("neb: Begin data path!\n");

  poller_thread = std::thread([=] { start_poller(); });
  poller_thread.detach();
}

void NonEquivocatingBroadcast::broadcast(size_t m_id, size_t val) {
  // TODO(Kristian): Fixme
  char text[QP_NAME_SIZE];
  sprintf(text, "Hello From %zu", lgid);

  struct neb_msg_t msg = {
      .id = m_id, .data = (void *)&text, .len = sizeof(text)};

  auto *own_buf =
      reinterpret_cast<volatile uint8_t *>(cb->conn_buf.get()[lgid]);

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
    sg.lkey = cb->conn_buf_mr.get()[lgid]->lkey;

    memset(&wr, 0, sizeof(wr));
    wr.wr_id = 0;
    wr.sg_list = &sg;
    wr.num_sge = 1;
    wr.opcode = IBV_WR_RDMA_WRITE;
    wr.next = nullptr;
    // wr.send_flags = IBV_SEND_SIGNALED;
    wr.wr.rdma.remote_addr = cb->r_qps.get()[i * 2]->buf_addr;
    wr.wr.rdma.rkey = cb->r_qps.get()[i * 2]->rkey;

    printf("neb: Write over broadcast QP to %d\n", i);

    if (ibv_post_send(cb->conn_qp.get()[i * 2], &wr, &bad_wr)) {
      fprintf(stderr, "Error, ibv_post_send() failed\n");
      // TODO(Krisitan): properly handle err
      return;
    }

    if (bad_wr != nullptr) {
      printf("bad_wr is set!\n");
    }

    // hrd_poll_cq(cb->conn_cq[i * 2], 1, &wc);
  }

  return;
}

void NonEquivocatingBroadcast::start_poller() {
  printf("neb: poller thread running\n");
  if (poller_running) return;
  poller_running = true;

  while (poller_running) {
    for (int i = 0; i < num_proc; i++) {
      if (i == lgid) continue;

      const size_t offset = 1024;

      auto *bcast_buf =
          reinterpret_cast<volatile uint8_t *>(cb->conn_buf.get()[i * 2]);

      neb_msg_t msg;

      msg.unmarshall((uint8_t *)bcast_buf);

      // TODO(Kristian): add more checks like matching next id etc.
      if (msg.id != 0) {
        printf("neb: bcast from %d = %s\n", i, (char *)msg.data);

        auto *repl_buf =
            reinterpret_cast<volatile uint8_t *>(cb->conn_buf.get()[i * 2 + 1]);

        memcpy((void *)&repl_buf[i * msg.size()], (void *)bcast_buf,
               msg.size());

        printf("neb: copying to replay-buffer\n");

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
              reinterpret_cast<uint64_t>(cb->conn_buf.get()[i * 2 + 1]) +
              (offset + (i * num_proc + j) * msg.size()) * sizeof(uint8_t);
          sg.length = msg.size();
          sg.lkey = cb->conn_buf_mr.get()[i * 2 + 1]->lkey;

          memset(&wr, 0, sizeof(wr));
          // wr.wr_id      = 0;
          wr.sg_list = &sg;
          wr.num_sge = 1;
          wr.opcode = IBV_WR_RDMA_READ;
          // wr.send_flags = IBV_SEND_SIGNALED;
          wr.wr.rdma.remote_addr =
              cb->r_qps.get()[j * 2 + 1]->buf_addr + i * msg.size();
          wr.wr.rdma.rkey = cb->r_qps.get()[j * 2 + 1]->rkey;

          printf("neb: Posting replay read for %d at %d\n", i, j);

          if (ibv_post_send(cb->conn_qp.get()[j * 2 + 1], &wr, &bad_wr)) {
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
          printf("neb: replay entry for %d at %d = %s\n", i, j,
                 (char *)r_msg.data);
        }
      }
    }

    sleep(1);
  }

  printf("neb: poller thread finishing\n");
}