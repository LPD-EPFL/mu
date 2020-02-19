#pragma once

#include <infiniband/verbs.h>

#include "hrd.hpp"

class ControlBlock {
 public:
  ControlBlock(size_t local_hid, size_t port_index, size_t numa_node,
               ConnectionConfig conn_config);
  ~ControlBlock();
  size_t lgid;
  size_t port_index;
  size_t numa_node;
  IBResolve resolve;
  struct ibv_pd *pd;

  ConnectionConfig conn_config;
  struct ibv_qp **conn_qp;
  struct ibv_cq **conn_cq;
  volatile uint8_t **conn_buf;
  struct ibv_mr **conn_buf_mr;

  hrd_qp_attr_t **r_qps;

  void create_conn_qps();
  void connect_remote_qp(size_t idx, hrd_qp_attr_t *remote_qp_attr);
  void publish_conn_qp(size_t idx, const char *qp_name);
};