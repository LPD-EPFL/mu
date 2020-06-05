#pragma once

#include <map>
#include <vector>

#include <dory/extern/ibverbs.hpp>

namespace dory {
class OpenDevice {
 public:
  OpenDevice();
  OpenDevice(struct ibv_device *device);

  ~OpenDevice();

  // Copy constructor
  OpenDevice(OpenDevice const &o);

  // Move constructor
  OpenDevice(OpenDevice &&o);

  // Copy assignment operator
  OpenDevice &operator=(OpenDevice const &o);

  // Move assignment operator
  OpenDevice &operator=(OpenDevice &&o);

  struct ibv_context *context() {
    return ctx;
  }

  char *name() const { return dev->name; }

  char *dev_name() const { return dev->dev_name; }

  size_t guid() const { return static_cast<size_t>(ibv_get_device_guid(dev)); }

  enum NodeType : int8_t { UNKNOWN_NODE = -1, CA = 1, RNIC = 4 };

  static const char *type_str(NodeType t) {
    const std::map<NodeType, const char *> MyEnumStrings{
        {NodeType::UNKNOWN_NODE, "NodeType::UNKNOWN"},
        {NodeType::CA, "NodeType::CA"},
        {NodeType::RNIC, "NodeType::RNIC"}};
    auto it = MyEnumStrings.find(t);
    return it == MyEnumStrings.end() ? "Out of range" : it->second;
  }

  NodeType node_type() const { return static_cast<NodeType>(dev->node_type); }

  enum TransportType : int8_t { UNKNOWN_TRANSPORT = -1, IB = 0, IWARP = 1 };

  static const char *type_str(TransportType t) {
    const std::map<TransportType, const char *> MyEnumStrings{
        {TransportType::UNKNOWN_TRANSPORT, "TransportType::UNKNOWN"},
        {TransportType::IB, "TransportType::IB"},
        {TransportType::IWARP, "TransportType::IWARP"}};
    auto it = MyEnumStrings.find(t);
    return it == MyEnumStrings.end() ? "Out of range" : it->second;
  }

  TransportType transport_type() const {
    return static_cast<TransportType>(dev->transport_type);
  }

  struct ibv_device_attr const &device_attributes() const;

  // printf("IB device %d:\n", dev_i);
  // printf("    Name: %s\n", dev_list[dev_i]->name);
  // printf("    Device name: %s\n", dev_list[dev_i]->dev_name);
  // printf("    GUID: %zx\n",
  //        static_cast<size_t>(ibv_get_device_guid(dev_list[dev_i])));
  // printf("    Node type: %d (-1: UNKNOWN, 1: CA, 4: RNIC)\n",
  //        dev_list[dev_i]->node_type);
  // printf("    Transport type: %d (-1: UNKNOWN, 0: IB, 1: IWARP)\n",
  //        dev_list[dev_i]->transport_type);

  // printf("    fw: %s\n", device_attr.fw_ver);
  // printf("    max_qp: %d\n", device_attr.max_qp);
  // printf("    max_cq: %d\n", device_attr.max_cq);
  // printf("    max_mr: %d\n", device_attr.max_mr);
  // printf("    max_pd: %d\n", device_attr.max_pd);
  // printf("    max_ah: %d\n", device_attr.max_ah);
  // printf("    phys_port_cnt: %u\n", device_attr.phys_port_cnt);

 private:
  struct ibv_device *dev = nullptr;
  struct ibv_context *ctx = nullptr;
  struct ibv_device_attr device_attr;
};
}  // namespace dory

namespace dory {
class Devices {
 public:
  Devices();
  ~Devices();

  std::vector<OpenDevice> &list(bool force = false);

 private:
  struct ibv_device **dev_list;
  std::vector<OpenDevice> devices;
};
}  // namespace dory

namespace dory {
class ResolvedPort {
 public:
  ResolvedPort(OpenDevice &od);

  /**
   * @param index: 0-based
   **/
  bool bindTo(size_t index);

  /**
   * @returns 1-based port id
   **/
  uint8_t portID() const { return port_id; }

  uint16_t portLID() const { return port_lid; }

  OpenDevice &device() { return open_dev; }

 private:
  static std::string link_layer_str(uint8_t link_layer) {
    switch (link_layer) {
      case IBV_LINK_LAYER_UNSPECIFIED:
        return "[Unspecified]";
      case IBV_LINK_LAYER_INFINIBAND:
        return "[InfiniBand]";
      case IBV_LINK_LAYER_ETHERNET:
        return "[Ethernet]";
      default:
        return "[Invalid]";
    }
  }

 private:
  OpenDevice &open_dev;
  int port_index;
  uint8_t port_id;
  uint16_t port_lid;
};
}  // namespace dory
