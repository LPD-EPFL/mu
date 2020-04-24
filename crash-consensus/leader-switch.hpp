#pragma once

#include "error.hpp"
#include "log.hpp"
#include "message-identifier.hpp"

#include <iterator>
#include <set>

#include "context.hpp"
#include "remote-log-reader.hpp"

#include "config.hpp"
#include "fixed-size-majority.hpp"
#include "pinning.hpp"
#include "follower.hpp"

#include "timers.h"

#include <fcntl.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>


#include "contexted-poller.hpp"

namespace dory {
struct LeaderContext {
  LeaderContext(ConnectionContext &cc, ScratchpadMemory &scratchpad)
      : cc{cc}, scratchpad{scratchpad}, poller{&cc} {

  }
  ConnectionContext &cc;
  ScratchpadMemory &scratchpad;
  ContextedPoller poller;
};
}  // namespace dory

namespace dory {
class LeaderHeartbeat {
 private:
    static constexpr double gapFactor = 2;
    static constexpr std::chrono::nanoseconds heartbeatRefreshRate = std::chrono::nanoseconds(500);
    static constexpr int fail_retry_interval = 1024;
    static constexpr int failed_attempt_limit = 6;
    static constexpr int history_length = 15;

 public:
  LeaderHeartbeat() {}
  LeaderHeartbeat(LeaderContext *ctx) : ctx{ctx}, want_leader{false} {
    // Careful, there is a move assignment happening!
  }

  void startPoller() {
    read_seq = 0;

    rcs = &(ctx->cc.ce.connections());
    my_id = ctx->cc.my_id;
    loopback = &(ctx->cc.ce.loopback());

    offset = ctx->scratchpad.leaderHeartbeatSlotOffset();
    counter_from = reinterpret_cast<uint64_t*>(ctx->scratchpad.leaderHeartbeatSlot() + 64);
    *counter_from = 0;

    slots = ctx->scratchpad.readLeaderHeartbeatSlots();

    ids = ctx->cc.remote_ids;
    ids.push_back(ctx->cc.my_id);
    std::sort(ids.begin(), ids.end());

    max_id = *(std::minmax_element(ids.begin(), ids.end()).second);
    status = std::vector<ReadingStatus>(max_id + 1);

    ctx->poller.registerContext(quorum::LeaderHeartbeat);
    ctx->poller.endRegistrations(3);

    heartbeat_poller = ctx->poller.getContext(quorum::LeaderHeartbeat);

    post_id = 0;
    post_ids.resize(max_id + 1);
    for (auto &id : post_ids) {
      id = post_id;
    }
  }

  void retract() {
    want_leader.store(false);
  }

