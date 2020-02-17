#include "hrd.hpp"

#include <cstdlib>

static const size_t ib_port_index = 0;
static constexpr size_t kAppBufSize = (8 * 1024);

static size_t num_nodes = 4;
static size_t local_gid;
// TODO-Q(Kristian): do we need this to access the remote addr?
static struct hrd_qp_attr_t **bcst_qps;
static struct hrd_qp_attr_t **repl_qps;
static struct hrd_ctrl_blk_t *cb;

int run_poller() {
  printf("main: poller thread running\n");

  while (true) {
    for (int i = 0; i < num_nodes; i++) {
      if (i == local_gid) {
        continue;
      }

      const int offset = 128;

      auto *val = reinterpret_cast<volatile uint64_t *>(cb->conn_buf[i * 2]);

      if (*val != 0) {
        printf("main: bcast from %d = %zu\n", i, *val);
        auto *repl_buf =
            reinterpret_cast<volatile uint64_t *>(cb->conn_buf[i * 2 + 1]);
        repl_buf[i] = *val;

        printf("main: set replay_buf[%d] = %lu\n", i, repl_buf[i]);

        // read replay slots for origin i
        for (int j = 0; j < num_nodes; j++) {
          if (j == local_gid || j == i) {
            continue;
          }

          struct ibv_sge sg;
          struct ibv_send_wr wr;
          struct ibv_wc wc;
          struct ibv_send_wr *bad_wr = nullptr;

          memset(&sg, 0, sizeof(sg));
          sg.addr = reinterpret_cast<uint64_t>(cb->conn_buf[i * 2 + 1]) +
                    (offset + i * num_nodes + j) * sizeof(uint64_t);
          sg.length = sizeof(uint64_t);
          sg.lkey = cb->conn_buf_mr[i * 2 + 1]->lkey;

          memset(&wr, 0, sizeof(wr));
          // wr.wr_id      = 0;
          wr.sg_list = &sg;
          wr.num_sge = 1;
          wr.opcode = IBV_WR_RDMA_READ;
          wr.send_flags = IBV_SEND_SIGNALED;
          wr.wr.rdma.remote_addr = repl_qps[j]->buf_addr + i * sizeof(uint64_t);
          wr.wr.rdma.rkey = repl_qps[j]->rkey;

          printf("main: Posting replay read for %d at %d\n", i, j);

          if (ibv_post_send(cb->conn_qp[j * 2 + 1], &wr, &bad_wr)) {
            fprintf(stderr, "Error, ibv_post_send() failed\n");
            return -1;
          }

          hrd_poll_cq(cb->conn_cq[j * 2 + 1], 1, &wc);

          if (bad_wr != nullptr) {
            printf("bad_wr is set!\n");
          }

          printf("main: replay entry in loop for %d at %d = %zu\n", i, j,
                 repl_buf[(offset + i * num_nodes + j)]);
        }
      }
    }

    sleep(1);
  }
}

/*
 * NOTE: we assume IDs starting from 0
 */
