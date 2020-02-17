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

struct neb_msg_t {
  uint64_t id;
  void *data;
  size_t len;

  size_t marshall(uint8_t *buf) {
    size_t data_size = len * sizeof(uint8_t);
    size_t msg_size = 2 * sizeof(id) + data_size;

    buf[0 * sizeof(uint64_t)] = id;
    buf[1 * sizeof(uint64_t)] = data_size;

    memcpy(&buf[2 * sizeof(uint64_t)], data, msg_size);

    return msg_size;
  };

  void unmarshall(uint8_t *buf) {
    id = (uint64_t)buf[0 * sizeof(uint64_t)];
    len = (uint64_t)buf[1 * sizeof(uint64_t)];
    data = (void *)&buf[2 * sizeof(uint64_t)];
  }

  size_t size() { return 2 * sizeof(id) + len; }
};

int run_poller() {
  printf("main: poller thread running\n");

  while (true) {
    for (int i = 0; i < num_nodes; i++) {
      if (i == local_gid) {
        continue;
      }

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
        for (int j = 0; j < num_nodes; j++) {
          if (j == local_gid || j == i) {
            continue;
          }

          struct ibv_sge sg;
          struct ibv_send_wr wr;
          // struct ibv_wc wc;
          struct ibv_send_wr *bad_wr = nullptr;

          memset(&sg, 0, sizeof(sg));
          sg.addr =
              reinterpret_cast<uint64_t>(cb->conn_buf[i * 2 + 1]) +
              (offset + (i * num_nodes + j) * msg.size()) * sizeof(uint8_t);
          sg.length = msg.size();
          sg.lkey = cb->conn_buf_mr[i * 2 + 1]->lkey;

          memset(&wr, 0, sizeof(wr));
          // wr.wr_id      = 0;
          wr.sg_list = &sg;
          wr.num_sge = 1;
          wr.opcode = IBV_WR_RDMA_READ;
          // wr.send_flags = IBV_SEND_SIGNALED;
          wr.wr.rdma.remote_addr = repl_qps[j]->buf_addr + i * msg.size();
          wr.wr.rdma.rkey = repl_qps[j]->rkey;

          printf("main: Posting replay read for %d at %d\n", i, j);

          if (ibv_post_send(cb->conn_qp[j * 2 + 1], &wr, &bad_wr)) {
            fprintf(stderr, "Error, ibv_post_send() failed\n");
            return -1;
          }

          // hrd_poll_cq(cb->conn_cq[j * 2 + 1], 1, &wc);

          if (bad_wr != nullptr) {
            printf("bad_wr is set!\n");
          }

          neb_msg_t r_msg;

          r_msg.unmarshall((uint8_t *)&repl_buf[(offset + (i * num_nodes + j) * msg.size())]);
          printf("main: replay entry for %d at %d = %s\n", i, j,
                 (char *)r_msg.data);
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

  local_gid = atoi(argv[1]);

  // otherwise default is 4
  if (argc == 3) {
    num_nodes = atoi(argv[2]);
  }

  bcst_qps = (hrd_qp_attr_t **)malloc(num_nodes * sizeof(hrd_qp_attr_t));
  repl_qps = (hrd_qp_attr_t **)malloc(num_nodes * sizeof(hrd_qp_attr_t));

  // hrd_ibv_devinfo();

  // ---------------------------------------------------------------------------
  // ------------------------------ CONTROL PATH -------------------------------
  // ---------------------------------------------------------------------------

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
    hrd_publish_conn_qp(cb, i * 2, srv_name);
    printf("main: Node %zu published broadcast slot for node %d\n", local_gid,
           i);

    sprintf(srv_name, "replay-%zu-%d", local_gid, i);
    hrd_publish_conn_qp(cb, i * 2 + 1, srv_name);
    printf("main: Node %zu published replay slot for node %d\n", local_gid, i);
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

  // ---------------------------------------------------------------------------
  // -------------------------------- DATA PATH --------------------------------
  // ---------------------------------------------------------------------------
  printf("main: Broadcast and replay connections established\n");
  printf("main: Begin data path\n");

  std::thread poller_thread = std::thread(run_poller);

  char text[kHrdQPNameSize];
  sprintf(text, "Hello From %zu", local_gid);

  printf("%s", text);
  struct neb_msg_t msg = {.id = 1, .data = (void *)&text, .len = sizeof(text)};

  // Broadcast: write to every "broadcast-self-x" qp
  for (int i = 0; i < num_nodes; i++) {
    if (i == local_gid) {
      continue;
    }

    const size_t offset = 256;

    struct ibv_sge sg;
    struct ibv_send_wr wr;
    // struct ibv_wc wc;
    struct ibv_send_wr *bad_wr = nullptr;

    auto *buf =
        reinterpret_cast<volatile uint8_t *>(cb->conn_buf[i * 2]) + offset;

    size_t msg_size = msg.marshall((uint8_t *)buf);

    memset(&sg, 0, sizeof(sg));
    sg.length = msg_size;
    sg.addr = reinterpret_cast<uint64_t>(cb->conn_buf[i * 2]) +
              offset * sizeof(uint8_t);
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
