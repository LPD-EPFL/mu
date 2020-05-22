#pragma once

#include <thread>
#include <atomic>
#include <mutex>
#include <stdexcept>

#include "context.hpp"
#include "log.hpp"
#include "config.hpp"
#include "log-recycling.hpp"

namespace dory {
class Follower {
  public:
  Follower() {}

  Follower(ReplicationContext *ctx, LeaderContext *le_ctx, BlockingIterator *iter, LiveIterator *commit_iter, ConsensusConfig::ThreadConfig threadConfig)
    : ctx{ ctx }, le_ctx{le_ctx}, iter{ iter }, commit_iter{ commit_iter }, block_thread_req{false}, blocked_thread{false}, blocked_state{false}, threadConfig{threadConfig}
    {
    }

  void spawn() {
    follower_thd = std::thread([this]{
      run();
    });

    if (threadConfig.pinThreads) {
      pinThreadToCore(follower_thd, threadConfig.followerThreadCoreID);
    }

    if (ConsensusConfig::nameThreads) {
      setThreadName(follower_thd, ConsensusConfig::followerThreadName);
    }
  }

  void attach(std::unique_ptr<LogSlotReader> *lsreader, ScratchpadMemory *scratchpad_memory) {
    lsr = lsreader;
    scratchpad = scratchpad_memory;
  }

  void waitForPoller() {
    le_ctx->poller.registerContext(quorum::RecyclingDone);
    le_ctx->poller.endRegistrations(4);
  }

  void block() {
    if (!blocked_state) {
      block_thread_req.store(true);
      while (block_thread_req.load()) {
        ;
      }
      blocked_state = true;
    }
  }

  template<typename Func>
  void commitHandler(Func f) {
    commit = std::move(f);
  }

  void unblock() {
    if (blocked_state) {
      // if (!blocked_thread.load()) {
      //   throw std::runtime_error("Cannot unblock a non-blocked thread");
      // }
      blocked_thread.store(false);
      blocked_state = false;
    }
  }

  inline std::mutex &lock() {
    return log_mutex;
  }

  // Move assignment operator
  Follower &operator=(Follower &&o) {
    if (&o == this) {
      return *this;
    }

    ctx = o.ctx;
    le_ctx = o.le_ctx;
    iter = o.iter;
    commit_iter = o.commit_iter;
    block_thread_req.store(o.block_thread_req.load());
    blocked_thread.store(o.blocked_thread.load());
    blocked_state = o.blocked_state;
    threadConfig = o.threadConfig;
    return *this;
  }

