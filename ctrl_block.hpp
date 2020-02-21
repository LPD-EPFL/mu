#pragma once

#include <memory>
#include <cassert>
#include <malloc.h>
#include <unistd.h>
#include <infiniband/verbs.h>
#include "conn_config.hpp"
#include "store_conn.hpp"

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

  ibv_qp *get_qp(size_t idx);

  ibv_cq *get_cq(size_t idx);

  ibv_mr *get_mr(size_t idx);

  volatile uint8_t *get_buf(size_t idx);

  hrd_qp_attr_t *get_r_qp(size_t idx);

  void publish_conn_qp(size_t idx, const char *qp_name);

  // blocks and polls until it finds the matching remote qp entry in memcached
  // and connects the conn_qp[idx] to it
  void connect_remote_qp(size_t idx, const char *qp_name);

 private:
  size_t lgid;

  size_t port_index;

  size_t numa_node;

  ConnectionConfig conn_config;

  // InfiniBand info resolved from `phy_port`
  IBResolve resolve;

  // Protection Domain
  // Not wrapped by a smart pointer as the destruction shoud happen after all
  // ressources are released.
  ibv_pd *pd;

  // For now needed to access the rkey and raddress for remote operations
  std::unique_ptr<hrd_qp_attr_t *[]> r_qps;

  // Connection Buffers
  std::unique_ptr<volatile uint8_t *[]> conn_buf;

  // Protection Domain
  // RConnected Queue Pairs
  std::unique_ptr<ibv_qp *[]> conn_qp;

  // Completion Queues
  std::unique_ptr<ibv_cq *[]> conn_cq;
  // Memory Regions

  std::unique_ptr<ibv_mr *[]> conn_buf_mr;

  void create_conn_qps();

  IBResolve resolve_port_index(size_t phy_port);
};