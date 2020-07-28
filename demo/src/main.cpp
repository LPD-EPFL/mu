#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

#include <dory/conn/exchanger.hpp>
#include <dory/conn/rc.hpp>
#include <dory/ctrl/block.hpp>
#include <dory/ctrl/device.hpp>
#include <dory/shared/units.hpp>
#include <dory/shared/unused-suppressor.hpp>
#include <dory/store.hpp>

using namespace dory;

int main(int argc, char* argv[]) {
  if (argc < 2) {
    throw std::runtime_error("Provide the id of the process as argument");
  }

  constexpr int nr_procs = 2;
  constexpr int minimum_id = 1;
  int id = 0;
  switch (argv[1][0]) {
    case '1':
      id = 1;
      break;
    case '2':
      id = 2;
      break;
    case '3':
      id = 3;
      break;
    case '4':
      id = 4;
      break;
    case '5':
      id = 5;
      break;
    default:
      throw std::runtime_error("Invalid id");
  }

  using namespace units;
  size_t allocated_size = 1_GiB;

  int alignment = 64;

  // Build the list of remote ids
  std::vector<int> remote_ids;
  for (int i = 0, min_id = minimum_id; i < nr_procs; i++, min_id++) {
    if (min_id == id) {
      continue;
    } else {
      remote_ids.push_back(min_id);
    }
  }

  std::vector<int> ids(remote_ids);
  ids.push_back(id);

  // Exchange info using memcached
  auto& store = MemoryStore::getInstance();

  Devices d;
  OpenDevice od;

  // Get the last device
  {
    // TODO: The copy constructor is invoked here if we use auto and then
    // iterate on the dev_lst
    // auto dev_lst = d.list();
    for (auto& dev : d.list()) {
      od = std::move(dev);
    }
  }

  std::cout << od.name() << " " << od.dev_name() << " "
            << OpenDevice::type_str(od.node_type()) << " "
            << OpenDevice::type_str(od.transport_type()) << std::endl;

  ResolvedPort rp(od);
  auto binded = rp.bindTo(0);
  std::cout << "Binded successful? " << binded << std::endl;
  std::cout << "(port_id, port_lid) = (" << +rp.portID() << ", "
            << +rp.portLID() << ")" << std::endl;

  // Configure the control block
  ControlBlock cb(rp);
  cb.registerPD("primary");
  cb.allocateBuffer("shared-buf", allocated_size, alignment);
  cb.registerMR("shared-mr", "primary", "shared-buf",
                ControlBlock::LOCAL_READ | ControlBlock::LOCAL_WRITE |
                    ControlBlock::REMOTE_READ | ControlBlock::REMOTE_WRITE);
  cb.registerCQ("cq");

  ConnectionExchanger ce(id, remote_ids, cb);
  ce.configure_all("primary", "shared-mr", "cq", "cq");
  ce.announce_all(store, "qp");

  auto shared_memory_addr = reinterpret_cast<uint8_t*>(cb.mr("shared-mr").addr);

  std::cout << "Waiting (10 sec) for all processes to fetch the connections"
            << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(10));

  ce.connect_all(store, "qp",
                 ControlBlock::LOCAL_READ | ControlBlock::LOCAL_WRITE |
                     ControlBlock::REMOTE_READ | ControlBlock::REMOTE_WRITE);

  return 0;
}
