#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

#include "device.hpp"

// OpenDevice definitions
namespace dory {
OpenDevice::OpenDevice() {}

OpenDevice::OpenDevice(struct ibv_device *device)
    : dev{device}, device_attr{ibv_device_attr()} {
  ctx = ibv_open_device(device);
  if (ctx == nullptr) {
    throw std::runtime_error("Could not get device list: " +
                             std::string(std::strerror(errno)));
  }

  memset(&device_attr, 0, sizeof(device_attr));
  if (ibv_query_device(ctx, &device_attr) != 0) {
    throw std::runtime_error("Could not query device: " +
                             std::string(std::strerror(errno)));
  }
}

OpenDevice::~OpenDevice() {
  if (ctx != nullptr) {
    ibv_close_device(ctx);
  }
}

// Copy constructor
OpenDevice::OpenDevice(OpenDevice const &o) : dev{o.dev} {
  ctx = ibv_open_device(dev);
  if (ctx == nullptr) {
    throw std::runtime_error("Could not get device list: " +
                             std::string(std::strerror(errno)));
  }

  memset(&device_attr, 0, sizeof(device_attr));
  if (ibv_query_device(ctx, &device_attr) != 0) {
    throw std::runtime_error("Could not query device: " +
                             std::string(std::strerror(errno)));
  }
}

// Move constructor
OpenDevice::OpenDevice(OpenDevice &&o)
    : dev{o.dev}, ctx{o.ctx}, device_attr(o.device_attr) {
  o.ctx = nullptr;
}

// Copy assignment operator
OpenDevice &OpenDevice::operator=(OpenDevice const &o) {
  if (&o == this) {
    return *this;
  }

  ctx = ibv_open_device(o.dev);
  if (ctx == nullptr) {
    throw std::runtime_error("Could not get device list: " +
                             std::string(std::strerror(errno)));
  }

  memset(&device_attr, 0, sizeof(device_attr));
  if (ibv_query_device(ctx, &device_attr) != 0) {
    throw std::runtime_error("Could not query device: " +
                             std::string(std::strerror(errno)));
  }

  return *this;
}

// Move assignment operator
OpenDevice &OpenDevice::operator=(OpenDevice &&o) {
  if (&o == this) {
    return *this;
  }

  dev = o.dev;
  ctx = o.ctx;
  device_attr = o.device_attr;
  o.ctx = nullptr;

  return *this;
}

struct ibv_device_attr const &OpenDevice::device_attributes() const {
  return device_attr;
}
} // namespace dory

// Device definitions
namespace dory {
Devices::Devices() : dev_list{nullptr} {}

Devices::~Devices() {
  if (dev_list != nullptr) {
    ibv_free_device_list(dev_list);
  }
}

std::vector<OpenDevice> &Devices::list(bool force) {
  if (force || dev_list == nullptr) {
    int num_devices = 0;
    dev_list = ibv_get_device_list(&num_devices);

    if (dev_list == nullptr) {
      throw std::runtime_error("Error getting device list: " +
                               std::string(std::strerror(errno)));
    }

    for (int i = 0; i < num_devices; i++) {
      devices.push_back(std::move(OpenDevice(dev_list[i])));
    }
  }

  return devices;
}
} // namespace dory

namespace dory {
ResolvedPort::ResolvedPort(OpenDevice &od) : open_dev{od}, port_index{-1} {}

bool ResolvedPort::bindTo(size_t index) {
  size_t skipped_active_ports = 0;
  for (unsigned i = 1; i <= open_dev.device_attributes().phys_port_cnt; i++) {
    struct ibv_port_attr port_attr;
    memset(&port_attr, 0, sizeof(ibv_port_attr));

    if (ibv_query_port(open_dev.context(), i, &port_attr) != 0) {
      throw std::runtime_error("Failed to query port: " +
                               std::string(std::strerror(errno)));
    }

    if (port_attr.phys_state != IBV_PORT_ACTIVE &&
        port_attr.phys_state != IBV_PORT_ACTIVE_DEFER) {
      continue;
    }

    if (skipped_active_ports == index) {
      if (port_attr.link_layer != IBV_LINK_LAYER_INFINIBAND) {
        throw std::runtime_error(
            "Transport type required is InfiniBand but port link layer is " +
            link_layer_str(port_attr.link_layer));
      }

      port_id = i;
      port_lid = port_attr.lid;

      return true;
    }

    skipped_active_ports += 1;
  }

  return false;
}
} // namespace dory