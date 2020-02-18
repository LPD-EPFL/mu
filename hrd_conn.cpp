#include "hrd.hpp"

// If @prealloc_conn_buf != nullptr, @conn_buf_size is the size of the
// preallocated buffer. If @prealloc_conn_buf == nullptr, @conn_buf_size is the
// size of the new buffer to create.
struct hrd_ctrl_blk_t *hrd_ctrl_blk_init(size_t local_hid, size_t port_index,
                                         size_t numa_node,
                                         hrd_conn_config_t *conn_config) {
  if (kHrdMlx5Atomics) {
    rt_assert(!kRoCE, "mlx5 atomics not supported with RoCE");
    hrd_red_printf(
        "HRD: Connect-IB atomics enabled. This QP setup has not "
        "been tested for non-atomics performance.\n");
    sleep(1);
  }

  hrd_red_printf("HRD: creating control block %zu: port %zu, socket %zu.\n",
                 local_hid, port_index, numa_node);

  if (conn_config != nullptr) {
    hrd_red_printf("HRD: control block %zu: Conn config = %s\n", local_hid,
                   conn_config->to_string().c_str());
  }

  // @local_hid can be anything. It's used for just printing.
  assert(port_index <= 16);
  assert(numa_node <= kHrdInvalidNUMANode);

  auto *cb = new hrd_ctrl_blk_t();
  memset(cb, 0, sizeof(hrd_ctrl_blk_t));

  // Fill in the control block
  cb->local_hid = local_hid;
  cb->port_index = port_index;
  cb->numa_node = numa_node;

  // Connected QPs
  if (conn_config != nullptr) {
    if (conn_config->prealloc_buf != nullptr) {
      assert(conn_config->buf_shm_key == -1);
    }

    cb->conn_config = *conn_config;
  }

  // Resolve the port into cb->resolve
  hrd_resolve_port_index(cb, port_index);

  cb->pd = ibv_alloc_pd(cb->resolve.ib_ctx);
  assert(cb->pd != nullptr);

  // int ib_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ |
  //                IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC;

  // Create connected QPs and transition them to RTS.
  // Create and register connected QP RDMA buffer.
  if (cb->conn_config.num_qps >= 1) {
    cb->conn_qp = new ibv_qp *[2 * cb->conn_config.num_qps];
    cb->conn_cq = new ibv_cq *[2 * cb->conn_config.num_qps];
    cb->conn_buf = (volatile uint8_t **)calloc(2 * cb->conn_config.num_qps,
                                               sizeof(uint8_t *));
    cb->conn_buf_mr =
        (ibv_mr **)calloc(2 * cb->conn_config.num_qps, sizeof(ibv_mr *));

    hrd_create_conn_qps(cb);

    if (conn_config->prealloc_buf == nullptr) {
      // Create and register conn_buf - always make it multiple of 2 MB
      size_t reg_size = 0;

      // If numa_node is invalid, use standard heap
      if (numa_node != kHrdInvalidNUMANode) {
        assert(false &&
               "We don't support neither hugepages, nor numa aware memory "
               "allocation");
        // // Hugepages
        // while (reg_size < cb->conn_config.buf_size) reg_size += MB(2);

        // assert(cb->conn_config.buf_shm_key >= 1);  // SHM key 0 is used by OS
        // cb->conn_buf = reinterpret_cast<volatile uint8_t*>(hrd_malloc_socket(
        //     cb->conn_config.buf_shm_key, reg_size, numa_node));
      } else {
        reg_size = cb->conn_config.buf_size;

        // use one replay buffer for all replay QPs
        auto *replay_buf =
            reinterpret_cast<volatile uint8_t *>(memalign(4096, reg_size));

        for (int i = 0; i < 2 * cb->conn_config.num_qps; i++) {
          cb->conn_buf[i] = i % 2 == 0 ? reinterpret_cast<volatile uint8_t *>(
                                             memalign(4096, reg_size))
                                       : replay_buf;

          assert(cb->conn_buf[i] != nullptr);
        }
      }

      for (int i = 0; i < 2 * cb->conn_config.num_qps; i++) {
        memset(const_cast<uint8_t *>(cb->conn_buf[i]), 0, reg_size);

        int ib_flags;
        if (i % 2 == 0) {  // Even ids are used for broadcast-p-q
          // TODO-Q(Kristian): Why local write?
          ib_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE;
        } else {
          ib_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ;
        }

        cb->conn_buf_mr[i] = ibv_reg_mr(
            cb->pd, const_cast<uint8_t *>(cb->conn_buf[i]), reg_size, ib_flags);

        if (cb->conn_buf_mr[i] == nullptr) {
          printf("Buffer reg %d failed with code %s\n", i, strerror(errno));
          exit(-1);
        }
      }
    } else {
      assert(false && "We don't support providing the allocated memory");
      // cb->conn_buf = const_cast<volatile
      // uint8_t*>(conn_config->prealloc_buf); cb->conn_buf_mr =
      // ibv_reg_mr(cb->pd, const_cast<uint8_t*>(cb->conn_buf),
      //                              cb->conn_config.buf_size, ib_flags);
      // assert(cb->conn_buf_mr != nullptr);
    }
  }

  return cb;
}

