#include <functional>
#include <future>

#include <dory/conn/exchanger.hpp>
#include <dory/ctrl/ae_handler.hpp>
#include <dory/ctrl/block.hpp>
#include <dory/ctrl/device.hpp>
#include <dory/extern/ibverbs.hpp>
#include <dory/shared/bench.hpp>
#include <dory/shared/logger.hpp>
#include <dory/store.hpp>

#include "consts.hpp"
#include "neb.hpp"

using namespace std::placeholders;

static auto logger = dory::std_out_logger("MAIN");

static constexpr auto num_messages = 2000;

static inline void sleep_sec(int s) {
  logger->info("Sleeping for {} sec", s);
  std::this_thread::sleep_for(std::chrono::seconds(s));
}

/* -------------------------------------------------------------------------- */

class NebSampleMessage : public dory::neb::Broadcastable {
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

  size_t size() const { return sizeof(val); }
};

class NebConsumer {
 public:
  NebConsumer(int num_proc)
      : expected(num_proc * num_messages),
        delivered_counter(0),
        bt(dory::BenchTimer(std::string("Delivies"))),
        f(done_signal.get_future()) {}

  void bench() { bt.start(); }

  void deliver_callback(uint64_t k, volatile const void *m, size_t proc_id) {
    NebSampleMessage msg;

    msg.unmarshall(m);

    logger->info("Delivered ({}, {}) by {}", k, msg.val, proc_id);
    delivered_counter++;

    if (delivered_counter == expected) {
      bt.stop();
      done_signal.set_value();
    }
  }

  void wait_to_finish() const {
    logger->info("Wating to finish");
    while (f.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
    }
  }

 private:
  int expected;
  int delivered_counter;
  dory::BenchTimer bt;

  std::promise<void> done_signal;
  std::future<void> f;
};
/*******************************************************************************
 *                                  MAIN
 ******************************************************************************/
int main(int argc, char *argv[]) {
  /* ------------------------------------------------------------------------ */
  if (argc < 2) {
    logger->error("Provide the process id as first argument!");
    exit(1);
  }

  int num_proc = dory::neb::DEFAULT_NUM_PROCESSES;
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

  /* ------------------------------------------------------------------------ */
  dory::Devices d;

  auto &dev_l = d.list();
  auto &od = dev_l[0];

  dory::ResolvedPort rp(od);

  rp.bindTo(0);

  dory::ControlBlock cb(rp);

  std::promise<void> exit_signal;
  auto async_event_thread =
      std::thread(&dory::ctrl::async_event_handler, logger,
                  exit_signal.get_future(), od.context());

  /* ------------------------------------------------------------------------ */
  constexpr auto pd_str = dory::NonEquivocatingBroadcast::PD_NAME;
  constexpr auto replay_w_str = dory::NonEquivocatingBroadcast::REPLAY_W_NAME;
  constexpr auto replay_r_str = dory::NonEquivocatingBroadcast::REPLAY_R_NAME;
  constexpr auto bcast_w_str = dory::NonEquivocatingBroadcast::BCAST_W_NAME;
  constexpr auto bcast_cq_str = dory::NonEquivocatingBroadcast::BCAST_CQ_NAME;
  constexpr auto replay_cq_str = dory::NonEquivocatingBroadcast::REPLAY_CQ_NAME;
  constexpr auto bcast_prefix = "neb-broadcast";
  constexpr auto replay_prefix = "neb-replay";

  auto &store = dory::MemoryStore::getInstance();

  cb.registerPD(pd_str);

  // REPLAY WRITE BUFFER
  cb.allocateBuffer(replay_w_str, dory::neb::BUFFER_SIZE, 64);
  cb.registerMR(
      replay_w_str, pd_str, replay_w_str,
      dory::ControlBlock::LOCAL_WRITE | dory::ControlBlock::REMOTE_READ);

  // REPLAY READ BUFFER
  cb.allocateBuffer(replay_r_str, dory::neb::BUFFER_SIZE, 64);
  cb.registerMR(replay_r_str, pd_str, replay_r_str,
                dory::ControlBlock::LOCAL_WRITE);

  // BROADCAST WRITE BUFFER
  cb.allocateBuffer(bcast_w_str, dory::neb::BUFFER_SIZE, 64);
  cb.registerMR(bcast_w_str, pd_str, bcast_w_str,
                dory::ControlBlock::LOCAL_WRITE);

  dory::ConnectionExchanger bcast_ce(self_id, remote_ids, cb);
  dory::ConnectionExchanger replay_ce(self_id, remote_ids, cb);

  cb.registerCQ(bcast_cq_str);
  cb.registerCQ(replay_cq_str);

  // Create QPs
  for (auto &id : remote_ids) {
    auto b_str = dory::NonEquivocatingBroadcast::bcast_str(id, self_id);
    auto r_str = dory::NonEquivocatingBroadcast::replay_str(self_id, id);

    // Buffer for Broadcast QP
    cb.allocateBuffer(b_str, dory::neb::BUFFER_SIZE, 64);
    cb.registerMR(
        b_str, pd_str, b_str,
        dory::ControlBlock::LOCAL_WRITE | dory::ControlBlock::REMOTE_WRITE);

    // Broadcast QP
    bcast_ce.configure(id, pd_str, b_str, bcast_cq_str, bcast_cq_str);
    bcast_ce.announce(id, store, bcast_prefix);

    // Replay QP
    replay_ce.configure(id, pd_str, replay_w_str, replay_cq_str, replay_cq_str);
    replay_ce.announce(id, store, replay_prefix);
  }

  sleep_sec(5);

  bcast_ce.connect_all(store, bcast_prefix, dory::ControlBlock::REMOTE_WRITE);
  replay_ce.connect_all(store, replay_prefix, dory::ControlBlock::REMOTE_READ);

  /* ------------------------------------------------------------------------ */
  NebConsumer nc(remote_ids.size() + 1);

  auto deliver_fn = [&](uint64_t k, volatile const void *m, size_t proc_id) {
    nc.deliver_callback(k, m, proc_id);
  };

  dory::NonEquivocatingBroadcast neb(self_id, remote_ids, cb, deliver_fn);

  neb.set_connections(bcast_ce, replay_ce);

  neb.start();

  sleep_sec(1);

  nc.bench();

  NebSampleMessage m;
  for (int i = 1; i <= num_messages; i++) {
    m.val = 1000 * self_id + i;
    neb.broadcast(i, m);
  }

  nc.wait_to_finish();

  // exit async event thread
  exit_signal.set_value();
  async_event_thread.join();

  logger->info("Async Event Thread finished");

  return 0;
}