  void scanHeartbeats() {

    if (outstanding_pids.find(my_id) == outstanding_pids.end()) {
      // Update my heartbeat
      *counter_from += 1;
      auto post_ret = loopback->postSendSingle(ReliableConnection::RdmaWrite, quorum::pack(quorum::LeaderHeartbeat, my_id, 0), counter_from, sizeof(uint64_t), loopback->remoteBuf() + offset);

      if (!post_ret) {
        std::cout << "Post returned " << post_ret << std::endl;
      }

      outstanding_pids.insert(my_id);
    }

    bool did_work = false;
    auto &rcs_ = *rcs;
    for (auto& [pid, rc]: rcs_) {

      if (outstanding_pids.find(pid) != outstanding_pids.end()) {
        continue;
      }

      did_work = true;
      outstanding_pids.insert(pid);
      post_ids[pid] = post_id;

      // std::cout << "Posting PID: " << pid << ", PostID: " << post_id << std::endl;

      auto post_ret = rc.postSendSingle(ReliableConnection::RdmaRead, quorum::pack(quorum::LeaderHeartbeat, pid, read_seq), slots[pid], sizeof(uint64_t), rc.remoteBuf() + offset);

      if (!post_ret) {
        std::cout << "Post returned " << post_ret << std::endl;
      }
    }

    if (did_work) {
      post_id += 1;
    }

    read_seq += 1;

    // If the number of outstanding requests goes out of hand, go slower
    // do {
    entries.resize(outstanding_pids.size());
    // std::vector<struct ibv_wc> entries(outstanding);
    // std::cout << "I have " << outstanding << " requests to be polled, max_id " << max_id << std::endl;

    if (heartbeat_poller(ctx->cc.cq, entries)) {
      // std::cout << "Polled " << entries.size() << " entries" << std::endl;

      for(auto const& entry: entries) {
        auto [k, pid, seq] = quorum::unpackAll<uint64_t, uint64_t>(entry.wr_id);
        IGNORE(k);
        IGNORE(seq);

        outstanding_pids.erase(pid);
        auto proc_post_id = post_ids[pid];

        volatile uint64_t *val = reinterpret_cast<uint64_t*>(slots[pid]);
        if (pid == unsigned(my_id)) {
          val = reinterpret_cast<uint64_t*>(loopback->remoteBuf() + offset);
        }

        // std::cout << "Polling PID: " << pid << ", PostID: " << proc_post_id << ", Value: " << *val << std::endl;

        if (status[pid].value == *val) {
          // status[pid].same_value += 1;
          // std::cout << "Same value" << std::endl;

          // if (status[pid].same_value == )
          status[pid].consecutive_updates = std::max(status[pid].consecutive_updates - 1, 0);
        } else {
          if (post_id < proc_post_id + 3 ) {
            status[pid].consecutive_updates = std::min(status[pid].consecutive_updates + 5, history_length);
          }
        }

        // if (status[pid].value != *val) {
        //   std::cout << "Polling PID: " << pid << ", PostID: " << proc_post_id << ", Value: " << *val << std::endl;
        // }

        status[pid].value = *val;
        // std::cout << "Received (pid, seq) = (" << pid << ", " << seq << "), value = " << *val << std::endl;
      }

      // // Penalize the outstanding that are way behind
      // for (auto pid : outstanding_pids) {
      //   auto proc_post_id = post_ids[pid];

      //   if (post_id > proc_post_id + 3 ) {
      //     // std::cout << "Penalizing " << pid << std::endl;
      //     status[pid].consecutive_updates = std::max(status[pid].consecutive_updates - 2, 0);
      //   }
      // }
    }

    // bool ok = false;
    // for (auto& pid: ids) {
    //   if (status[pid].consecutive_updates < 7) {
    //     ok = true;
    //     std::cout << "PID:" << pid << ", score: " << status[pid].consecutive_updates << std::endl;
    //   }
    // }
    // if (ok) {
    //   std::cout << std::endl;
    // }

    if (leader_pid() == ctx->cc.my_id) {
      want_leader.store(true);
    } else {
      std::this_thread::sleep_for(std::chrono::microseconds(10));
      // std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
  }

  std::atomic<bool> &wantLeaderSignal() { return want_leader; }

  // Move assignment operator
  LeaderHeartbeat &operator=(LeaderHeartbeat &&o) {
    if (&o == this) {
      return *this;
    }

    ctx = o.ctx;
    o.ctx = nullptr;
    want_leader.store(false);
    return *this;
  }

 private:
  struct ReadingStatus {
    ReadingStatus() :
      value{ 0 }, consecutive_updates{ 0 }, failed_attempts{ 0 }, loop_modulo{ 0 }, freshly_updated{false}
    {}

    int outstanding;
    uint64_t value;
    int consecutive_updates;
    int failed_attempts;
    int loop_modulo;
    bool freshly_updated;
  };

 private:

  int leader_pid() {
    int leader_id = -1;

    for (auto& pid : ids) {
      // std::cout << pid << " " << status[pid].consecutive_updates << std::endl;
      if (status[pid].consecutive_updates > 7) {
        leader_id = pid;
        break;
      }
    }

    return leader_id;
  }

  LeaderContext *ctx;
  std::atomic<bool> want_leader;
  std::map<int, ReliableConnection> *rcs;
  ReliableConnection *loopback;

  uint64_t read_seq;

  uint64_t post_id;
  std::vector<uint64_t> post_ids;
  std::set<int> outstanding_pids;


  PollingContext heartbeat_poller;
  std::vector<ReadingStatus> status;

  ptrdiff_t offset;
  std::vector<uint8_t *> slots;
  std::vector<struct ibv_wc> entries;

  std::vector<int> ids;
  int max_id;
  int my_id;

  uint64_t *counter_from;
};
}  // namespace dory

namespace dory {
class LeaderPermissionAsker {
 public:
  LeaderPermissionAsker() {}
  LeaderPermissionAsker(LeaderContext *ctx)
      : ctx{ctx},
        c_ctx{&ctx->cc},
        scratchpad{&ctx->scratchpad},
        req_nr(c_ctx->my_id),
        grant_req_id{1} {
    auto quorum_size = c_ctx->remote_ids.size();
    modulo = Identifiers::maxID(c_ctx->my_id, c_ctx->remote_ids);

    // TODO:
    // We assume that these writes can never fail
    SequentialQuorumWaiter waiterLeaderWrite(quorum::LeaderReqWr,
                                             c_ctx->remote_ids, quorum_size, 1);
    leaderWriter =
        MajorityWriter(c_ctx, waiterLeaderWrite, c_ctx->remote_ids, 0);

    auto remote_slot_offset =
        scratchpad->writeLeaderChangeSlotsOffsets()[c_ctx->my_id];
    remote_mem_locations.resize(Identifiers::maxID(c_ctx->remote_ids) + 1);
    std::fill(remote_mem_locations.begin(), remote_mem_locations.end(),
              remote_slot_offset);
  }

