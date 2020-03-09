
#include <ios>
#include <iostream>

#include <chrono>

#include <array>
#include <atomic>
#include <map>
#include <sstream>

#include <dory/conn/exchanger.hpp>
#include <dory/conn/rc.hpp>
#include <dory/ctrl/block.hpp>
#include <dory/ctrl/device.hpp>
#include <dory/shared/unused-suppressor.hpp>
#include <dory/store.hpp>

#include <algorithm>

int main(int argc, char* argv[]) {
  if (argc < 2) {
    throw std::runtime_error("Provide the id of the process as argument");
  }

  constexpr int nr_procs = 4;
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
    default:
      throw std::runtime_error("Invalid id");
  }

  // Build the list of remote ids
  std::vector<int> remote_ids;
  for (int i = 0, min_id = minimum_id; i < nr_procs; i++, min_id++) {
    if (min_id == id) {
      continue;
    } else {
      remote_ids.push_back(min_id);
    }
  }

  // Exchange info using memcached
  auto& store = dory::MemoryStore::getInstance();

  dory::Devices d;
  dory::OpenDevice od;

  // Get the last device
  {
    // TODO: The copy constructor is invoked here if we use auto and then
    // iterate on the dev_lst
    // auto dev_lst = d.list();
    for (auto& dev : d.list()) {
      od = std::move(dev);
    }
  }

  std::cout << od.name() << " " << od.dev_name() << " " << od.guid() << " "
            << dory::OpenDevice::type_str(od.node_type()) << " "
            << dory::OpenDevice::type_str(od.transport_type()) << std::endl;

  dory::ResolvedPort rp(od);
  auto binded = rp.bindTo(0);
  std::cout << "Binded successful? " << binded << std::endl;
  std::cout << "(port_id, port_lid) = (" << +rp.portID() << ", "
            << +rp.portLID() << ")" << std::endl;

  // Configure the control block
  dory::ControlBlock cb(rp);
  cb.registerPD("primary");
  cb.allocateBuffer("leader-election-buf", 1024 * 1024, 64);
  cb.registerMR(
      "leader-election-mr", "primary", "leader-election-buf",
      dory::ControlBlock::LOCAL_READ | dory::ControlBlock::LOCAL_WRITE |
          dory::ControlBlock::REMOTE_READ | dory::ControlBlock::REMOTE_WRITE);
  cb.registerCQ("cq-leader-election-counter-for-all");

  dory::ConnectionExchanger ce(id, remote_ids, cb);
  ce.configure("primary", "leader-election-mr",
               "cq-leader-election-counter-for-all",
               "cq-leader-election-counter-for-all");
  ce.announce(store, "qp-le");

  std::cout << "Waiting (10 sec) for all processes to fetch the connections"
            << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(10));

  if (id == 1) {
    ce.connect(
        store, "qp-le",
        dory::ControlBlock::LOCAL_READ | dory::ControlBlock::LOCAL_WRITE |
            dory::ControlBlock::REMOTE_READ | dory::ControlBlock::REMOTE_WRITE);
  } else {
    ce.connect(store, "qp-le",
               dory::ControlBlock::LOCAL_READ |
                   dory::ControlBlock::LOCAL_WRITE |
                   dory::ControlBlock::REMOTE_READ);
  }

  std::cout << "Waiting (5 sec) for all processes to establish the connections"
            << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(5));

  if (id == 1) {
    volatile uint64_t* value =
        reinterpret_cast<uint64_t*>(cb.mr("leader-election-mr").addr);

    std::vector<std::reference_wrapper<dory::ReliableConnection>> foo;
    auto& rcs = ce.connections();
    for (auto& [pid, rc] : rcs) {
      dory::IGNORE(pid);
      foo.push_back(std::ref(rc));
    }

    std::cout << "Killing connections" << std::endl;

    auto t1 = std::chrono::high_resolution_clock::now();
    for (auto& [pid, rc] : rcs) {
      dory::IGNORE(pid);
      rc.reset();
      rc.init(dory::ControlBlock::LOCAL_READ | dory::ControlBlock::LOCAL_WRITE);
      rc.reconnect();
    }
    // std::for_each(
    //     std::execution::par,
    //     foo.begin(),
    //     foo.end(),
    //     [](dory::ReliableConnection& rc)
    //     {
    //       rc.reset();
    //       rc.init(dory::ControlBlock::LOCAL_READ |
    //               dory::ControlBlock::LOCAL_WRITE);
    //       rc.reconnect();
    //     });
    auto t2 = std::chrono::high_resolution_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
    std::cout << "It takes " << duration << "us to reset 3 connections"
              << std::endl;

    std::this_thread::sleep_for(std::chrono::seconds(5));

    for (auto& [pid, rc] : rcs) {
      dory::IGNORE(pid);
      rc.reset();
      rc.init(dory::ControlBlock::LOCAL_READ | dory::ControlBlock::LOCAL_WRITE |
              dory::ControlBlock::REMOTE_READ |
              dory::ControlBlock::REMOTE_WRITE);
      rc.reconnect();
    }
    std::cout << "Re-establishing connections" << std::endl;

    for (int i = 0; i < 100; i++) {
      std::cout << "V:" << *value << std::endl;
      std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

  } else {
    std::cout << "Waiting for another 10 sec" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(10));

    uint64_t* write_from =
        reinterpret_cast<uint64_t*>(cb.mr("leader-election-mr").addr);
    auto& cq = cb.cq("cq-leader-election-counter-for-all");
    auto& rcs = ce.connections();
    auto& rc = rcs.find(1)->second;
    std::vector<struct ibv_wc> entries;

    for (uint64_t write_seq = 1;; write_seq++) {
      *write_from = id * write_seq;
      rc.postSendSingle(dory::ReliableConnection::RdmaWrite,
                        uint64_t(id) << 48 | write_seq, write_from,
                        sizeof(uint64_t), rc.remoteBuf());

      // char x;
      // std::cin >> x;

      while (true) {
        entries.resize(1);
        if (cb.pollCqIsOK(cq, entries)) {
          std::cout << "Polled " << entries.size() << " entries" << std::endl;

          for (auto const& entry : entries) {
            std::cout << "Entry has status " << ibv_wc_status_str(entry.status)
                      << std::endl;

            if (entry.status != IBV_WC_SUCCESS) {
              std::cout << "Trying to re-establish connection" << std::endl;
              std::this_thread::sleep_for(std::chrono::milliseconds(3000));
              rc.reset();
              rc.init(dory::ControlBlock::LOCAL_READ |
                      dory::ControlBlock::LOCAL_WRITE |
                      dory::ControlBlock::REMOTE_READ);
              rc.reconnect();
            }
          }

          if (entries.size() > 0) {
            break;
          }
        } else {
          std::cout << "Poll returned an error" << std::endl;
        }
      }

      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }

  std::this_thread::sleep_for(std::chrono::seconds(100));
  // Permission switcher

  return 0;
}
