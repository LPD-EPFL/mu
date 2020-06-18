#include <dory/shared/logger.hpp>
#include <dory/store.hpp>

#include "async.hpp"
#include "async/broadcast.hpp"

namespace dory {

using namespace neb;

AsyncNonEquivocatingBroadcast::AsyncNonEquivocatingBroadcast(
    int self_id, std::vector<int> proc_ids, deliver_callback deliver_cb) {
  logger = std_out_logger("NEB");

  dory::Devices d;

  auto &dev_l = d.list();
  auto &od = dev_l[0];

  dory::ResolvedPort rp(od);

  rp.bindTo(0);

  cb = std::make_unique<ControlBlock>(rp);

  std::vector<int> remote_ids;

  for (int pid : proc_ids) {
    if (pid != self_id) remote_ids.push_back(pid);
  }

  auto &store = dory::MemoryStore::getInstance();

  constexpr auto PKEY_PREFIX = "pkey-p";

  dory::crypto::dalek::init();
  dory::crypto::dalek::publish_pub_key(PKEY_PREFIX + std::to_string(self_id));

  dory::ConnectionExchanger bcast_ce(self_id, remote_ids, *cb);
  dory::ConnectionExchanger replay_ce(self_id, remote_ids, *cb);

  neb::async::NonEquivocatingBroadcast::run_default_async_control_path(
      *cb, store, bcast_ce, replay_ce, self_id, proc_ids, logger);

  impl = std::make_unique<neb::async::NonEquivocatingBroadcast>(
      self_id, proc_ids, *cb, deliver_cb);

  impl->set_connections(bcast_ce, replay_ce);
  impl->set_remote_keys(
      dory::crypto::dalek::get_public_keys(PKEY_PREFIX, remote_ids));
  impl->start();
}

AsyncNonEquivocatingBroadcast::~AsyncNonEquivocatingBroadcast() {}

void AsyncNonEquivocatingBroadcast::broadcast(uint64_t k, Broadcastable &msg) {
  return impl->broadcast(k, msg);
}
}  // namespace dory