// Free up the resources taken by @cb. Return -1 if something fails, else 0.
int hrd_ctrl_blk_destroy(hrd_ctrl_blk_t *cb) {
  hrd_red_printf("HRD: Destroying control block %d\n", cb->local_hid);

  // Destroy QPs and CQs. QPs must be destroyed before CQs.
  for (size_t i = 0; i < 2 * cb->conn_config.num_qps; i++) {
    rt_assert(ibv_destroy_qp(cb->conn_qp[i]) == 0,
              "Failed to destroy dgram QP");

    rt_assert(ibv_destroy_cq(cb->conn_cq[i]) == 0,
              "Failed to destroy connected CQ");
  }

  // Destroy memory regions
  if (cb->conn_config.num_qps > 0) {
    for (int i = 0; i < 2 * cb->conn_config.num_qps; i++) {
      assert(cb->conn_buf_mr[i] != nullptr);
      if (ibv_dereg_mr(cb->conn_buf_mr[i])) {
        fprintf(stderr, "HRD: Couldn't deregister conn MR for cb %zu\n",
                cb->local_hid);
        return -1;
      }
      // odd QPs share the same replay-buffer
      if (i % 2 == 0 || i == 1) {
        free(const_cast<uint8_t *>(cb->conn_buf[i]));
      }
    }

    free(const_cast<ibv_mr **>(cb->conn_buf_mr));
    free(const_cast<uint8_t **>(cb->conn_buf));
  }

  // Destroy protection domain
  rt_assert(ibv_dealloc_pd(cb->pd) == 0, "Failed to dealloc PD");

  // Destroy device context
  rt_assert(ibv_close_device(cb->resolve.ib_ctx) == 0,
            "Failed to close device");

  hrd_red_printf("HRD: Control block %d destroyed.\n", cb->local_hid);
  return 0;
}

