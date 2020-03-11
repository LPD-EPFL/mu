#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/spdlog.h"

#include <dory/conn/exchanger.hpp>
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
    if (num_proc < 1) {
      logger->error("At least one processes is required!");
      exit(1);
    }
  }

  std::vector<int> remote_ids;

  for (int i = 1; i <= num_proc; i++) {
    if (i == self_id) continue;
    remote_ids.push_back(i);
  }

  dory::Devices d;

  auto &dev_l = d.list();
  auto &od = dev_l[0];

  dory::ResolvedPort rp(od);

  rp.bindTo(0);

  dory::ControlBlock cb(rp);

  /* ------------------------------------------------------------------------ */
  constexpr auto pd_name = dory::NonEquivocatingBroadcast::PD_NAME;
  constexpr auto replay_w_name = dory::NonEquivocatingBroadcast::REPLAY_W_NAME;
  constexpr auto replay_r_name = dory::NonEquivocatingBroadcast::REPLAY_R_NAME;
  constexpr auto bcast_w_name = dory::NonEquivocatingBroadcast::BCAST_W_NAME;
  constexpr auto bcast_prefix = "neb-broadcast";
  constexpr auto replay_prefix = "neb-replay";

  auto &store = dory::MemoryStore::getInstance();

  cb.registerPD(pd_name);

  // REPLAY WRITE BUFFER
  cb.allocateBuffer(replay_w_name, BUFFER_SIZE, 64);
  cb.registerMR(
      replay_w_name, pd_name, replay_w_name,
      dory::ControlBlock::LOCAL_WRITE | dory::ControlBlock::REMOTE_READ);

  // REPLAY READ BUFFER
  cb.allocateBuffer(replay_r_name, BUFFER_SIZE, 64);
  cb.registerMR(replay_r_name, pd_name, replay_r_name,
                dory::ControlBlock::LOCAL_WRITE);

  // BROADCAST WRITE BUFFER
  cb.allocateBuffer(bcast_w_name, BUFFER_SIZE, 64);
  cb.registerMR(bcast_w_name, pd_name, bcast_w_name,
                dory::ControlBlock::LOCAL_WRITE);

  dory::ConnectionExchanger bcast_ce(self_id, remote_ids, cb);
  dory::ConnectionExchanger replay_ce(self_id, remote_ids, cb);

  // Create QPs
  for (auto &id : remote_ids) {
    auto b_name = dory::NonEquivocatingBroadcast::bcast_str(id, self_id);

    cb.allocateBuffer(b_name, BUFFER_SIZE, 64);
    cb.registerMR(
        b_name, pd_name, b_name,
        dory::ControlBlock::LOCAL_WRITE | dory::ControlBlock::REMOTE_WRITE);
    cb.registerCQ(b_name);

    bcast_ce.configure(id, pd_name, b_name, b_name, b_name);
    bcast_ce.announce(id, store, bcast_prefix);

    auto r_name = dory::NonEquivocatingBroadcast::replay_str(self_id, id);

    cb.registerCQ(r_name);

    replay_ce.configure(id, pd_name, replay_w_name, r_name, r_name);
    replay_ce.announce(id, store, replay_prefix);
  }

  logger->info("Sleeping for 5 sec");
  std::this_thread::sleep_for(std::chrono::seconds(5));

  bcast_ce.connect_all(store, bcast_prefix, dory::ControlBlock::REMOTE_WRITE);
  replay_ce.connect_all(
      store, replay_prefix,
      dory::ControlBlock::LOCAL_WRITE | dory::ControlBlock::REMOTE_READ);

  /* ------------------------------------------------------------------------ */

  dory::NonEquivocatingBroadcast neb(self_id, remote_ids, cb, deliver_callback);

  neb.set_connections(bcast_ce, replay_ce);

  neb.start();

  logger->info("Sleeping for 1 sec");
  std::this_thread::sleep_for(std::chrono::seconds(1));

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