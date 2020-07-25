#include <chrono>
#include <csignal>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <random>
#include <thread>

#include <dory/conn/exchanger.hpp>
#include <dory/crypto/sign/dalek.hpp>
#include <dory/ctrl/ae_handler.hpp>
#include <dory/ctrl/block.hpp>
#include <dory/ctrl/device.hpp>
#include <dory/extern/ibverbs.hpp>
#include <dory/shared/bench.hpp>
#include <dory/shared/logger.hpp>
#include <dory/shared/unused-suppressor.hpp>
#include <dory/store.hpp>

#include "broadcast.hpp"
#include "consts.hpp"

using namespace std::chrono;

static auto logger = dory::std_out_logger("MAIN");

using deliver_sample =
    std::tuple<int, uint64_t, uint64_t, steady_clock::time_point>;
using broadcast_sample = std::tuple<int, uint64_t, steady_clock::time_point>;

/**
 * This handle will be overwritten in the body of the main function where it
 * captures references to the local variables so we can use them in the handler.
 **/
std::function<void(int)> handle = [](int sig) {
  SPDLOG_LOGGER_WARN(logger, "Got signal {} before neb start. Exiting!", sig);
  exit(sig);
};
inline void signal_handler(int sig) { handle(sig); }

/**
 * Writes the provided data samples to the file specified by `file_name`.
 * In each vector of samples the first element represents the start point of
 * measurement.
 * @param deliveries: vector holding tuples (<origin, k, msg, time-point>)
 * @param broadcasts: vector holding tuples (<k, msg, time-point>)
 **/
inline void write(const std::vector<deliver_sample> &deliveries,
                  const std::vector<broadcast_sample> &broadcasts,
                  const char *file_name) {
  std::ofstream fs;

  fs.open(file_name);
  {
    auto &[nutin1, nutin2, nutin3, start] = deliveries[0];

    dory::IGNORE(nutin1);
    dory::IGNORE(nutin2);
    dory::IGNORE(nutin3);

    for (size_t i = 1; i < deliveries.size(); i++) {
      auto &[pid, k, val, ts] = deliveries[i];

      // we only write actual samples
      if (pid == 0) continue;

      auto diff = duration_cast<nanoseconds>(ts - start);

      fs << "d " << pid << " " << k << " " << val << " " << diff.count()
         << "\n";
    }
  }
  {
    auto &[nutin1, nutin2, start] = broadcasts[0];
    dory::IGNORE(nutin1);
    dory::IGNORE(nutin2);

    for (size_t i = 1; i < broadcasts.size(); i++) {
      auto &[k, val, ts] = broadcasts[i];

      // we only write actual samples
      if (k == 0) continue;

      auto diff = duration_cast<nanoseconds>(ts - start);

      fs << "b " << k << " " << val << " " << diff.count() << "\n";
    }
  }

  fs.close();
}

/**
 * Reads and parses the membership file.
 *
 * @param self_id: the id of the local process
 * @returns a tuple holding:
 *            1. total number of processes
 *            2. vector holding remote ids
 *            3. vector holding process ids
 *            4. map holding the number of messages to send for every process
 **/
inline std::tuple<int, std::vector<int>, std::vector<int>, std::map<int, int>>
read_membership(int self_id) {
  std::string line;
  std::ifstream fs("./membership");
  std::vector<int> remote_ids;
  std::vector<int> process_ids;
  std::map<int, int> num_msg;

  int num_proc = 2;
  int i = 0;

  if (!fs.is_open()) {
    throw std::runtime_error("`membership` file missing!");
  }

  while (std::getline(fs, line)) {
    if (i == 0) {
      num_proc = atoi(line.c_str());
    } else {
      auto s = line.find(" ");

      int pid = atoi(line.substr(0, s).c_str());
      int nmsg = atoi(line.substr(s, line.size()).c_str());

      if (i <= num_proc) {
        num_msg.insert({pid, nmsg});
        process_ids.push_back(pid);
        if (i != self_id) {
          remote_ids.push_back(pid);
        }
      }
    }
    i++;
  }

  return {num_proc, remote_ids, process_ids, num_msg};
}

/**
 * This class provides a sample implementation of the required
 *`Broadcastable` interface for messages that should be broadcasted by the
 * NonEquivocatingBroadcast module.
 **/
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

/**
 * Simple consumer class that counts broadcasts and deliveries and writes
 * collected timestaps to a file.
 **/
class NebConsumer {
 public:
  NebConsumer(std::map<int, int> &num_msgs, int self_id)
      // : num_msgs(num_msgs),
      : delivered_counter(0),
        bt(dory::BenchTimer("Deliveries")),
        f(done_signal.get_future()) {
    expected = 0;

    for (auto &[pid, num] : num_msgs) {
      dory::IGNORE(pid);
      expected += num;
    }

    // +1 for the start point
    deliveries.resize(expected + 1);
    broadcasts.resize(num_msgs.find(self_id)->second + 1);
  }

  void bench() {
    bt.start();
    auto stp = steady_clock::now();
    deliveries[0] = {0, 0, 0, stp};
    broadcasts[0] = {0, 0, stp};
  }

  void log_broadcast(int k, uint64_t val) {
    broadcasts[k] = {k, val, steady_clock::now()};
  }