  void startPoller() {
    ctx->poller.registerContext(quorum::LeaderReqWr);
    ctx->poller.registerContext(quorum::LeaderGrantWr);
    ctx->poller.endRegistrations(3);

    ask_perm_poller = ctx->poller.getContext(quorum::LeaderReqWr);
    give_perm_poller = ctx->poller.getContext(quorum::LeaderGrantWr);
  }

  // TODO: Refactor
  std::unique_ptr<MaybeError> givePermission(int pid, uint64_t response) {
    auto &offsets = scratchpad->readLeaderChangeSlotsOffsets();
    auto offset = offsets[c_ctx->my_id];

    auto &rcs = c_ctx->ce.connections();
    auto rc_it = rcs.find(pid);
    if (rc_it == rcs.end()) {
      throw std::runtime_error("Bug: connection does not exist");
    }

    uint64_t *temp =
        reinterpret_cast<uint64_t *>(scratchpad->leaderResponseSlot());
    *temp = response;

    auto &rc = rc_it->second;
    rc.postSendSingle(ReliableConnection::RdmaWrite,
                      quorum::pack(quorum::LeaderGrantWr, pid, grant_req_id),
                      temp, sizeof(temp), rc.remoteBuf() + offset);

    grant_req_id += 1;

    int expected_nr = 1;

    while (true) {
      entries.resize(expected_nr);
      if (give_perm_poller(c_ctx->cq, entries)) {
      // if (c_ctx->cb.pollCqIsOK(c_ctx->cq, entries)) {
        for (auto const &entry : entries) {
          auto [reply_k, reply_pid, reply_seq] =
              quorum::unpackAll<uint64_t, uint64_t>(entry.wr_id);

          if (reply_k != quorum::LeaderGrantWr || reply_pid != uint64_t(pid) ||
              reply_seq != (grant_req_id - 1)) {
            continue;
          }

          if (entry.status != IBV_WC_SUCCESS) {
            throw std::runtime_error(
                "Unimplemented: We assume the leader election connections "
                "never fail");
          } else {
            return std::make_unique<NoError>();
          }
        }
      } else {
        std::cout << "Poll returned an error" << std::endl;
      }
    }

    return std::make_unique<NoError>();
  }

  bool waitForApproval(Leader current_leader, std::atomic<Leader> &leader) {
    auto &slots = scratchpad->readLeaderChangeSlots();
    auto ids = c_ctx->remote_ids;
    auto constexpr shift = 8 * sizeof(uintptr_t) - 1;

    // TIMESTAMP_T start, end;
    // GET_TIMESTAMP(start);
    // uint64_t sec = 1000000000UL;

    while (true) {
      int eliminated_one = -1;
      for (size_t i = 0; i < ids.size(); i++) {
        auto pid = ids[i];
        uint64_t volatile *temp = reinterpret_cast<uint64_t *>(slots[pid]);
        uint64_t val = *temp;
        val &= (1UL << shift) - 1;

        // std::cout << "(" << val << ", " << pid << ")" << std::endl;

        if (val + modulo == req_nr) {
          eliminated_one = i;
          // std::cout << "Eliminating " << pid << std::endl;
          break;
        }
      }

      if (eliminated_one >= 0) {
        ids[eliminated_one] = ids[ids.size() - 1];
        ids.pop_back();

        if (ids.empty()) {
          return true;
        }
      }

      // GET_TIMESTAMP(end);
      // if (ELAPSED_NSEC(start, end) > 5UL * sec) {
      //   std::cout << "WaitForApproval timed-out" << std::endl;
      //   return false;
      // }

      if (leader.load().requester != current_leader.requester) {
        return false;
      }
    }
  }

