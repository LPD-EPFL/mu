#pragma once

#include <thread>
#include <atomic>
#include <mutex>
#include <stdexcept>

#include "context.hpp"
#include "log.hpp"
#include "config.hpp"

namespace dory {
class Follower {
  public:
  Follower() {}

  Follower(ReplicationContext *ctx, BlockingIterator *iter, LiveIterator *commit_iter)
    : ctx{ ctx }, iter{ iter }, commit_iter{ commit_iter }, block_thread_req{false}, blocked_thread{false}, blocked_state{false}
    {
    }

  void spawn() {
    follower_thd = std::thread([this]{
      run();
    });

    if (ConsensusConfig::pinThreads) {
      pinThreadToCore(follower_thd, ConsensusConfig::followerThreadCoreID);
    }

    if (ConsensusConfig::nameThreads) {
      setThreadName(follower_thd, ConsensusConfig::followerThreadName);
    }
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
    iter = o.iter;
    commit_iter = o.commit_iter;
    block_thread_req.store(o.block_thread_req.load());
    blocked_thread.store(o.blocked_thread.load());
    blocked_state = o.blocked_state;
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

      // std::cout << "Accepted proposal " << pslot.acceptedProposal()
      //           << std::endl;
      // std::cout << "First undecided offset " << pslot.firstUndecidedOffset()
      //           << std::endl;
      // std::string str;
      // auto [buf, len] = pslot.payload();
      // auto bbuf = reinterpret_cast<char*>(buf);
      // str.assign(bbuf, len);
      // std::cout << "Payload (len=" << len << ") `" << str << "`" <<
      // std::endl;

      // Now that I got something, I will use the commit iterator
      auto fuo = pslot.firstUndecidedOffset();
      while (commit_iter->hasNext(fuo)) {
        commit_iter->next();

        ParsedSlot pslot(commit_iter->location());
        auto [buf, len] = pslot.payload();
        commit(buf, len);

        // Bookkeeping
        ctx->log.updateHeaderFirstUndecidedOffset(fuo);
      }
    }
  }

  private:
    ReplicationContext *ctx;
    BlockingIterator *iter;
    LiveIterator *commit_iter;
    std::function<void(uint8_t*, size_t)> commit;

    std::thread follower_thd;

    alignas(64) std::atomic<bool> block_thread_req;
    alignas(64) std::atomic<bool> blocked_thread;
    alignas(64) std::mutex log_mutex;

    bool blocked_state;
};
}