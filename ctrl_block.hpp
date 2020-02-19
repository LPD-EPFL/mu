#pragma once

#include <infiniband/verbs.h>

#include "hrd.hpp"

class ControlBlock {
 public:
  /**
   * TODO(Kristian): doc
   * @param local_hid:
   * @param port_index:
   * @param numa_node:
   * @param conn_config:
   */
  ControlBlock(size_t local_hid, size_t port_index, size_t numa_node,
               ConnectionConfig conn_config);
  ~ControlBlock();

  size_t lgid;
  size_t port_index;
  size_t numa_node;
  IBResolve resolve;

  ConnectionConfig conn_config;
  // TODO(Kristian): get rid of this
  // For now needed to access the rkey and raddress for remote operations
  hrd_qp_attr_t **r_qps;
  // Connection Buffers
  volatile uint8_t **conn_buf;
  // Protection Domain
  struct ibv_pd *pd;
  // RConnected Queue Pairs
  struct ibv_qp **conn_qp;
  // Completion Queues
  struct ibv_cq **conn_cq;
  // Memory Regions
  struct ibv_mr **conn_buf_mr;

  void publish_conn_qp(size_t idx, const char *qp_name);
  // blocks and polls until it finds the matching remote qp entry in memcached
  // and connects the conn_qp[idx] to it
  void connect_remote_qp(size_t idx, const char *qp_name);

 private:
  void create_conn_qps();
};