  std::unique_ptr<MaybeError> askForPermissions(bool hard_reset = false) {
    uint64_t *temp =
        reinterpret_cast<uint64_t *>(scratchpad->leaderRequestSlot());
    if (hard_reset) {
      *temp = (1UL << 63) | req_nr;
    } else {
      *temp = req_nr;
    }

    // std::cout << "AskForPermissions_Write" << std::endl;
    // Wait for the request to reach all followers
    auto err = leaderWriter.write(temp, sizeof(req_nr), remote_mem_locations, ask_perm_poller);
    // std::cout << "AskForPermissions_WriteDone" << std::endl;

    if (!err->ok()) {
      return err;
    }

    req_nr += modulo;

    return std::make_unique<NoError>();
  }

  inline uint64_t requestNr() const { return req_nr; }

 private:
  LeaderContext *ctx;
  ConnectionContext *c_ctx;
  ScratchpadMemory *scratchpad;
  uint64_t req_nr;
  uint64_t grant_req_id;

  using MajorityWriter = FixedSizeMajorityOperation<SequentialQuorumWaiter,
                                                    LeaderSwitchRequestError>;
  MajorityWriter leaderWriter;

  std::vector<uintptr_t> remote_mem_locations;

  int modulo;
  std::vector<struct ibv_wc> entries;
  PollingContext ask_perm_poller;
  PollingContext give_perm_poller;
};
}  // namespace dory

namespace dory {
class LeaderSwitcher {
 public:
  LeaderSwitcher() : read_slots{dummy} {}

  LeaderSwitcher(LeaderContext *ctx, LeaderHeartbeat *heartbeat)
      : ctx{ctx},
        c_ctx{&ctx->cc},
        want_leader{&heartbeat->wantLeaderSignal()},
        read_slots{ctx->scratchpad.writeLeaderChangeSlots()},
        sz{read_slots.size()},
        permission_asker{ctx} {
    prepareScanner();
  }

  void startPoller() {
    permission_asker.startPoller();
  }

  void scanPermissions() {
    // Scan the memory for new messages
    int requester = -1;
    int force_reset = 0;
    auto constexpr shift = 8 * sizeof(uintptr_t) - 1;

    for (size_t i = 0; i < sz; i++) {
      reading[i] = *reinterpret_cast<uint64_t *>(read_slots[i]);
      force_reset = reading[i] >> shift;
      reading[i] &= (1UL << shift) - 1;

      if (reading[i] > current_reading[i]) {
        current_reading[i] = reading[i];
        requester = i;
        break;
      }
    }

    // If you discovered a new request for a leader, notify the main event loop
    // to give permissions to him and switch to follower.
    if (requester > 0) {
      // std::cout << "Process with pid " << requester
      //           << " asked for permissions" << std::endl;
      leader.store(dory::Leader(requester, reading[requester], force_reset));
      want_leader->store(false);
    } else {
      // Check if my leader election declared me as leader
      if (want_leader->load()) {
        // std::cout << "Want leader" << std::endl;
        // want_leader->store(false);

        auto expected = leader.load();
        if (expected.unused()) {
          // std::cout << "I have consumed the previous leader request" <<
          // std::endl;
          // TODO: Concurrent access to requestNr
          dory::Leader desired(c_ctx->my_id, permission_asker.requestNr());
          auto ret = leader.compare_exchange_strong(expected, desired);
          if (ret) {
            // std::cout << "Process " << c_ctx->my_id << " wants to become leader"
            // << std::endl;
            want_leader->store(false);
          }
        }
      }
    }
  }