int main(int argc, char *argv[]) {
  rt_assert(argc > 1);

  num_nodes = atoi(argv[1]);
  local_gid = atoi(argv[2]);

  bcst_qps = (hrd_qp_attr_t **)malloc(num_nodes * sizeof(hrd_qp_attr_t));
  repl_qps = (hrd_qp_attr_t **)malloc(num_nodes * sizeof(hrd_qp_attr_t));

  hrd_ibv_devinfo();

  printf("main: Begin control path\n");

  struct hrd_conn_config_t conn_config;

  conn_config.num_qps = num_nodes;
  conn_config.use_uc = 0;
  conn_config.prealloc_buf = nullptr;
  conn_config.buf_size = kAppBufSize;
  conn_config.buf_shm_key = -1;

  cb = hrd_ctrl_blk_init(local_gid, ib_port_index, kHrdInvalidNUMANode,
                         &conn_config);

  // Announce the QPs
  for (int i = 0; i < num_nodes; i++) {
    if (i == local_gid) {
      continue;
    }

    char srv_name[kHrdQPNameSize];
    sprintf(srv_name, "broadcast-%d-%zu", i, local_gid);
    printf("main: Node %zu published broadcast slot for node %d\n", local_gid,
           i);
    hrd_publish_conn_qp(cb, i * 2, srv_name);

    sprintf(srv_name, "replay-%zu-%d", local_gid, i);
    printf("main: Node %zu published replay slot for node %d\n", local_gid, i);
    hrd_publish_conn_qp(cb, i * 2 + 1, srv_name);
  }

  // Connect to "boradcast-self-x" QP and "replay-x-self" QP published by x
  for (int i = 0; i < num_nodes; i++) {
    if (i == local_gid) {
      continue;
    }

    char clt_name[kHrdQPNameSize];
    hrd_qp_attr_t *clt_qp = nullptr;

    // connect to broadcast
    sprintf(clt_name, "broadcast-%zu-%d", local_gid, i);
    printf("main: Looking for %s server\n", clt_name);

    while (clt_qp == nullptr) {
      clt_qp = hrd_get_published_qp(clt_name);
      if (clt_qp == nullptr) usleep(200000);
    }

    printf("main: Server %s found server! Connecting..\n", clt_qp->name);
    hrd_connect_qp(cb, i * 2, clt_qp);
    // This garbles the server's qp_attr - which is safe
    hrd_publish_ready(clt_qp->name);
    bcst_qps[i] = clt_qp;
    printf("main: Server %s READY\n", clt_qp->name);

    // Connect to replay
    sprintf(clt_name, "replay-%d-%zu", i, local_gid);
    printf("main: Looking for %s server\n", clt_name);

    clt_qp = nullptr;
    while (clt_qp == nullptr) {
      clt_qp = hrd_get_published_qp(clt_name);
      if (clt_qp == nullptr) usleep(200000);
    }

    printf("main: Server %s found server! Connecting..\n", clt_qp->name);
    hrd_connect_qp(cb, i * 2 + 1, clt_qp);
    // This garbles the server's qp_attr - which is safe
    hrd_publish_ready(clt_qp->name);
    repl_qps[i] = clt_qp;
    printf("main: Server %s READY\n", clt_qp->name);
  }

  // Wait till qps are ready
  for (int i = 0; i < num_nodes; i++) {
    if (i == local_gid) {
      continue;
    }

    char clt_name[kHrdQPNameSize];

    sprintf(clt_name, "broadcast-%d-%zu", i, local_gid);
    hrd_wait_till_ready(clt_name);

    sprintf(clt_name, "replay-%zu-%d", local_gid, i);
    hrd_wait_till_ready(clt_name);
  }

  printf("main: Broadcast and replay connections established\n");
  printf("main: Begin data path\n");

  std::thread poller_thread = std::thread(run_poller);

  // Broadcast: write to every "broadcast-self-x" qp
  for (int i = 0; i < num_nodes; i++) {
    if (i == local_gid) {
      continue;
    }

    const int offset = 128;

    struct ibv_sge sg;
    struct ibv_send_wr wr;
    // struct ibv_wc wc;
    struct ibv_send_wr *bad_wr = nullptr;

    auto *buf = reinterpret_cast<volatile uint64_t *>(cb->conn_buf[i * 2]);
    buf[offset] = local_gid + 100;

    memset(&sg, 0, sizeof(sg));
    sg.length = sizeof(uint64_t);
    sg.addr = reinterpret_cast<uint64_t>(cb->conn_buf[i * 2]) +
              offset * sizeof(uint64_t);
    sg.lkey = cb->conn_buf_mr[i * 2]->lkey;

    memset(&wr, 0, sizeof(wr));
    wr.wr_id = 0;
    wr.sg_list = &sg;
    wr.num_sge = 1;
    wr.opcode = IBV_WR_RDMA_WRITE;
    wr.next = nullptr;
    // wr.send_flags = IBV_SEND_SIGNALED;
    wr.wr.rdma.remote_addr = bcst_qps[i]->buf_addr;
    wr.wr.rdma.rkey = bcst_qps[i]->rkey;

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

  poller_thread.join();

  return 0;
}
