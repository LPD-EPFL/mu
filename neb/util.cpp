#include "util.hpp"

std::string link_layer_str(uint8_t link_layer) {
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

// Print information about all IB devices in the system
void hrd_ibv_devinfo(void) {
  int num_devices = 0, dev_i;
  struct ibv_device **dev_list;
  struct ibv_context *ctx;
  struct ibv_device_attr device_attr;

  printf("HRD: printing IB dev info\n");

  dev_list = ibv_get_device_list(&num_devices);
  assert(dev_list != nullptr);

  for (dev_i = 0; dev_i < num_devices; dev_i++) {
    ctx = ibv_open_device(dev_list[dev_i]);
    assert(ctx != nullptr);

    memset(&device_attr, 0, sizeof(device_attr));
    if (ibv_query_device(ctx, &device_attr)) {
      printf("Could not query device: %d\n", dev_i);
      assert(false);
    }

    printf("IB device %d:\n", dev_i);
    printf("    Name: %s\n", dev_list[dev_i]->name);
    printf("    Device name: %s\n", dev_list[dev_i]->dev_name);
    printf("    GUID: %zx\n",
           static_cast<size_t>(ibv_get_device_guid(dev_list[dev_i])));
    printf("    Node type: %d (-1: UNKNOWN, 1: CA, 4: RNIC)\n",
           dev_list[dev_i]->node_type);
    printf("    Transport type: %d (-1: UNKNOWN, 0: IB, 1: IWARP)\n",
           dev_list[dev_i]->transport_type);

    printf("    fw: %s\n", device_attr.fw_ver);
    printf("    max_qp: %d\n", device_attr.max_qp);
    printf("    max_cq: %d\n", device_attr.max_cq);
    printf("    max_mr: %d\n", device_attr.max_mr);
    printf("    max_pd: %d\n", device_attr.max_pd);
    printf("    max_ah: %d\n", device_attr.max_ah);
    printf("    phys_port_cnt: %u\n", device_attr.phys_port_cnt);
  }

  ibv_free_device_list(dev_list);
}

// Return the environment variable @name if it is set. Exit if not.
char *get_env(const char *name) {
  char *env = getenv(name);
  if (env == nullptr) {
    fprintf(stderr, "Environment variable %s not set\n", name);
    assert(false);
  }

  return env;
}