  bool checkAndApplyPermissions(
      std::map<int, ReliableConnection> *replicator_rcs,
      Follower& follower, std::atomic<bool> &leader_mode, bool &force_permission_request) {
    Leader current_leader = leader.load();
    if (current_leader != prev_leader || force_permission_request) {
      // std::cout << "Adjusting connections to leader ("
      //           << int(current_leader.requester) << " "
      //           << current_leader.requester_value << ") " << (current_leader != prev_leader) << " " << force_permission_request << std::endl;

      auto orig_leader = prev_leader;
      prev_leader = current_leader;
      bool hard_reset = force_permission_request;
      force_permission_request = false;

      if (current_leader.requester == c_ctx->my_id) {
        // std::cout << "A" << std::endl;
        if (!leader_mode.load()) {

          // std::cout << "Asking for permissions: " << hard_reset << std::endl;
          // Ask for permission. Wait for everybody to reply
          permission_asker.askForPermissions(hard_reset);

          // std::cout << "Waiting for approval" << std::endl;
          // In order to avoid a distributed deadlock (when two processes try
          // to become leaders at the same time), we bail whe the leader
          // changes.
          if (!permission_asker.waitForApproval(current_leader, leader)) {
            force_permission_request = true;
            return false;
          };

          auto expected = current_leader;
          auto desired = expected;
          desired.makeUnused();
          leader.compare_exchange_strong(expected, desired);

          // std::cout << "I (process " << c_ctx->my_id << ") got leader "
          //           << "approval" << std::endl;

          if (hard_reset) {
            // Reset everybody
            for (auto &[pid, rc] : *replicator_rcs) {
              IGNORE(pid);
              rc.reset();
            }

            // Re-configure the connections
            for (auto &[pid, rc] : *replicator_rcs) {
              IGNORE(pid);
              rc.init(ControlBlock::LOCAL_READ | ControlBlock::LOCAL_WRITE);
              rc.reconnect();
            }
          } else if (orig_leader.requester != c_ctx->my_id) {
            // If I am going from follower to leader, then I need to revoke write
            // permissions to old leader. Otherwise, I do nothing.
            auto old_leader = replicator_rcs->find(orig_leader.requester);
            if (old_leader != replicator_rcs->end()) {
              auto &rc = old_leader->second;
              auto rights = ControlBlock::LOCAL_READ | ControlBlock::LOCAL_WRITE;

              if (!rc.changeRights(rights)) {
                rc.reset();
                rc.init(rights);
                rc.reconnect();
              }
            }
          }

          // std::cout << "Blocking the follower" << std::endl;
          follower.block();
          leader_mode.store(true);
          // std::cout << "Permissions granted" << std::endl;
        } else {
          // std::cout << "C" << std::endl;
        }
      } else {
        // std::cout << "B" << std::endl;
        leader_mode.store(false);

        if (current_leader.reset()) {
          // Hard reset every connection

          // Reset everybody
          for (auto &[pid, rc] : *replicator_rcs) {
            IGNORE(pid);
            rc.reset();
          }

          // Re-configure the connections
          for (auto &[pid, rc] : *replicator_rcs) {
            if (pid == current_leader.requester) {
              rc.init(ControlBlock::LOCAL_READ | ControlBlock::LOCAL_WRITE |
                      ControlBlock::REMOTE_READ | ControlBlock::REMOTE_WRITE);
            } else {
              rc.init(ControlBlock::LOCAL_READ | ControlBlock::LOCAL_WRITE);
            }
            rc.reconnect();
          }
        } else {
          // First revoke from old leader
          auto old_leader = replicator_rcs->find(orig_leader.requester);
          if (old_leader != replicator_rcs->end()) {
            auto &rc = old_leader->second;
            auto rights = ControlBlock::LOCAL_READ | ControlBlock::LOCAL_WRITE;

            if (!rc.changeRights(rights)) {
              rc.reset();
              rc.init(rights);
              rc.reconnect();
            }
          }

          // Then grant to new leader
          auto new_leader = replicator_rcs->find(current_leader.requester);
          if (new_leader != replicator_rcs->end()) {
            auto &rc = new_leader->second;
            auto rights = ControlBlock::LOCAL_READ | ControlBlock::LOCAL_WRITE |
                          ControlBlock::REMOTE_READ | ControlBlock::REMOTE_WRITE;

            if (!rc.changeRights(rights)) {
              rc.reset();
              rc.init(rights);
              rc.reconnect();
            }
          }
        }

        follower.unblock();

        // Notify the remote party
        permission_asker.givePermission(current_leader.requester,
                                        current_leader.requester_value);
        // std::cout << "Permissions given" << std::endl;

        // std::cout << "Giving permissions to " << int(current_leader.requester)
        //           << std::endl;
        auto expected = current_leader;
        auto desired = expected;
        desired.makeUnused();
        leader.compare_exchange_strong(expected, desired);
      }

      // encountered_error = false;
    }

    return true;
  }

