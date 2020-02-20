#include "ctrl_block.hpp"
#include "store_conn.hpp"

ControlBlock::~ControlBlock() {
  printf("ctb: Destroying control block %lu\n", lgid);

  // Destroy QPs and CQs. QPs must be destroyed before CQs.
  for (size_t i = 0; i < conn_config.num_qps; i++) {
    rt_assert(ibv_destroy_qp(conn_qp[i]) == 0, "Failed to destroy QP");
    rt_assert(ibv_destroy_cq(conn_cq[i]) == 0, "Failed to destroy CQ");
  }

  for (size_t i = 0; i < conn_config.num_qps; i++) {
    assert(conn_buf_mr[i] != nullptr);

    // Destroy memory regions
    if (ibv_dereg_mr(conn_buf_mr[i])) {
      fprintf(stderr, "ctb: Couldn't deregister conn MR for cb %zu\n", lgid);
      // TODO(Kristian): handle me
      return;
    }

    // Free memory buffer
    // Replay QPs share all the same buffer
    if (i % 2 == 0 || i == 1) {
      free(const_cast<uint8_t *>(conn_buf[i]));
    }
  }

  // Free remote QP attributes
  for (size_t i = 0; i < conn_config.num_qps; i++) {
    free(r_qps[i]);
  }

  // Destroy protection domain
  rt_assert(ibv_dealloc_pd(pd) == 0, "Failed to dealloc PD");

  // Destroy device context
  rt_assert(ibv_close_device(resolve.ib_ctx) == 0, "Failed to close device");

  printf("ctb: Control block %zu destroyed.\n", lgid);
  return;
}

ControlBlock::ControlBlock(size_t lgid, size_t port_index, size_t numa_node,
                           ConnectionConfig conn_config)
    : lgid(lgid),
      port_index(port_index),
      numa_node(numa_node),
      conn_config(conn_config) {
  printf("ctb: Begin control path\n");
  printf("ctb: creating control block %zu: port %zu, socket %zu.\n", lgid,
         port_index, numa_node);
  printf("ctb: control block %zu: Conn config = %s\n", lgid,
         conn_config.to_string().c_str());

  assert(port_index <= 16);
  assert(numa_node <= kHrdInvalidNUMANode);
  assert(conn_config.num_qps >= 1);

  if (conn_config.prealloc_buf != nullptr) {
    assert(conn_config.buf_shm_key == -1);
  }

  if (conn_config.prealloc_buf != nullptr) {
    rt_assert(false, "We don't support providing the allocated memory");
  }

  if (numa_node != kHrdInvalidNUMANode) {
    rt_assert(false,
              "We don't support neither hugepages, nor numa aware memory "
              "allocation");
  }

  r_qps = std::make_unique<hrd_qp_attr_t *[]>(conn_config.num_qps);
  conn_buf = std::make_unique<volatile uint8_t *[]>(conn_config.num_qps);
  conn_qp = std::make_unique<ibv_qp *[]>(conn_config.num_qps);
  conn_cq = std::make_unique<ibv_cq *[]>(conn_config.num_qps);
  conn_buf_mr = std::make_unique<ibv_mr *[]>(conn_config.num_qps);

  // Resolve Port Index
  resolve = hrd_resolve_port_index(port_index);
  assert(resolve.ib_ctx != nullptr && resolve.dev_port_id >= 1);

  // Create PD
  pd = ibv_alloc_pd(resolve.ib_ctx);
  assert(pd != nullptr);

  // Create RC QPs
  create_conn_qps();

  // Create Buffers and register MRs
  size_t reg_size = conn_config.buf_size;
  // use one replay buffer for all replay QPs
  // TODO(Kristian): this custom logic should ideally not be here
  auto *replay_buf =
      reinterpret_cast<volatile uint8_t *>(memalign(4096, reg_size));

  for (size_t i = 0; i < conn_config.num_qps; i++) {
    conn_buf[i] =
        i % 2 != 0
            ? replay_buf
            : reinterpret_cast<volatile uint8_t *>(memalign(4096, reg_size));

    assert(conn_buf[i] != nullptr);
    memset(const_cast<uint8_t *>(conn_buf[i]), 0, reg_size);

    // Even ids are used for broadcast-p-q
    int ib_flags = i % 2 == 0 ? IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE
                              : IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ;

    conn_buf_mr[i] =
        ibv_reg_mr(pd, const_cast<uint8_t *>(conn_buf[i]), reg_size, ib_flags);

    if (conn_buf_mr[i] == nullptr) {
      printf("Buffer reg %lu failed with code %s\n", i, strerror(errno));
      exit(-1);
    }
  }
}

