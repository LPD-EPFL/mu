
#include <array>
#include <atomic>
#include <chrono>
#include <ios>
#include <iostream>
#include <map>
#include <sstream>

#include "failure-detector.hpp"
#include <dory/ctrl/block.hpp>
#include <dory/ctrl/device.hpp>
#include <dory/store.hpp>

int main(int argc, char *argv[]) {
  // uint64_t data[24];
  // int len = 24;

  // std::vector <uint64_t, dory::OverlayAllocator<uint64_t>> res;

  // {
  //   // Usage
  //   dory::OverlayAllocator<uint64_t> alloc;
  //   {
  //     alloc = dory::OverlayAllocator<uint64_t>((uint64_t *)data, len);
  //   }
  //   std::vector <uint64_t, dory::OverlayAllocator<uint64_t>> p(len, alloc);
  //   p.resize(len);

  //   res = p;
  // }

#if 1
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
  auto &store = dory::MemoryStore::getInstance();

  dory::Devices d;
  dory::OpenDevice od;

  // auto &dev_lst = d.list();
  // auto &od = dev_lst[0];

  {
    // TODO: The copy constructor is invoked here if we use auto and then
    // iterate on the dev_lst
    // auto dev_lst = d.list();
    for (auto &dev : d.list()) {
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

  dory::FailureDetector fd(id, remote_ids, cb);
  fd.configure("primary", "leader-election-mr",
               "cq-leader-election-counter-for-all",
               "cq-leader-election-counter-for-all");
  fd.announce(store);

  std::cout << "Waiting (10 sec) for all processes to fetch the connections"
            << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(10));

  fd.connect(store);

  std::cout << "Waiting (5 sec) for all processes to establish the connections"
            << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(5));

  std::cout << "Incrementing the counter in addr " << std::hex
            << cb.mr("leader-election-mr").addr << std::endl;
  uint64_t *local_counter =
      reinterpret_cast<uint64_t *>(cb.mr("leader-election-mr").addr);
  fd.heartbeatCounterStart(local_counter);

  uint64_t *remote_counters = local_counter + 64 / sizeof(uint64_t);
  fd.allocateCountersOverlay(remote_counters);

  // printf("Address raw = %llu, pointer=%p\n",
  // cb.mr("leader-election-mr").addr, local_counter);

#endif

  auto &cq = cb.cq("cq-leader-election-counter-for-all");

  for (int i = 0; i < 10000000; i++) {
    // std::cout << "Running detection" << std::endl;
    fd.detect(cq);
    std::cout << "(" << i << ") "
              << "Leader so far " << fd.leaderPID() << std::endl;
    // fd.leaderPID();
    std::cout << std::endl;
    std::this_thread::sleep_for(std::chrono::nanoseconds(750));
  }

  std::this_thread::sleep_for(std::chrono::seconds(20));

  // uint64_t read_seq = 0;

  // if (id == 1) {
  //   while (true) {

  //     for (auto& [pid, rc]: rcs) {
  //       // std::cout << "Posting read for pid " << pid << std::endl;
  //       // auto post_ret = rc.postSendSingle(uint64_t(pid) << 48 | read_seq,
  //       &readings.find(pid)->second, sizeof(uint64_t), rc.remoteBuf());
  //       // std::cout << "Reading from remote addr " << std::hex <<
  //       rc.remoteBuf() << std::endl; auto post_ret =
  //       rc.postSendSingle(uint64_t(pid) << 48 | read_seq,
  //       reinterpret_cast<void*>(cb.mr("leader-election-mr").addr + 16 * pid),
  //       sizeof(uint64_t), rc.remoteBuf());

  //       if (!post_ret) {
  //         std::cout << "Post returned " << post_ret << std::endl;
  //       }
  //     }

  //     read_seq += 1;

  //     std::vector<struct ibv_wc> entries(16);
  //     // std::cout << "Problem after here" << std::endl;
  //     if (cb.pollCqIsOK(cq, entries)) {
  //       // std::cout << "Polled " << entries.size() << " entries" <<
  //       std::endl;

  //       for(auto const& entry: entries) {
  //         int pid = entry.wr_id >> 48;
  //         uint64_t seq = (entry.wr_id << 16) >> 16;
  //         volatile uint64_t *val =
  //         reinterpret_cast<uint64_t*>(cb.mr("leader-election-mr").addr + 16 *
  //         pid);

  //         std::cout << "Received (pid, seq) = (" << pid << ", " << seq << "),
  //         value = " << *val << std::endl;
  //       }
  //     }
  //     // std::cout << "Problem before here" << std::endl;

  //     std::this_thread::sleep_for(std::chrono::milliseconds(50));
  //   }
  // }

  // if (id == 4) {
  //   std::this_thread::sleep_for(std::chrono::seconds(10));
  //   std::cout << "Resetting all connections" << std::endl;
  //   for (auto& [pid, rc]: rcs) {
  //     rc.reset();

  //   }

  // }

  // Let the main thread detect the failures

  // if (argc > 1 && argv[1][0] == '2') {
  //   std::this_thread::sleep_for(std::chrono::seconds(10));
  //   volatile uint64_t* read_value = (volatile uint64_t*) ri.rci.buf_addr;
  //   std::cout << "Read value " << *read_value << std::endl;
  // }

  // if (argc > 1 && argv[1][0] == '1') {
  //   while (true) {
  //     uint64_t dummy = 123456789;
  //     std::cout << "Trying to post" << std::endl;
  //     auto post_ret = rc.postSendSingle(&dummy, sizeof(uint64_t),
  //     newRi.rci.buf_addr); std::cout << "Post returned " << post_ret <<
  //     std::endl;

  //     for (int i = 0; i < 5; i++) {
  //       std::vector<struct ibv_wc> entries(5);
  //       if (rc.pollCqIsOK(entries)) {
  //         std::cout << "Polled " << entries.size() << " entries" <<
  //         std::endl;

  //         for(auto const& entry: entries) {
  //           std::cout << "Entry has status " <<
  //           ibv_wc_status_str(entry.status) << std::endl;

  //           if (entry.status != IBV_WC_SUCCESS) {
  //             std::cout << "Trying to re-establish connection" << std::endl;
  //             rc.reset();
  //             rc.init(dory::ControlBlock::LOCAL_READ |
  //                     dory::ControlBlock::LOCAL_WRITE |
  //                     dory::ControlBlock::REMOTE_READ |
  //                     dory::ControlBlock::REMOTE_WRITE);
  //             rc.connect(newRi);
  //           }
  //         }
  //       } else {
  //         std::cout << "Poll returned an error" << std::endl;
  //       }

  //       // std::this_thread::sleep_for(std::chrono::seconds(1));
  //     }
  //   }
  // }

  // if (argc > 1 && argv[1][0] == '2') {
  //   std::this_thread::sleep_for(std::chrono::seconds(30));
  //   rc.reset();
  //   std::cout << "Resetted" << std::endl;
  //   // std::this_thread::sleep_for(std::chrono::seconds(30));
  //   std::cout << "Re-initialized without rights" << std::endl;
  //   rc.init(dory::ControlBlock::LOCAL_READ |
  //           dory::ControlBlock::LOCAL_WRITE);
  //   rc.connect(newRi);
  //   std::cout << "Waiting" << std::endl;
  //   std::this_thread::sleep_for(std::chrono::seconds(30));

  //   std::cout << "Re-initialized with rights" << std::endl;
  //   rc.reset();
  //   rc.init(dory::ControlBlock::LOCAL_READ |
  //           dory::ControlBlock::LOCAL_WRITE |
  //           dory::ControlBlock::REMOTE_READ |
  //           dory::ControlBlock::REMOTE_WRITE);
  //   rc.connect(newRi);
  //   std::cout << "Waiting" << std::endl;
  //   std::this_thread::sleep_for(std::chrono::seconds(300));

  // }

  // std::cout << newRi.rci.lid << std::endl;
  // std::cout << newRi.rci.qpn << std::endl;
  // std::cout << newRi.rci.buf_addr << std::endl;
  // std::cout << newRi.rci.buf_size << std::endl;
  // std::cout << newRi.rci.rkey << std::endl;

  // if (memcmp(&ri.rci, &newRi.rci,
  // sizeof(dory::RemoteConnection::RemoteConnectionInfo)) == 0) {
  //   std::cout << "Serialization/deserialization works!" << std::endl;
  // } else {
  //   std::cout << "Serialization/deserialization failed" << std::endl;
  // }

  return 0;
}