  std::atomic<Leader> &leaderSignal() { return leader; }

  // Move assignment operator
  LeaderSwitcher &operator=(LeaderSwitcher &&o) {
    if (&o == this) {
      return *this;
    }

    ctx = o.ctx;
    o.ctx = nullptr;
    c_ctx = o.c_ctx;
    o.c_ctx = nullptr;
    want_leader = o.want_leader;
    o.want_leader = nullptr;
    prev_leader = o.prev_leader;
    leader.store(o.leader.load());
    dummy = o.dummy;
    read_slots = o.read_slots;
    sz = o.sz;
    permission_asker = o.permission_asker;
    current_reading = o.current_reading;
    reading = o.reading;
    return *this;
  }

 private:
  void prepareScanner() {
    current_reading.resize(sz);

    auto constexpr shift = 8 * sizeof(uintptr_t) - 1;
    for (size_t i = 0; i < sz; i++) {
      current_reading[i] = *reinterpret_cast<uint64_t *>(read_slots[i]);
      current_reading[i] &= (1UL << shift) - 1;
    }

    reading.resize(sz);
  }

 private:
  LeaderContext *ctx;
  ConnectionContext *c_ctx;
  std::atomic<bool> *want_leader;
  Leader prev_leader;
  std::atomic<Leader> leader;

  std::vector<uint8_t *> dummy;
  std::vector<uint8_t *> &read_slots;
  size_t sz;

  LeaderPermissionAsker permission_asker;

  std::vector<uint64_t> current_reading;
  std::vector<uint64_t> reading;
};
}  // namespace dory

namespace dory {
class LeaderElection {
 public:
  LeaderElection(ConnectionContext &cc, ScratchpadMemory &scratchpad)
      : ctx{cc, scratchpad}, hb_started{false}, switcher_started{false} {
    startHeartbeat();
    startLeaderSwitcher();
  }

  ~LeaderElection() {
    stopLeaderSwitcher();
    stopHeartbreat();
  }

  void attachReplicatorContext(ReplicationContext *replicator_ctx) {
    auto &ref = replicator_ctx->cc.ce.connections();
    replicator_conns = &ref;
  }

  inline bool checkAndApplyConnectionPermissionsOK(
      Follower& follower, std::atomic<bool> &leader_mode, bool &force_permission_request) {
    return leader_switcher.checkAndApplyPermissions(
        replicator_conns, follower, leader_mode, force_permission_request);
  }

  inline std::atomic<Leader> &leaderSignal() {
    return leader_switcher.leaderSignal();
  }

