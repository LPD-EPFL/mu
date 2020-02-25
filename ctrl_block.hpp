#pragma once

#include <infiniband/verbs.h>
#include <malloc.h>
#include <unistd.h>
#include <cassert>
#include <memory>
#include "conn_config.hpp"
#include "store_conn.hpp"

class ControlBlock {
 public:
  class IBResolve {
   public:
    // Device index in list of verbs devices
    int device_id;
    // TODO: use smart pointer
    // The verbs device context
    struct ibv_context *ib_ctx;
    // 1-based port ID in device. 0 is invalid.
    uint8_t dev_port_id;
    // LID of phy_port. 0 is invalid.
    uint16_t port_lid;
    // GID, used only for RoCE
    union ibv_gid gid;
  };
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

  std::tuple<volatile uint8_t*, volatile uint8_t*> get_replay_buf();

  volatile uint8_t *get_buf(size_t idx);

  MemoryStore::QPAttr *get_r_qp(size_t idx);

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
  ControlBlock::IBResolve resolve;

  // Protection Domain
  // Not wrapped by a smart pointer as the destruction shoud happen after all
  // ressources are released.
  ibv_pd *pd;

  // For now needed to access the rkey and raddress for remote operations
  std::unique_ptr<MemoryStore::QPAttr *[]> r_qps;

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

  ControlBlock::IBResolve resolve_port_index(size_t phy_port);
};