void ControlBlock::publish_conn_qp(size_t idx, const char *qp_name) {
  assert(idx < conn_config.num_qps);
  assert(strlen(qp_name) < QP_NAME_SIZE - 1);
  assert(strstr(qp_name, RESERVED_NAME_PREFIX) == nullptr);

  size_t len = strlen(qp_name);
  for (size_t i = 0; i < len; i++) assert(qp_name[i] != ' ');

  hrd_qp_attr_t qp_attr;
  memset(&qp_attr, 0, sizeof(hrd_qp_attr_t));

  strcpy(qp_attr.name, qp_name);
  qp_attr.lid = resolve.port_lid;
  qp_attr.qpn = conn_qp[idx]->qp_num;
  if (kRoCE) qp_attr.gid = resolve.gid;

  qp_attr.buf_addr = reinterpret_cast<uint64_t>(conn_buf[idx]);
  qp_attr.buf_size = conn_config.buf_size;
  qp_attr.rkey = conn_buf_mr[idx]->rkey;

  MemoryStore::getInstance().set(qp_attr.name, &qp_attr, sizeof(hrd_qp_attr_t));

  printf("ctb: Published %s\n", qp_name);
}

void ControlBlock::connect_remote_qp(size_t idx, const char *qp_name) {
  assert(idx < conn_config.num_qps);
  assert(conn_qp[idx] != nullptr);
  assert(resolve.dev_port_id >= 1);

  printf("ctb: Looking for server %s.\n", qp_name);

  hrd_qp_attr_t *remote_qp = nullptr;
  while (remote_qp == nullptr) {
    remote_qp = MemoryStore::getInstance().get_qp(qp_name);
    if (remote_qp == nullptr) usleep(200000);
  }

  printf("ctb: Found server %s! Connecting..\n", qp_name);

  r_qps[idx] = remote_qp;

  struct ibv_qp_attr conn_attr;
  memset(&conn_attr, 0, sizeof(struct ibv_qp_attr));
  conn_attr.qp_state = IBV_QPS_RTR;
  conn_attr.path_mtu = IBV_MTU_4096;
  conn_attr.dest_qp_num = remote_qp->qpn;
  conn_attr.rq_psn = kHrdDefaultPSN;

  conn_attr.ah_attr.is_global = kRoCE ? 1 : 0;
  conn_attr.ah_attr.dlid = kRoCE ? 0 : remote_qp->lid;
  conn_attr.ah_attr.sl = 0;
  conn_attr.ah_attr.src_path_bits = 0;
  conn_attr.ah_attr.port_num = resolve.dev_port_id;  // Local port!

  if (kRoCE) {
    auto &grh = conn_attr.ah_attr.grh;
    grh.dgid.global.interface_id = remote_qp->gid.global.interface_id;
    grh.dgid.global.subnet_prefix = remote_qp->gid.global.subnet_prefix;

    grh.sgid_index = 0;
    grh.hop_limit = 1;
  }

  int rtr_flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
                  IBV_QP_RQ_PSN;

  if (!conn_config.use_uc) {
    conn_attr.max_dest_rd_atomic = conn_config.max_rd_atomic;
    conn_attr.min_rnr_timer = 12;
    rtr_flags |= IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;
  }

  if (ibv_modify_qp(conn_qp[idx], &conn_attr, rtr_flags)) {
    fprintf(stderr, "ctb: Failed to modify QP to RTR\n");
    assert(false);
  }

  memset(&conn_attr, 0, sizeof(conn_attr));
  conn_attr.qp_state = IBV_QPS_RTS;
  conn_attr.sq_psn = kHrdDefaultPSN;

  int rts_flags = IBV_QP_STATE | IBV_QP_SQ_PSN;

  if (!conn_config.use_uc) {
    conn_attr.timeout = 14;
    conn_attr.retry_cnt = 7;
    conn_attr.rnr_retry = 7;
    conn_attr.max_rd_atomic = conn_config.max_rd_atomic;
    conn_attr.max_dest_rd_atomic = conn_config.max_rd_atomic;
    rts_flags |= IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY |
                 IBV_QP_MAX_QP_RD_ATOMIC;
  }

  if (ibv_modify_qp(conn_qp[idx], &conn_attr, rts_flags)) {
    fprintf(stderr, "ctb: Failed to modify QP to RTS\n");
    assert(false);
  }

  MemoryStore::getInstance().set_qp_ready(remote_qp->name);
}