 private:
  void startHeartbeat() {
    if (hb_started) {
      throw std::runtime_error("Already started");
    }
    hb_started = true;

    leader_heartbeat = LeaderHeartbeat(&ctx);
    std::future<void> ftr = hb_exit_signal.get_future();
    heartbeat_thd = std::thread([this, ftr = std::move(ftr)]() {
      leader_heartbeat.startPoller();

      std::string fifo("/tmp/fifo-" + std::to_string(ctx.cc.my_id));
      if (unlink(fifo.c_str())) {
        if (errno != ENOENT) {
          throw std::runtime_error("Could not delete the fifo: " +
                                  std::string(std::strerror(errno)));
        }
      }

      if (mkfifo(fifo.c_str(), 0666)) {
        throw std::runtime_error("Could not create the fifo: " +
                                std::string(std::strerror(errno)));
      }

      int fd = open(fifo.c_str(), O_RDWR);
      if (fd == -1) {
        throw std::runtime_error("Could not open the fifo: " +
                                std::string(std::strerror(errno)));
      }

      // int flags = fcntl(fd, F_GETFL, 0);
      // if (flags == -1) {
      //   throw std::runtime_error("Could not get the fifo flags: " +
      //                           std::string(std::strerror(errno)));
      // }

      // if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
      //   throw std::runtime_error("Could not set the fifo to non-blocking: " +
      //                           std::string(std::strerror(errno)));
      // }

      std::atomic<char> command{'c'}; // 'p' for pause, 'c' for continue
      char prev_command = 'c';

      auto file_watcher_thd = std::thread([&command, &fd]() {
        while (true) {
          char tmp;
          int ret = read(fd, &tmp, 1);
          if (ret == -1) {
            // if (errno != EAGAIN) {
            throw std::runtime_error("Could not read from the fifo: " +
                              std::string(std::strerror(errno)));
            // }
          } else if (ret == 1) {
            // command = tmp;
            command.store(tmp);
          }
        }
      });

      if (ConsensusConfig::pinThreads) {
        pinThreadToCore(file_watcher_thd, ConsensusConfig::fileWatcherThreadCoreID);
      }

      if (ConsensusConfig::nameThreads) {
        setThreadName(file_watcher_thd, ConsensusConfig::fileWatcherThreadName);
      }

      for (unsigned long long i = 0;; i = (i + 1) & iterations_ftr_check) {
        char current_command = command.load();
        if (current_command == 'c') {
          leader_heartbeat.scanHeartbeats();
        } else if (prev_command == 'c') {
          leader_heartbeat.retract();
        }

        prev_command = current_command;

        // std::this_thread::sleep_for(std::chrono::milliseconds(250));

        std::this_thread::sleep_for(std::chrono::microseconds(5));

        // if (i % 5 == 0) {
        //   std::this_thread::sleep_for(std::chrono::seconds(10));
        // }

        if (i == 0) {
          if (ftr.wait_for(std::chrono::seconds(0)) !=
              std::future_status::timeout) {
            break;
          }
        }
      }

      file_watcher_thd.join();
    });

    if (ConsensusConfig::pinThreads) {
      pinThreadToCore(heartbeat_thd, ConsensusConfig::heartbeatThreadCoreID);
    }

    if (ConsensusConfig::nameThreads) {
      setThreadName(heartbeat_thd, ConsensusConfig::heartbeatThreadName);
    }
  }

  void stopHeartbreat() {
    if (hb_started) {
      hb_exit_signal.set_value();
      heartbeat_thd.join();
      hb_started = false;
    }
  }

  void startLeaderSwitcher() {
    if (switcher_started) {
      throw std::runtime_error("Already started");
    }
    switcher_started = true;

    leader_switcher = LeaderSwitcher(&ctx, &leader_heartbeat);
    std::future<void> ftr = switcher_exit_signal.get_future();
    switcher_thd = std::thread([this, ftr = std::move(ftr)]() {
      leader_switcher.startPoller();
      for (unsigned long long i = 0;; i = (i + 1) & iterations_ftr_check) {
        leader_switcher.scanPermissions();
        if (i == 0) {
          if (ftr.wait_for(std::chrono::seconds(0)) !=
              std::future_status::timeout) {
            break;
          }
        }
      }
    });

    if (ConsensusConfig::pinThreads) {
      pinThreadToCore(switcher_thd, ConsensusConfig::switcherThreadCoreID);
    }

    if (ConsensusConfig::nameThreads) {
      setThreadName(switcher_thd, ConsensusConfig::switcherThreadName);
    }
  }

  void stopLeaderSwitcher() {
    if (switcher_started) {
      switcher_exit_signal.set_value();
      switcher_thd.join();
      switcher_started = false;
    }
  }

 private:
  // Must be power of 2 minus 1
  static constexpr unsigned long long iterations_ftr_check = (2 >> 13) - 1;
  LeaderContext ctx;
  std::map<int, ReliableConnection> *replicator_conns;

  // For heartbeat thread
  LeaderHeartbeat leader_heartbeat;
  std::thread heartbeat_thd;
  bool hb_started;
  std::promise<void> hb_exit_signal;

  // For the leader switcher thread
  LeaderSwitcher leader_switcher;
  std::thread switcher_thd;
  bool switcher_started;
  std::promise<void> switcher_exit_signal;
};
}  // namespace dory