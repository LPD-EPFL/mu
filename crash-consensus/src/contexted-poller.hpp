#pragma once

#include <map>
#include <set>
#include <stdexcept>
#include <vector>

#include <dory/ctrl/block.hpp>
#include "readerwriterqueue.h"

namespace dory {

class ConnectionContext;

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
  PollingContext();
  PollingContext(
      ConnectionContext *cc, ContextKind context_kind,
      std::vector<moodycamel::ReaderWriterQueue<WC> *> &from,
      std::map<ContextKind, moodycamel::ReaderWriterQueue<WC> *> &to);

  bool operator()(deleted_unique_ptr<struct ibv_cq> &,
                  std::vector<struct ibv_wc> &entries);

 private:
  ConnectionContext *cc;
  ContextKind context_kind;
  std::vector<moodycamel::ReaderWriterQueue<WC> *> from;
  std::map<ContextKind, moodycamel::ReaderWriterQueue<WC> *> to;
};

class ContextedPoller : public GenericContext {
 public:
  ContextedPoller(ConnectionContext *cc);

  void registerContext(ContextKind context_kind);

  void endRegistrations(size_t expected_nr_contexts);

  PollingContext getContext(ContextKind context_kind);

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