// Create connected QPs and transition them to INIT
void hrd_create_conn_qps(hrd_ctrl_blk_t *cb) {
  assert(cb->pd != nullptr && cb->resolve.ib_ctx != nullptr);
  assert(cb->conn_config.num_qps >= 1 && cb->resolve.dev_port_id >= 1);

  for (size_t i = 0; i < 2 * cb->conn_config.num_qps; i++) {
    cb->conn_cq[i] = ibv_create_cq(cb->resolve.ib_ctx, cb->conn_config.sq_depth,
                                   nullptr, nullptr, 0);
    // We sometimes set Mellanox env variables for hugepage-backed queues.
    rt_assert(cb->conn_cq[i] != nullptr,
              "Failed to create conn CQ. Check hugepages and SHM limits?");

#if (kHrdMlx5Atomics == false)
    struct ibv_qp_init_attr create_attr;
    memset(&create_attr, 0, sizeof(struct ibv_qp_init_attr));
    create_attr.send_cq = cb->conn_cq[i];
    create_attr.recv_cq = cb->conn_cq[i];
    create_attr.qp_type = cb->conn_config.use_uc ? IBV_QPT_UC : IBV_QPT_RC;

    create_attr.cap.max_send_wr = cb->conn_config.sq_depth;
    create_attr.cap.max_recv_wr = 1;  // We don't do RECVs on conn QPs
    create_attr.cap.max_send_sge = 1;
    create_attr.cap.max_recv_sge = 1;
    create_attr.cap.max_inline_data = kHrdMaxInline;

    cb->conn_qp[i] = ibv_create_qp(cb->pd, &create_attr);
    rt_assert(cb->conn_qp[i] != nullptr, "Failed to create conn QP");

    struct ibv_qp_attr init_attr;
    memset(&init_attr, 0, sizeof(struct ibv_qp_attr));
    init_attr.qp_state = IBV_QPS_INIT;
    init_attr.pkey_index = 0;
    init_attr.port_num = cb->resolve.dev_port_id;
    init_attr.qp_access_flags = cb->conn_config.use_uc
                                    ? IBV_ACCESS_REMOTE_WRITE
                                    : IBV_ACCESS_REMOTE_WRITE |
                                          IBV_ACCESS_REMOTE_READ |
                                          IBV_ACCESS_REMOTE_ATOMIC;

    if (ibv_modify_qp(cb->conn_qp[i], &init_attr,
                      IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT |
                          IBV_QP_ACCESS_FLAGS)) {
      fprintf(stderr, "Failed to modify conn QP to INIT\n");
      exit(-1);
    }
#else
    assert(cb->use_uc == 0);  // This is for atomics; no atomics on UC
    struct ibv_exp_qp_init_attr create_attr;
    memset(&create_attr, 0, sizeof(struct ibv_exp_qp_init_attr));

    create_attr.pd = cb->pd;
    create_attr.send_cq = cb->conn_cq[i];
    create_attr.recv_cq = cb->conn_cq[i];
    create_attr.cap.max_send_wr = cb->conn_config.sq_depth;
    create_attr.cap.max_recv_wr = 1;  // We don't do RECVs on conn QPs
    create_attr.cap.max_send_sge = 1;
    create_attr.cap.max_recv_sge = 1;
    create_attr.cap.max_inline_data = kHrdMaxInline;
    create_attr.max_atomic_arg = 8;
    create_attr.exp_create_flags = IBV_EXP_QP_CREATE_ATOMIC_BE_REPLY;
    create_attr.comp_mask = IBV_EXP_QP_INIT_ATTR_CREATE_FLAGS |
                            IBV_EXP_QP_INIT_ATTR_PD |
                            IBV_EXP_QP_INIT_ATTR_ATOMICS_ARG;
    create_attr.qp_type = IBV_QPT_RC;

    cb->conn_qp[i] = ibv_exp_create_qp(cb->resolve.ib_ctx, &create_attr);
    assert(cb->conn_qp[i] != nullptr);

    struct ibv_exp_qp_attr init_attr;
    memset(&init_attr, 0, sizeof(struct ibv_exp_qp_attr));
    init_attr.qp_state = IBV_QPS_INIT;
    init_attr.pkey_index = 0;
    init_attr.port_num = cb->resolve.dev_port_id;
    init_attr.qp_access_flags = cb->use_uc == 1 ? IBV_ACCESS_REMOTE_WRITE
                                                : IBV_ACCESS_REMOTE_WRITE |
                                                      IBV_ACCESS_REMOTE_READ |
                                                      IBV_ACCESS_REMOTE_ATOMIC;

    if (ibv_exp_modify_qp(cb->conn_qp[i], &init_attr,
                          IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT |
                              IBV_QP_ACCESS_FLAGS)) {
      fprintf(stderr, "Failed to modify conn QP to INIT\n");
      exit(-1);
    }
  }
#endif
  }
}

