#include "spdlog/sinks/stdout_color_sinks.h"

#include <dory/store.hpp>
#include "neb.hpp"

namespace dory {

static inline void connect_remote(ReliableConnection &rc, std::string r_qp_name,
                                  ControlBlock::MemoryRights rights,
                                  spdlog::logger &logger) {
  std::string ret_val;

  if (!MemoryStore::getInstance().get(r_qp_name, ret_val)) {
    throw std::runtime_error("Could not retrieve key " + r_qp_name);
  }

  rc.init(rights);
  auto remote_conn = RemoteConnection::fromStr(ret_val);

  rc.connect(remote_conn);
  logger.debug("Connected with remote {} qp", r_qp_name);
}

static inline std::string replay_str(int at, int from) {
  std::stringstream s;
  s << "neb-replay-" << at << "-" << from;
  return s.str();
}

static inline std::string bcast_str(int from, int to) {
  std::stringstream s;
  s << "neb-broadcast-" << from << "-" << to;
  return s.str();
}

static inline ControlBlock::MemoryRegion create_and_get_mr(
    ControlBlock &cb, std::string name, std::string pd_name,
    ControlBlock::MemoryRights rights) {
  cb.allocateBuffer(name, BUFFER_SIZE, 64);

  cb.registerMR(name, pd_name, name, rights);

  return cb.mr(name);
}

static inline void configure_and_publish_qp(ReliableConnection &rc,
                                            std::string PD_NAME,
                                            std::string qp_name,
                                            std::string mr_name,
                                            spdlog::logger &logger) {
  rc.bindToPD(PD_NAME);
  rc.bindToMR(mr_name);
  rc.associateWithCQ(qp_name, qp_name);

  MemoryStore::getInstance().set(qp_name, rc.remoteInfo().serialize());
  logger.debug("Publishing {} qp", qp_name);
}

/* -------------------------------------------------------------------------- */

NonEquivocatingBroadcast::~NonEquivocatingBroadcast() {
  poller_running = false;
  // while (!poller_finished) {
  // }
  logger->info("Poller thread finished\n");
}

NonEquivocatingBroadcast::NonEquivocatingBroadcast(int self_id,
                                                   std::vector<int> remote_ids,
                                                   dory::ControlBlock &cb,
                                                   deliver_callback deliver)
    : self_id(self_id),
      remote_ids{remote_ids},
      num_proc(remote_ids.size() + 1),
      deliver(deliver),
      cb(cb),
      logger(spdlog::stdout_color_mt("NEB")) {
  logger->set_pattern(SPD_FORMAT_STR);
  logger->set_level(spdlog::level::debug);
  logger->info("Initializing");

  next.insert(std::pair<int, uint64_t>(self_id, 1));

  cb.registerPD(PD_NAME);

  auto r_mr_w =
      create_and_get_mr(cb, REPLAY_W_NAME, PD_NAME,
                        ControlBlock::LOCAL_WRITE | ControlBlock::REMOTE_READ);
  replay_w_buf =
      std::make_unique<ReplayBufferWriter>(r_mr_w.addr, r_mr_w.size, num_proc);

  auto r_mr_r =
      create_and_get_mr(cb, REPLAY_R_NAME, PD_NAME, ControlBlock::LOCAL_WRITE);
  replay_r_buf = std::make_unique<ReplayBufferReader>(r_mr_r.addr, r_mr_r.size,
                                                      r_mr_r.lkey, num_proc);

  // Insert a buffer which will be used as scatter when post sending writes
  auto b_mr =
      create_and_get_mr(cb, bcast_str(self_id, self_id), PD_NAME,
                        ControlBlock::LOCAL_WRITE | ControlBlock::REMOTE_WRITE);
  bcast_bufs.insert(std::pair<int, BroadcastBuffer>(
      self_id, BroadcastBuffer(b_mr.addr, b_mr.size, b_mr.lkey)));

  // Create ressources for remote processes
  for (auto &id : remote_ids) {
    next.insert(std::pair<int, uint64_t>(id, 1));

    auto b_name = bcast_str(id, self_id);
    auto b_mr = create_and_get_mr(
        cb, b_name, PD_NAME,
        ControlBlock::LOCAL_WRITE | ControlBlock::REMOTE_WRITE);

    bcast_bufs.insert(std::pair<int, BroadcastBuffer>(
        id, BroadcastBuffer(b_mr.addr, b_mr.size, b_mr.lkey)));

    cb.registerCQ(b_name);

    bcast_conn.insert(
        std::pair<int, ReliableConnection>(id, ReliableConnection(cb)));

    auto &b_rc = bcast_conn.find(id)->second;

    configure_and_publish_qp(b_rc, PD_NAME, b_name, b_name, *logger.get());

    auto r_name = replay_str(self_id, id);

    cb.registerCQ(r_name);

    replay_conn.insert(
        std::pair<int, ReliableConnection>(id, ReliableConnection(cb)));

    auto &r_rc = replay_conn.find(id)->second;

    configure_and_publish_qp(r_rc, PD_NAME, r_name, REPLAY_W_NAME,
                             *logger.get());
  }
}

void NonEquivocatingBroadcast::connect_to_remote_qps() {
  if (connected) return;

  // connect to remote broadcast QPs
  for (auto &[id, rc] : bcast_conn) {
    try {
      connect_remote(rc, bcast_str(self_id, id),
                     ControlBlock::LOCAL_READ | ControlBlock::REMOTE_WRITE,
                     *logger.get());
    } catch (std::runtime_error e) {
      logger->critical("While connecting to remote QP:" +
                       std::string(e.what()));
      return;
    }
  }

  // connect to remote replay QPs
  for (auto &[id, rc] : replay_conn) {
    try {
      connect_remote(rc, replay_str(id, self_id),
                     ControlBlock::LOCAL_WRITE | ControlBlock::REMOTE_READ,
                     *logger.get());
    } catch (std::runtime_error e) {
      logger->critical("While connecting to remote QP: " +
                       std::string(e.what()));
      return;
    }
  }

  connected = true;
  logger->info("Connected");
}

void NonEquivocatingBroadcast::start() {
  if (started) return;

  if (!connected) {
    throw std::runtime_error(
        "Not connected to remote QPs, cannot start serving");
  }

  poller_thread = std::thread([=] { start_poller(); });
  poller_thread.detach();

  started = true;
  logger->info("Started");
}

void NonEquivocatingBroadcast::broadcast(uint64_t k, Broadcastable &msg) {
  const auto next_idx = next[self_id];

  // write to own broadcast buffer and use it as the scatter element for writes
  auto bcast_buf = bcast_bufs.find(self_id)->second;
  auto entry = bcast_buf.get_entry(next_idx);
  auto buf = reinterpret_cast<uint64_t *>(entry->addr());

  buf[0] = k;

  auto msg_size = msg.marshall(reinterpret_cast<void *>(&buf[1]));

  for (auto &it : bcast_conn) {
    auto &rc = it.second;
    auto ret = rc.postSendSingle(
        ReliableConnection::RdmaWrite, next_idx,
        reinterpret_cast<void *>(entry->addr()), msg_size + NEB_MSG_OVERHEAD,
        bcast_buf.lkey, rc.remoteBuf() + bcast_buf.get_byte_offset(next_idx));

    if (!ret) {
      std::cout << "Post returned " << ret << std::endl;
    }
  }

  // increase the message counter
  next[self_id]++;
  // deliver to ourself
  deliver(k, entry->content(), self_id);
}

void NonEquivocatingBroadcast::start_poller() {
  logger->info("Poller thread running");

  if (poller_running) return;

  poller_running = true;

  while (poller_running) {
    for (int &id : remote_ids) {
      uint64_t next_index = next[id];

      auto bcast_entry = bcast_bufs.find(id)->second.get_entry(next_index);

      // TODO(Kristian): eventually check for matching signature
      if (bcast_entry->id() == 0 || bcast_entry->id() != next_index) continue;

      logger->debug(
          "Bcast from {} = ({}, {})", id, bcast_entry->id(),
          *reinterpret_cast<volatile const uint64_t *>(bcast_entry->content()));

      auto replay_entry_w = replay_w_buf->get_entry(id, next_index);

      memcpy((void *)replay_entry_w->addr(), (void *)bcast_entry->addr(),
             BUFFER_ENTRY_SIZE);

      logger->debug("Replay for {} = ({}, {})", id, replay_entry_w->id(),
                    *reinterpret_cast<volatile const uint64_t *>(
                        replay_entry_w->content()));

      bool is_valid = true;

      for (auto &[j, rc] : replay_conn) {
        if (j == id) continue;

        auto replay_entry_r = replay_r_buf->get_entry(id, j, next_index);

        auto ret = rc.postSendSingle(
            ReliableConnection::RdmaRead, next_index,
            reinterpret_cast<void *>(replay_entry_r->addr()), BUFFER_ENTRY_SIZE,
            replay_r_buf->lkey,
            rc.remoteBuf() + replay_w_buf->get_byte_offset(id, next_index));

        if (!ret) {
          std::cout << "Post returned " << ret << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        logger->debug("Replay entry for {} at {} = ({},{})", id, j,
                      replay_entry_r->id(),
                      *reinterpret_cast<volatile const uint64_t *>(
                          replay_entry_r->content()));

        // TODO(Kristian): split the reading from writing
        if (replay_entry_r->id() != 0 &&
            memcmp((void *)bcast_entry->addr(), (void *)replay_entry_r->addr(),
                   BUFFER_ENTRY_SIZE)) {
          is_valid = false;
        }
      }

      if (is_valid) {
        deliver(bcast_entry->id(), bcast_entry->content(), id);
        next[id]++;
      }
    }
  }
  poller_finished = true;
}
}  // namespace dory
