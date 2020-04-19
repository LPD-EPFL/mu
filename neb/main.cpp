#include <chrono>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <thread>

#include <dory/conn/exchanger.hpp>
#include <dory/crypto/sign.hpp>
#include <dory/ctrl/ae_handler.hpp>
#include <dory/ctrl/block.hpp>
#include <dory/ctrl/device.hpp>
#include <dory/extern/ibverbs.hpp>
#include <dory/shared/bench.hpp>
#include <dory/shared/logger.hpp>
#include <dory/shared/unused-suppressor.hpp>
#include <dory/store.hpp>

#include "consts.hpp"
#include "neb.hpp"

static constexpr auto PKEY_PREFIX = "pkey-p";
static auto logger = dory::std_out_logger("MAIN");
static auto num_messages = 1000;

void write(const std::vector<
               std::tuple<int, uint64_t, std::chrono::steady_clock::time_point>>
               &samples,
           const char *file_name) {
  std::ofstream fs;

  fs.open(file_name);
  auto &[nutin1, nutin2, start] = samples[0];

  dory::IGNORE(nutin1);
  dory::IGNORE(nutin2);

  for (size_t i = 1; i < samples.size(); i++) {
    auto &[pid, k, ts] = samples[i];
    auto diff =
        std::chrono::duration_cast<std::chrono::nanoseconds>(ts - start);

    fs << pid << " " << k << " " << diff.count() << "\n";
  }

  fs.close();
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
        bt(dory::BenchTimer("Deliveries")),
        f(done_signal.get_future()) {
    // +1 for the start point
    timestamps.resize(expected + 1);
  }

  void bench() {
    bt.start();
    timestamps[0] =
        std::tuple<int, uint64_t, std::chrono::steady_clock::time_point>(
            0, 0, std::chrono::steady_clock::now());
  }

  void deliver_callback(uint64_t k, volatile const void *m, size_t proc_id) {
    delivered_counter += 1;

    timestamps[delivered_counter] =
        std::tuple<int, uint64_t, std::chrono::steady_clock::time_point>(
            proc_id, k, std::chrono::steady_clock::now());

    if (delivered_counter == expected) {
      bt.stop();
      done_signal.set_value();
    }

    dory::IGNORE(k);
    dory::IGNORE(m);
    dory::IGNORE(proc_id);

    NebSampleMessage msg;
    msg.unmarshall(m);
    LOGGER_DEBUG(logger, "Delivered ({}, {}) by {}, overall: {}", k, msg.val,
                 proc_id, delivered_counter);
  }

  void wait_to_finish() const {
    LOGGER_INFO(logger, "Done boradcasting. Wating for all messages.");

    // block until we delivered all expected messages
    while (f.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
    }

    write(timestamps, "/tmp/neb-bench-ts");
  }

 private:
  int expected;
  volatile int delivered_counter;
  dory::BenchTimer bt;

  std::promise<void> done_signal;
  std::future<void> f;
  std::vector<std::tuple<int, uint64_t, std::chrono::steady_clock::time_point>>
      timestamps;
};
/*******************************************************************************
 *                                  MAIN
 ******************************************************************************/
int main(int argc, char *argv[]) {
  logger->set_level(spdlog::level::debug);
  /* ------------------------------------------------------------------------ */
  if (argc < 2) {
    LOGGER_CRITICAL(logger, "Provide the process id as first argument!");
    exit(1);
  }

  int num_proc = dory::neb::DEFAULT_NUM_PROCESSES;
  int self_id = atoi(argv[1]);

  if (argc > 2) {
    num_proc = atoi(argv[2]);
    assert(num_proc > 1);
  }

  if (argc > 3) {
    num_messages = atoi(argv[3]);
    assert(num_messages >= 0);
  }

  std::vector<int> remote_ids;

  for (int i = 1; i <= num_proc; i++) {
    if (i != self_id) remote_ids.push_back(i);
  }

  dory::crypto::init();
  dory::crypto::publish_pub_key(PKEY_PREFIX + std::to_string(self_id));

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

  store.set("dory__neb__" + std::to_string(self_id) + "__published", "1");
  LOGGER_INFO(logger, "Waiting for all remote processes to publish their QPs");
  for (int pid : remote_ids) {
    auto key = "dory__neb__" + std::to_string(pid) + "__published";
    std::string val;

    while (!store.get(key, val))
      std::this_thread::sleep_for(std::chrono::microseconds(100));
  }

  bcast_ce.connect_all(store, bcast_prefix, dory::ControlBlock::REMOTE_WRITE);
  replay_ce.connect_all(store, replay_prefix, dory::ControlBlock::REMOTE_READ);

  /* ------------------------------------------------------------------------ */
  NebConsumer nc(remote_ids.size() + 1);

  auto deliver_fn = [&](uint64_t k, volatile const void *m, size_t proc_id) {
    nc.deliver_callback(k, m, proc_id);
  };

  dory::NonEquivocatingBroadcast neb(self_id, remote_ids, cb, deliver_fn);

  neb.set_connections(bcast_ce, replay_ce);
  neb.set_remote_keys(dory::crypto::get_public_keys(PKEY_PREFIX, remote_ids));

  neb.start();

  store.set("dory__neb__" + std::to_string(self_id) + "__connected", "1");

  LOGGER_INFO(logger,
              "Waiting for all remote processes to connect to published QPs");

  for (int pid : remote_ids) {
    auto key = "dory__neb__" + std::to_string(pid) + "__connected";
    std::string val;

    while (!store.get(key, val))
      ;

    LOGGER_INFO(logger, "Remote process {} is ready", pid);
  }

  nc.bench();

  NebSampleMessage m;
  for (int i = 1; i <= num_messages; i++) {
    m.val = 1000000 * self_id + i;
    neb.broadcast(i, m);
    // std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  nc.wait_to_finish();

  LOGGER_INFO(logger, "Sleeping for 10 seconds to let others finish");
  std::this_thread::sleep_for(std::chrono::seconds(10));

  neb.end();

  // exit async event thread
  exit_signal.set_value();
  async_event_thread.join();

  LOGGER_INFO(logger, "Async Event Thread finished");

  return 0;
}