  void deliver_callback(uint64_t k, volatile const void *m, size_t proc_id) {
    NebSampleMessage msg;
    msg.unmarshall(m);

    delivered_counter += 1;

    deliveries[delivered_counter] = {proc_id, k, msg.val, steady_clock::now()};

    if (delivered_counter == expected) {
      bt.stop();
      done_signal.set_value();
    }

    SPDLOG_LOGGER_INFO(logger, "Delivered ({}, {}) by {}, overall: {}", k,
                       msg.val, proc_id, delivered_counter);
  }

  void wait_to_finish() const {
    SPDLOG_LOGGER_CRITICAL(logger,
                           "Done boradcasting. Wating for all messages.");

    // block until we delivered all expected messages
    while (f.wait_for(seconds(0)) != std::future_status::ready) {
    }

    write_samples();
  }

  void write_samples() const { write(deliveries, broadcasts, OUT_FILE); }

 private:
  static constexpr auto OUT_FILE = "/tmp/neb-bench-ts";

  int expected;
  // std::map<int, int> &num_msgs;
  volatile int delivered_counter;
  dory::BenchTimer bt;

  std::promise<void> done_signal;
  std::future<void> f;
  std::vector<deliver_sample> deliveries;
  std::vector<broadcast_sample> broadcasts;
};

/*******************************************************************************
 *                                  MAIN
 ******************************************************************************/
int main(int argc, char *argv[]) {
  std::random_device dev;
  std::mt19937 rng(dev());

  signal(SIGINT, signal_handler);
  logger->set_level(spdlog::level::trace);

  if (argc < 2) {
    SPDLOG_LOGGER_CRITICAL(logger, "Provide the process id as first argument!");
    exit(1);
  }

  int self_id = atoi(argv[1]);
  auto &&[num_proc, remote_ids, process_ids, num_msgs] =
      read_membership(self_id);

  assert(num_proc > 1);

  dory::IGNORE(num_proc);

  constexpr auto PKEY_PREFIX = "pkey-p";

  dory::crypto::dalek::init();
  dory::crypto::dalek::publish_pub_key(PKEY_PREFIX + std::to_string(self_id));

  /* --------------------------CONTROL PATH---------------------------------- */
  dory::Devices d;

  auto &dev_l = d.list();
  auto &od = dev_l[0];

  dory::ResolvedPort rp(od);

  rp.bindTo(0);

  dory::ControlBlock cb(rp);

  auto &store = dory::MemoryStore::getInstance();

  dory::ConnectionExchanger bcast_ce(self_id, remote_ids, cb);
  dory::ConnectionExchanger replay_ce(self_id, remote_ids, cb);

  dory::neb::async::NonEquivocatingBroadcast::run_default_async_control_path(
      cb, store, bcast_ce, replay_ce, self_id, process_ids, logger);

  NebConsumer nc(num_msgs, self_id);

  auto deliver_fn = [&](uint64_t k, volatile const void *m, size_t proc_id) {
    nc.deliver_callback(k, m, proc_id);
  };

  dory::neb::async::NonEquivocatingBroadcast neb(self_id, process_ids, cb,
                                                 deliver_fn);

  neb.set_connections(bcast_ce, replay_ce);
  neb.set_remote_keys(
      dory::crypto::dalek::get_public_keys(PKEY_PREFIX, remote_ids));

  // so we can capture the local scope and use it in the signal handler
  handle = [&](int sig) {
    SPDLOG_LOGGER_WARN(logger, "Got signal {}. Stopping and writing output!",
                       sig);

    nc.write_samples();
    neb.end();

    exit(sig);
  };

  store.set("dory__neb__" + std::to_string(self_id) + "__connected", "1");

  SPDLOG_LOGGER_INFO(
      logger, "Waiting for all remote processes to connect to published QPs");

  for (int pid : remote_ids) {
    auto key = "dory__neb__" + std::to_string(pid) + "__connected";
    std::string val;

    while (!store.get(key, val))
      ;

    SPDLOG_LOGGER_INFO(logger, "Remote process {} is ready", pid);
  }

  NebSampleMessage m;
  const int to_bcast = num_msgs.find(self_id)->second;

  neb.resize_ttd(num_msgs);
  nc.bench();

  neb.start();

  auto broadcast_thread = std::thread([&]() {
    for (int i = 1; i <= to_bcast; i++) {
      m.val = 1000000 * self_id + i;
      nc.log_broadcast(i, m.val);
      neb.broadcast(i, m);
      // std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
  });

  dory::pinThreadToCore(broadcast_thread, 18);

  broadcast_thread.join();

  nc.wait_to_finish();

  store.set("dory__neb__" + std::to_string(self_id) + "__done", "1");

  SPDLOG_LOGGER_INFO(logger,
                     "Waiting for others to finish. C-c for force stop.");

  for (int pid : remote_ids) {
    auto key = "dory__neb__" + std::to_string(pid) + "__done";
    std::string val;
    while (!store.get(key, val)) {
      std::this_thread::sleep_for(std::chrono::seconds(3));
      SPDLOG_LOGGER_INFO(logger, "waiting for {}", pid);
    }
    SPDLOG_LOGGER_INFO(logger, "{} is done!", pid);
  }

  SPDLOG_LOGGER_CRITICAL(
      logger, "All finished, C-c to terminate run and write output!");

  return 0;
}