void ControlBlock::create_conn_qps() {
  assert(pd != nullptr && resolve.ib_ctx != nullptr);
  assert(conn_config.num_qps >= 1 && resolve.dev_port_id >= 1);

  for (size_t i = 0; i < conn_config.num_qps; i++) {
    conn_cq[i] = ibv_create_cq(resolve.ib_ctx, conn_config.sq_depth, nullptr,
                               nullptr, 0);
    // We sometimes set Mellanox env variables for hugepage-backed queues.
    rt_assert(conn_cq[i] != nullptr,
              "Failed to create conn CQ. Check hugepages and SHM limits?");

    struct ibv_qp_init_attr create_attr;
    memset(&create_attr, 0, sizeof(struct ibv_qp_init_attr));
    create_attr.send_cq = conn_cq[i];
    create_attr.recv_cq = conn_cq[i];
    create_attr.qp_type = conn_config.use_uc ? IBV_QPT_UC : IBV_QPT_RC;

    create_attr.cap.max_send_wr = conn_config.sq_depth;
    create_attr.cap.max_recv_wr = 1;  // We don't do RECVs on conn QPs
    create_attr.cap.max_send_sge = 1;
    create_attr.cap.max_recv_sge = 1;
    create_attr.cap.max_inline_data = kHrdMaxInline;

    conn_qp[i] = ibv_create_qp(pd, &create_attr);
    rt_assert(conn_qp[i] != nullptr, "Failed to create conn QP");

    struct ibv_qp_attr init_attr;
    memset(&init_attr, 0, sizeof(struct ibv_qp_attr));
    init_attr.qp_state = IBV_QPS_INIT;
    init_attr.pkey_index = 0;
    init_attr.port_num = resolve.dev_port_id;
    init_attr.qp_access_flags =
        conn_config.use_uc ? IBV_ACCESS_REMOTE_WRITE
                           : IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ |
                                 IBV_ACCESS_REMOTE_ATOMIC;

    if (ibv_modify_qp(conn_qp[i], &init_attr,
                      IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT |
                          IBV_QP_ACCESS_FLAGS)) {
      rt_assert(false, "Failed to modify conn QP to INIT\n");
    }
  }
}

ibv_qp *ControlBlock::get_qp(size_t idx) { return conn_qp[idx]; }

ibv_cq *ControlBlock::get_cq(size_t idx) { return conn_cq[idx]; }

ibv_mr *ControlBlock::get_mr(size_t idx) { return conn_buf_mr[idx]; }

volatile uint8_t *ControlBlock::get_buf(size_t idx) { return conn_buf[idx]; }

hrd_qp_attr_t *ControlBlock::get_r_qp(size_t idx) { return r_qps[idx]; }