// Connects @cb's queue pair index @n to remote QP @remote_qp_attr
void hrd_connect_qp(hrd_ctrl_blk_t *cb, size_t n,
                    hrd_qp_attr_t *remote_qp_attr) {
  assert(n < 2 * cb->conn_config.num_qps);
  assert(cb->conn_qp[n] != nullptr);
  assert(cb->resolve.dev_port_id >= 1);

#if (kHrdMlx5Atomics == false)
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
  conn_attr.ah_attr.port_num = cb->resolve.dev_port_id;  // Local port!

  if (kRoCE) {
    auto &grh = conn_attr.ah_attr.grh;
    grh.dgid.global.interface_id = remote_qp_attr->gid.global.interface_id;
    grh.dgid.global.subnet_prefix = remote_qp_attr->gid.global.subnet_prefix;

    grh.sgid_index = 0;
    grh.hop_limit = 1;
  }

  int rtr_flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
                  IBV_QP_RQ_PSN;

  if (!cb->conn_config.use_uc) {
    conn_attr.max_dest_rd_atomic = cb->conn_config.max_rd_atomic;
    conn_attr.min_rnr_timer = 12;
    rtr_flags |= IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;
  }

  if (ibv_modify_qp(cb->conn_qp[n], &conn_attr, rtr_flags)) {
    fprintf(stderr, "Failed to modify QP to RTR\n");
    assert(false);
  }

  memset(&conn_attr, 0, sizeof(conn_attr));
  conn_attr.qp_state = IBV_QPS_RTS;
  conn_attr.sq_psn = kHrdDefaultPSN;

  int rts_flags = IBV_QP_STATE | IBV_QP_SQ_PSN;

  if (!cb->conn_config.use_uc) {
    conn_attr.timeout = 14;
    conn_attr.retry_cnt = 7;
    conn_attr.rnr_retry = 7;
    conn_attr.max_rd_atomic = cb->conn_config.max_rd_atomic;
    conn_attr.max_dest_rd_atomic = cb->conn_config.max_rd_atomic;
    rts_flags |= IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY |
                 IBV_QP_MAX_QP_RD_ATOMIC;
  }

  if (ibv_modify_qp(cb->conn_qp[n], &conn_attr, rts_flags)) {
    fprintf(stderr, "HRD: Failed to modify QP to RTS\n");
    assert(false);
  }
#else
  struct ibv_exp_qp_attr conn_attr;
  memset(&conn_attr, 0, sizeof(struct ibv_exp_qp_attr));
  conn_attr.qp_state = IBV_QPS_RTR;
  conn_attr.path_mtu = IBV_MTU_4096;
  conn_attr.dest_qp_num = remote_qp_attr->qpn;
  conn_attr.rq_psn = kHrdDefaultPSN;

  conn_attr.ah_attr.is_global = 0;
  conn_attr.ah_attr.dlid = remote_qp_attr->lid;
  conn_attr.ah_attr.sl = 0;
  conn_attr.ah_attr.src_path_bits = 0;
  conn_attr.ah_attr.port_num = cb->resolve.dev_port_id;  // Local port!

  uint64_t rtr_flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU |
                       IBV_QP_DEST_QPN | IBV_QP_RQ_PSN;

  if (!cb->use_uc) {
    conn_attr.max_dest_rd_atomic = 16;
    conn_attr.min_rnr_timer = 12;
    rtr_flags |= IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;
  }

  if (ibv_exp_modify_qp(cb->conn_qp[n], &conn_attr, rtr_flags)) {
    fprintf(stderr, "HRD: Failed to modify QP to RTR\n");
    assert(false);
  }

  memset(&conn_attr, 0, sizeof(conn_attr));
  conn_attr.qp_state = IBV_QPS_RTS;
  conn_attr.sq_psn = kHrdDefaultPSN;

  uint64_t rts_flags = IBV_QP_STATE | IBV_QP_SQ_PSN;

  if (!cb->use_uc) {
    conn_attr.timeout = 14;
    conn_attr.retry_cnt = 7;
    conn_attr.rnr_retry = 7;
    conn_attr.max_rd_atomic = 16;
    conn_attr.max_dest_rd_atomic = 16;
    rts_flags |= IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY |
                 IBV_QP_MAX_QP_RD_ATOMIC;
  }

  if (ibv_exp_modify_qp(cb->conn_qp[n], &conn_attr, rts_flags)) {
    fprintf(stderr, "HRD: Failed to modify QP to RTS\n");
    assert(false);
  }
#endif

  return;
}

void hrd_publish_conn_qp(hrd_ctrl_blk_t *cb, size_t n, const char *qp_name) {
  assert(n < 2 * cb->conn_config.num_qps);
  assert(strlen(qp_name) < kHrdQPNameSize - 1);
  assert(strstr(qp_name, kHrdReservedNamePrefix) == nullptr);

  size_t len = strlen(qp_name);
  for (size_t i = 0; i < len; i++) assert(qp_name[i] != ' ');

  hrd_qp_attr_t qp_attr;
  memset(&qp_attr, 0, sizeof(hrd_qp_attr_t));

  strcpy(qp_attr.name, qp_name);
  qp_attr.lid = cb->resolve.port_lid;
  qp_attr.qpn = cb->conn_qp[n]->qp_num;
  if (kRoCE) qp_attr.gid = cb->resolve.gid;

  qp_attr.buf_addr = reinterpret_cast<uint64_t>(cb->conn_buf[n]);
  qp_attr.buf_size = cb->conn_config.buf_size;
  qp_attr.rkey = cb->conn_buf_mr[n]->rkey;

  hrd_publish(qp_attr.name, &qp_attr, sizeof(hrd_qp_attr_t));
}

hrd_qp_attr_t *hrd_get_published_qp(const char *qp_name) {
  assert(strlen(qp_name) < kHrdQPNameSize - 1);
  assert(strstr(qp_name, kHrdReservedNamePrefix) == nullptr);

  hrd_qp_attr_t *ret;
  for (size_t i = 0; i < strlen(qp_name); i++) assert(qp_name[i] != ' ');

  int ret_len = hrd_get_published(qp_name, reinterpret_cast<void **>(&ret));

  // The registry lookup returns only if we get a unique QP for @qp_name, or
  // if the memcached lookup succeeds but we don't have an entry for @qp_name.
  rt_assert(ret_len == static_cast<int>(sizeof(*ret)) || ret_len == -1, "");
  return ret;
}