  private:
  void run() {
    int loops = 0;
    constexpr unsigned mask = (1 << 14) - 1;  // Must be power of 2 minus 1

    while (true) {
      loops = (loops + 1) & mask;

      if (loops == 0) {
        if (block_thread_req.load()) {
          blocked_thread.store(true);
          block_thread_req.store(false);
          log_mutex.unlock();

          while (blocked_thread.load()) {
            // // This is necessary to make the call to `block` idempotent
            // if (block_thread_req.load()) {
            //   block_thread_req.store(false);
            // }
          }
          log_mutex.lock();
        }
      }

      auto has_next = iter->sampleNext();
      if (!has_next) {
        continue;
      }

      ParsedSlot pslot(iter->location());
      // std::cout << "Discovered element on position " << uintptr_t(iter->location()) << std::endl;
      // std::cout << "Accepted proposal " << pslot.acceptedProposal()
      //           << std::endl;
      // std::cout << "First undecided offset " << pslot.firstUndecidedOffset()
      //           << std::endl;
      // auto [buf, len] = pslot.payload();
      // auto bbuf = reinterpret_cast<uint64_t*>(buf);
      // std::cout << "Payload (len=" << len << ") `" << *bbuf << "`" <<
      // std::endl;

      // Now that I got something, I will use the commit iterator
      auto fuo = pslot.firstUndecidedOffset();
      bool recycling_requested = false;

      if (unlikely(fuo == 0)) {
        auto [buf, len] = pslot.payload();

        if (len != sizeof(LogRecyclingRequest)) {
          throw std::runtime_error(
          "Coding bug: A fuo of 0 indicates a recycling request. The payload "
          "indicates the point up to which the follower has to commit");
        }
        recycling_req = *reinterpret_cast<LogRecyclingRequest*>(buf);
        fuo = recycling_req.commit_up_to;
        recycling_requested = true;

        // std::cout << "Fuo encoded inside the 0: " << fuo << std::endl;
      }

      // std::cout << "Commit up to " << fuo << std::endl;

      while (commit_iter->hasNext(fuo)) {
        commit_iter->next();

        ParsedSlot pslot(commit_iter->location());
        auto [buf, len] = pslot.payload();
        // std::cout << "Committing element on position " << uintptr_t(commit_iter->location()) << std::endl;
        // std::cout << "Accepted proposal " << pslot.acceptedProposal()
        //           << std::endl;
        // std::cout << "First undecided offset " << pslot.firstUndecidedOffset()
        //           << std::endl;
        // auto bbuf = reinterpret_cast<uint64_t*>(buf);
        // std::cout << "Payload (len=" << len << ") `" << *bbuf << "`" <<
        // std::endl;
        // std::cout << std::endl;

        commit(false, buf, len);

        // Bookkeeping
        ctx->log.updateHeaderFirstUndecidedOffset(fuo);
      }

      if (unlikely(recycling_requested)) {
        // std::cout << "Resetting" << std::endl;
        ctx->log.resetFUO();
        ctx->log.rebuildLog();

        *iter =  ctx->log.blockingIterator();
        *commit_iter = ctx->log.liveIterator();
        *lsr = std::make_unique<LogSlotReader>(ctx, *scratchpad,
                                        ctx->log.headerFirstUndecidedOffset());
        ctx->log.bzero();

        // Notify that recycling occurred
        notifyRecyclingRequestor();
      }
    }
  }

  std::unique_ptr<MaybeError> notifyRecyclingRequestor() {
    auto &c_ctx = le_ctx->cc;
    auto &offsets = scratchpad->readLogRecyclingSlotsOffsets();
    auto offset = offsets[c_ctx.my_id];
    recycling_req_poller = le_ctx->poller.getContext(quorum::RecyclingDone);

    auto pid = recycling_req.requestor;
    auto recycling_done_id = recycling_req.request_id;

    auto &rcs = c_ctx.ce.connections();
    auto rc_it = rcs.find(pid);
    if (rc_it == rcs.end()) {
      throw std::runtime_error("Coding bug: connection does not exist");
    }

    uint64_t *temp =
        reinterpret_cast<uint64_t *>(scratchpad->logRecyclingResponseSlot());
    *temp = recycling_done_id;

    auto &rc = rc_it->second;
    rc.postSendSingle(ReliableConnection::RdmaWrite,
                      quorum::pack(quorum::RecyclingDone, pid, recycling_done_id),
                      temp, sizeof(temp), rc.remoteBuf() + offset);

    int expected_nr = 1;

    while (true) {
      entries.resize(expected_nr);
      if (recycling_req_poller(c_ctx.cq, entries)) {
        for (auto const &entry : entries) {
          auto [reply_k, reply_pid, reply_seq] =
              quorum::unpackAll<uint64_t, uint64_t>(entry.wr_id);

          if (reply_k != quorum::RecyclingDone || reply_pid != uint64_t(pid) ||
              reply_seq != recycling_done_id) {
            continue;
          }

          if (entry.status != IBV_WC_SUCCESS) {
            throw std::runtime_error(
                "Unimplemented: We don't support failures yet");
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

  private:
    ReplicationContext *ctx;
    LeaderContext * le_ctx;
    BlockingIterator *iter;
    LiveIterator *commit_iter;
    std::unique_ptr<LogSlotReader> *lsr;
    ScratchpadMemory *scratchpad;
    std::function<void(bool, uint8_t*, size_t)> commit;

    std::thread follower_thd;

    alignas(64) std::atomic<bool> block_thread_req;
    alignas(64) std::atomic<bool> blocked_thread;
    alignas(64) std::mutex log_mutex;

    bool blocked_state;
    ConsensusConfig::ThreadConfig threadConfig;
    PollingContext recycling_req_poller;
    LogRecyclingRequest recycling_req;
    std::vector<struct ibv_wc> entries;
};
}
