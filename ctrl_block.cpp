#include "ctrl_block.hpp"

ControlBlock::ControlBlock(size_t lgid, size_t port_index, size_t numa_node,
                           ConnectionConfig conn_config)
    : lgid(lgid),
      port_index(port_index),
      numa_node(numa_node),
      conn_config(conn_config) {
  printf("HRD: creating control block %zu: port %zu, socket %zu.\n", lgid,
         port_index, numa_node);

  printf("HRD: control block %zu: Conn config = %s\n", lgid,
         conn_config.to_string().c_str());

  assert(port_index <= 16);
  assert(numa_node <= kHrdInvalidNUMANode);
  assert(conn_config.num_qps >= 1);

  if (conn_config.prealloc_buf != nullptr) {
    assert(conn_config.buf_shm_key == -1);
  }

  conn_buf =
      (volatile uint8_t **)calloc(2 * conn_config.num_qps, sizeof(uint8_t *));
  conn_qp = (ibv_qp **)calloc(2 * conn_config.num_qps, sizeof(ibv_qp));
  conn_cq = (ibv_cq **)calloc(2 * conn_config.num_qps, sizeof(ibv_cq));
  conn_buf_mr = (ibv_mr **)calloc(2 * conn_config.num_qps, sizeof(ibv_mr *));

  // ---------------------------------------------------------------------------
  // ----------------------------- PORT RESOLUTION -----------------------------
  // ---------------------------------------------------------------------------
  resolve = hrd_resolve_port_index(port_index);
  assert(resolve.ib_ctx != nullptr && resolve.dev_port_id >= 1);
  // ---------------------------------------------------------------------------
  pd = ibv_alloc_pd(resolve.ib_ctx);
  assert(pd != nullptr);

  // ---------------------------------------------------------------------------
  // ------------------------------- CREATE QPs --------------------------------
  // ---------------------------------------------------------------------------
  create_conn_qps();
  // -------------------------------------------------------------------------
  if (conn_config.prealloc_buf != nullptr) {
    rt_assert(false, "We don't support providing the allocated memory");
  }

  size_t reg_size = conn_config.buf_size;

  if (numa_node != kHrdInvalidNUMANode) {
    rt_assert(false,
              "We don't support neither hugepages, nor numa aware memory "
              "allocation");
  }

  // use one replay buffer for all replay QPs
  auto *replay_buf =
      reinterpret_cast<volatile uint8_t *>(memalign(4096, reg_size));

  for (int i = 0; i < 2 * conn_config.num_qps; i++) {
    conn_buf[i] =
        i % 2 == 0
            ? reinterpret_cast<volatile uint8_t *>(memalign(4096, reg_size))
            : replay_buf;

    assert(conn_buf[i] != nullptr);
  }

  // ------------------------------ Register MR ------------------------------
  for (int i = 0; i < 2 * conn_config.num_qps; i++) {
    memset(const_cast<uint8_t *>(conn_buf[i]), 0, reg_size);

    int ib_flags;
    // Even ids are used for broadcast-p-q
    if (i % 2 == 0) {
      // TODO-Q(Kristian): Why local write?
      ib_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE;
    } else {
      ib_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ;
    }

    conn_buf_mr[i] =
        ibv_reg_mr(pd, const_cast<uint8_t *>(conn_buf[i]), reg_size, ib_flags);

    if (conn_buf_mr[i] == nullptr) {
      printf("Buffer reg %d failed with code %s\n", i, strerror(errno));
      exit(-1);
    }
  }
}

ControlBlock::~ControlBlock() {
  hrd_red_printf("HRD: Destroying control block %d\n", lgid);

  // Destroy QPs and CQs. QPs must be destroyed before CQs.
  for (size_t i = 0; i < 2 * conn_config.num_qps; i++) {
    rt_assert(ibv_destroy_qp(conn_qp[i]) == 0, "Failed to destroy dgram QP");

    rt_assert(ibv_destroy_cq(conn_cq[i]) == 0,
              "Failed to destroy connected CQ");
  }

  // Destroy memory regions
  if (conn_config.num_qps > 0) {
    for (int i = 0; i < 2 * conn_config.num_qps; i++) {
      assert(conn_buf_mr[i] != nullptr);

      if (ibv_dereg_mr(conn_buf_mr[i])) {
        fprintf(stderr, "HRD: Couldn't deregister conn MR for cb %zu\n", lgid);
        // TODO(Kristian): handle me
        return;
      }

      // odd QPs share the same replay-buffer
      if (i % 2 == 0 || i == 1) {
        free(const_cast<uint8_t *>(conn_buf[i]));
      }
    }

    free(const_cast<ibv_mr **>(conn_buf_mr));
    free(const_cast<uint8_t **>(conn_buf));
    free(const_cast<ibv_qp **>(conn_qp));
    free(const_cast<ibv_cq **>(conn_cq));
  }

  // Destroy protection domain
  rt_assert(ibv_dealloc_pd(pd) == 0, "Failed to dealloc PD");

  // Destroy device context
  rt_assert(ibv_close_device(resolve.ib_ctx) == 0, "Failed to close device");

  // close memcached connection
  hrd_close_memcached();

  printf("HRD: Control block %zu destroyed.\n", lgid);

  return;
}

void ControlBlock::publish_conn_qp(size_t idx, const char *qp_name) {
  assert(idx < 2 * conn_config.num_qps);
  assert(strlen(qp_name) < kHrdQPNameSize - 1);
  assert(strstr(qp_name, kHrdReservedNamePrefix) == nullptr);

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

  hrd_publish(qp_attr.name, &qp_attr, sizeof(hrd_qp_attr_t));
}

void ControlBlock::connect_remote_qp(size_t idx,
                                     hrd_qp_attr_t *remote_qp_attr) {
  assert(idx < 2 * conn_config.num_qps);
  assert(conn_qp[idx] != nullptr);
  assert(resolve.dev_port_id >= 1);

  struct ibv_qp_attr conn_attr;
  memset(&conn_attr, 0, sizeof(struct ibv_qp_attr));
  conn_attr.qp_state = IBV_QPS_RTR;
  conn_attr.path_mtu = IBV_MTU_4096;
  conn_attr.dest_qp_num = remote_qp_attr->qpn;
  conn_attr.rq_psn = kHrdDefaultPSN;

  conn_attr.ah_attr.is_global = kRoCE ? 1 : 0;
  conn_attr.ah_attr.dlid = kRoCE ? 0 : remote_qp_attr->lid;
  conn_attr.ah_attr.sl = 0;
  conn_attr.ah_attr.src_path_bits = 0;
  conn_attr.ah_attr.port_num = resolve.dev_port_id;  // Local port!

  if (kRoCE) {
    auto &grh = conn_attr.ah_attr.grh;
    grh.dgid.global.interface_id = remote_qp_attr->gid.global.interface_id;
    grh.dgid.global.subnet_prefix = remote_qp_attr->gid.global.subnet_prefix;

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
    fprintf(stderr, "Failed to modify QP to RTR\n");
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
    fprintf(stderr, "HRD: Failed to modify QP to RTS\n");
    assert(false);
  }
}

void ControlBlock::create_conn_qps() {
  assert(pd != nullptr && resolve.ib_ctx != nullptr);
  assert(conn_config.num_qps >= 1 && resolve.dev_port_id >= 1);

  for (size_t i = 0; i < 2 * conn_config.num_qps; i++) {
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