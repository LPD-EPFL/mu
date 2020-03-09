#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/spdlog.h"

#include <dory/ctrl/block.hpp>
#include <dory/ctrl/device.hpp>
#include <dory/store.hpp>
#include "consts.hpp"
#include "neb.hpp"

static auto logger = spdlog::stdout_color_mt("MAIN");

class NebSampleMessage : public dory::NonEquivocatingBroadcast::Broadcastable {
 public:
  uint64_t val;

  size_t marshall(volatile void *buf) const {
    auto b = reinterpret_cast<volatile uint64_t *>(buf);

    b[0] = val;

    return sizeof(val);
  }

  void unmarshall(volatile const void *buf) {
    val = *reinterpret_cast<volatile const uint64_t *>(buf);
  }
};

void deliver_callback(uint64_t k, volatile const void *m, size_t proc_id) {
  NebSampleMessage msg;

  msg.unmarshall(m);

  logger->info("Delivered ({}, {}) by {}", k, msg.val, proc_id);
}

int main(int argc, char *argv[]) {
  logger->set_pattern(SPD_FORMAT_STR);

  if (argc < 2) {
    logger->error("Provide the process id as first argument!");
    exit(1);
  }

  int num_proc = DEFAULT_NUM_PROCESSES;
  int self_id = atoi(argv[1]);

  if (argc > 2) {
    num_proc = atoi(argv[2]);
    if (num_proc < 2) {
      logger->error("At least two processes are required!");
      exit(1);
    }
  }

  std::vector<int> remote_ids;

  for (int i = 0; i < num_proc; i++) {
    if (i == self_id) continue;

    remote_ids.push_back(i);
  }

  dory::Devices d;

  auto &dev_l = d.list();
  auto &od = dev_l[0];

  dory::ResolvedPort rp(od);

  rp.bindTo(0);

  dory::ControlBlock cb(rp);

  dory::NonEquivocatingBroadcast neb(self_id, remote_ids, cb, deliver_callback);

  logger->info("Sleeping for 5 sec");
  std::this_thread::sleep_for(std::chrono::seconds(5));

  neb.connect_to_remote_qps();

  neb.start();

  NebSampleMessage m;
  for (int i = 1; i <= 135; i++) {
    m.val = 1000 * self_id + i;
    neb.broadcast(i, m);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  logger->info("Sleeping for 3 sec");
  std::this_thread::sleep_for(std::chrono::seconds(3));

  return 0;
}
