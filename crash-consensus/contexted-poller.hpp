#pragma once

#include <map>
#include <set>
#include <stdexcept>
#include <vector>

#include <dory/ctrl/block.hpp>
#include "readerwriterqueue.h"

namespace dory {

class GenericContext {
 public:
  using ContextKind = int;
};

struct WC {
  uint64_t wr_id;             // ID of the completed Work Request (WR)
  enum ibv_wc_status status;  // Status of the operation

  WC() {}
  WC(struct ibv_wc &wc) : wr_id{wc.wr_id}, status{wc.status} {}
};

struct PollingContext : public GenericContext {
  PollingContext() {}

  PollingContext(ConnectionContext *cc, ContextKind context_kind,
                 std::vector<moodycamel::ReaderWriterQueue<WC> *> &from,
                 std::map<ContextKind, moodycamel::ReaderWriterQueue<WC> *> &to)
      : cc{cc}, context_kind{context_kind}, from{from}, to{to} {}

  bool operator() (deleted_unique_ptr<struct ibv_cq> &, std::vector<struct ibv_wc> &entries) {
    int num_requested = entries.size();
    int index = 0;

    // Go over all the queues and try to fulfill the request
    for (auto queue : from) {
      WC returned;

      while (num_requested > 0 && queue->try_dequeue(returned)) {
        entries[index].wr_id = returned.wr_id;
        entries[index].status = returned.status;
        index += 1;
        num_requested -= 1;
      }

      if (num_requested == 0) {
        return true;
      }
    }

    // Poll the rest and distribute if necessary
    auto cq = cc->cq.get();
    int num = ibv_poll_cq(cq, num_requested, &entries[index]);

    if (num >= 0) {
      int frozen_index = index;
      // The ones that are not ours, put them in their respective queues
      for (int i = frozen_index; i < frozen_index + num; i++) {
        auto &entry = entries[i];
        int kind = quorum::unpackKind(entry.wr_id);

        if (kind == context_kind) {
          entries[index] = entry;
          index++;
        } else {
          auto queue = to.find(kind);
          if (queue == to.end()) {
            throw std::runtime_error("No queue exists with kind " + kind);
          } else {
            if (!queue->second->try_enqueue(WC{entry})) {
              // throw std::runtime_error("Queue overflowed");
              return false;
            }
          }
        }
      }
    }

    entries.erase(entries.begin() + index, entries.end());

    return true;
  }

 private:
  ConnectionContext *cc;
  ContextKind context_kind;
  std::vector<moodycamel::ReaderWriterQueue<WC> *> from;
  std::map<ContextKind, moodycamel::ReaderWriterQueue<WC> *> to;
};

class ContextedPoller : public GenericContext {
 public:
  ContextedPoller(ConnectionContext *cc) : cc{cc}, done{false} {}

  void registerContext(ContextKind context_kind) {
    const std::lock_guard<std::mutex> lock(contexts_mutex);
    if (contexts.find(context_kind) != contexts.end()) {
      throw std::runtime_error("Already registered polling context with id " +
                               std::to_string(context_kind));
    }

    contexts.insert(context_kind);
  }

  void endRegistrations(size_t expected_nr_contexts) {
    while (true) {
      const std::lock_guard<std::mutex> lock(contexts_mutex);
      if (contexts.size() == expected_nr_contexts) {
        break;
      }
    }

    const std::lock_guard<std::mutex> lock(contexts_mutex);
    if (done.load()) {
      return;
    }

    done.store(true);

    // Create all to all queues
    for (auto cid_from : contexts) {
      for (auto cid_to : contexts) {
        if (cid_from == cid_to) {
          continue;
        }
        queues.insert(
            std::make_pair(std::make_pair(cid_from, cid_to),
                           moodycamel::ReaderWriterQueue<WC>(QueueDepth)));
      }
    }
  }

  PollingContext getContext(ContextKind context_kind) {
    if (!done.load()) {
      throw std::runtime_error("ContextedPoller is not finalized");
    }

    std::map<ContextKind, moodycamel::ReaderWriterQueue<WC> *> to_mapping;
    std::vector<moodycamel::ReaderWriterQueue<WC> *> from_list;
    // Create a compact map
    for (auto &[cid_from_to, queue] : queues) {
      auto from = cid_from_to.first;
      auto to = cid_from_to.second;
      if (to == context_kind) {
        from_list.push_back(&queue);
      }

      if (from == context_kind) {
        to_mapping.insert(std::make_pair(to, &queue));
      }
    }

    return PollingContext(cc, context_kind, from_list, to_mapping);
  }

 private:
  ConnectionContext *cc;
  std::map<std::pair<ContextKind, ContextKind>,
           moodycamel::ReaderWriterQueue<WC>>
      queues;  // (from, to) -> queue
  std::set<ContextKind> contexts;
  std::mutex contexts_mutex;
  std::atomic<bool> done;

  static constexpr int QueueDepth = 1024;
};

}  // namespace dory