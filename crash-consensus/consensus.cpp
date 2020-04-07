#include "consensus.hpp"

#include <iostream>
// #include <algorithm>
// #include <functional>

#include <dory/conn/exchanger.hpp>
#include <dory/conn/rc.hpp>
#include <dory/ctrl/block.hpp>
#include <dory/ctrl/device.hpp>
#include <dory/shared/units.hpp>
#include <dory/shared/unused-suppressor.hpp>
#include <dory/store.hpp>

#include "branching.hpp"
#include "config.hpp"
#include "log.hpp"
#include "memory.hpp"
#include "pinning.hpp"
#include "response-tracker.hpp"
#include "slow-path.hpp"

#include <random>  // TODO: Remove if leader-switch is finished
#include "leader-switch.hpp"

// #include "readerwriterqueue.h"

namespace dory {
RdmaConsensus::RdmaConsensus(int my_id, std::vector<int>& remote_ids)
    : my_id{my_id}, remote_ids{remote_ids}, request{false} {
  using namespace units;

  allocated_size = 1_GiB;
  alignment = 64;

  consensus_thd = std::thread([this]() { run(); });

  if (ConsensusConfig::pinThreads) {
    pinThreadToCore(consensus_thd, ConsensusConfig::consensusThreadCoreID);
  }

  if (ConsensusConfig::nameThreads) {
    setThreadName(consensus_thd, ConsensusConfig::consensusThreadName);
  }
}

RdmaConsensus::~RdmaConsensus() { consensus_thd.join(); }

bool RdmaConsensus::propose(uint8_t* buf, size_t len) {
  handover_buf = buf;
  handover_buf_len = len;
  request.store(true, std::memory_order_release);

  while (request.load(std::memory_order_relaxed))
    ;

  return !not_leader;
}

void RdmaConsensus::run() {
  std::vector<int> ids(remote_ids);
  ids.push_back(my_id);

  // Exchange info using memcached
  auto& store = MemoryStore::getInstance();

  Devices d;
  OpenDevice od;

  // Get the last device
  {
    // TODO: The copy constructor is invoked here if we use auto and then
    // iterate on the dev_lst
    // auto dev_lst = d.list();
    for (auto& dev : d.list()) {
      od = std::move(dev);
    }
  }

  std::cout << od.name() << " " << od.dev_name() << " "
            << OpenDevice::type_str(od.node_type()) << " "
            << OpenDevice::type_str(od.transport_type()) << std::endl;

  ResolvedPort rp(od);
  auto binded = rp.bindTo(0);
  std::cout << "Binded successful? " << binded << std::endl;
  std::cout << "(port_id, port_lid) = (" << +rp.portID() << ", "
            << +rp.portLID() << ")" << std::endl;

  // Configure the control block
  ControlBlock cb(rp);
  cb.registerPD("primary");
  cb.allocateBuffer("shared-buf", allocated_size, alignment);
  cb.registerMR("shared-mr", "primary", "shared-buf",
                ControlBlock::LOCAL_READ | ControlBlock::LOCAL_WRITE |
                    ControlBlock::REMOTE_READ | ControlBlock::REMOTE_WRITE);
  cb.registerCQ("cq-replication");
  cb.registerCQ("cq-leader-election");

  ConnectionExchanger ce_replication(my_id, remote_ids, cb);
  ce_replication.configure_all("primary", "shared-mr", "cq-replication",
                               "cq-replication");
  ce_replication.announce_all(store, "qp-replication");

  ConnectionExchanger ce_leader_election(my_id, remote_ids, cb);
  ce_leader_election.configure_all("primary", "shared-mr", "cq-leader-election",
                                   "cq-leader-election");
  ce_leader_election.announce_all(store, "qp-leader-election");

  auto shared_memory_addr = reinterpret_cast<uint8_t*>(cb.mr("shared-mr").addr);

  OverlayAllocator overlay(shared_memory_addr, allocated_size);

  ScratchpadMemory scratchpad(ids, overlay, alignment);
  auto [logmem_ok, logmem, logmem_size] = overlay.allocateRemaining(alignment);

  std::cout << "Logmem status: " << logmem_ok << std::endl;
  std::cout << "Log (" << uintptr_t(logmem) << ", " << logmem_size << ")"
            << std::endl;

  auto log_offset = logmem - shared_memory_addr;

  Log replication_log(logmem, logmem_size);

  std::cout << "Waiting (10 sec) for all processes to fetch the connections"
            << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(10));

  ce_replication.connect_all(
      store, "qp-replication",
      ControlBlock::LOCAL_READ | ControlBlock::LOCAL_WRITE);

  ce_leader_election.connect_all(
      store, "qp-leader-election",
      ControlBlock::LOCAL_READ | ControlBlock::LOCAL_WRITE |
          ControlBlock::REMOTE_READ | ControlBlock::REMOTE_WRITE);

  std::cout << "Waiting (10 sec) for all processes to stabilize" << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(10));

  // Initialize the contexts
  auto& cq_leader_election = cb.cq("cq-leader-election");
  ConnectionContext le_conn_ctx(cb, ce_leader_election, cq_leader_election,
                                remote_ids, my_id);

  auto& cq_replication = cb.cq("cq-replication");
  ConnectionContext re_conn_ctx(cb, ce_replication, cq_replication, remote_ids,
                                my_id);
  ReplicationContext re_ctx(re_conn_ctx, replication_log, log_offset);

  // Initialize Leader election
  LeaderElection leader_election(le_conn_ctx, scratchpad);
  leader_election.attachReplicatorContext(&re_ctx);

  // Initialize replication
  std::atomic<bool> am_I_leader(false);
  auto quorum_size = quorum::majority(remote_ids.size() + 1) - 1;
  auto next_log_entry_offset = re_ctx.log.headerFirstUndecidedOffset();

  std::cout << "My first undecided offset is " << next_log_entry_offset
            << std::endl;

  CatchUpWithFollowers catchup(&re_ctx, scratchpad);
  LogSlotReader lsr(&re_ctx, scratchpad, next_log_entry_offset);

  SequentialQuorumWaiter sqw(quorum::EntryWr, re_ctx.cc.remote_ids, quorum_size,
                             1);
  FixedSizeMajorityOperation<SequentialQuorumWaiter, WriteLogMajorityError>
      majW(&re_ctx.cc, sqw, re_ctx.cc.remote_ids);

  std::vector<uintptr_t> to_remote_memory;
  to_remote_memory.resize(Identifiers::maxID(remote_ids) + 1);
  std::fill(to_remote_memory.begin(), to_remote_memory.end(), log_offset);

  auto dest = to_remote_memory;

  // Hanging workaround
  auto& leader = leader_election.leaderSignal();

  std::cout << "Waiting (5 sec) for all processes to establish the connections"
            << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(5));

  auto iter = re_ctx.log.blockingIterator();
  auto commit_iter = re_ctx.log.liveIterator();

  // Used by consensus
  bool encountered_error = false;
  bool became_leader = true;
  bool fast_path = false;
  uint64_t proposal_nr = 0;

  // Used by leader election and error handler
  bool force_permission_request = false;

  while (true) {
    auto apply_ok = leader_election.checkAndApplyConnectionPermissionsOK(
        am_I_leader, force_permission_request);

    if (likely(apply_ok)) {
      encountered_error = false;
    } else {
      continue;
    }

    if (unlikely(encountered_error)) {
      encountered_error = false;
      std::cout << "Request permissions again" << std::endl;
      // std::this_thread::sleep_for(std::chrono::seconds(1));
      force_permission_request = true;
      am_I_leader.store(false);
      continue;
    }

    if (am_I_leader.load()) {  // Leader (slow and fast-path)
      not_leader = false;
      if (unlikely(became_leader)) {
        // std::this_thread::sleep_for(std::chrono::seconds(3));
        fast_path = false;
        became_leader = false;
        // std::cout << "Rebuilding log" << std::endl;
        re_ctx.log.rebuildLog();
      }

      if (likely(fast_path)) {  // Fast-path
        if (request.load(std::memory_order_relaxed)) {
          Slot slot(re_ctx.log);

          // TODO: Are these values correct?
          auto local_fuo = re_ctx.log.headerFirstUndecidedOffset();
          slot.storeAcceptedProposal(proposal_nr);
          slot.storeFirstUndecidedOffset(local_fuo);
          slot.storePayload(handover_buf, handover_buf_len);

          auto [address, offset, size] = slot.location();

          auto ok =
              majW.fastWrite(address, size, to_remote_memory, offset, leader);
          if (likely(ok)) {
            // std::cout << "Processes ";
            // for (auto pid : majW.successes()) {
            //   std:: cout << pid << ", ";
            // }
            // std::cout << "are in the majority" << std::endl;

            re_ctx.log.updateHeaderFirstUndecidedOffset(
                LogConfig::round_up_powerof2(offset + size));
            auto has_next = iter.sampleNext();
            if (has_next) {
              ParsedSlot pslot(*iter);

              // std::cout << "Accepted proposal " << pslot.acceptedProposal()
              //           << std::endl;
              // std::cout << "First undecided offset " <<
              // pslot.firstUndecidedOffset()
              //           << std::endl;
              // std::string str;
              // auto [buf, len] = pslot.payload();
              // auto bbuf = reinterpret_cast<char*>(buf);
              // str.assign(bbuf, len);
              // std::cout << "Payload (len=" << len << ") `" << str << "`" <<
              // std::endl;

              // Now that I got something, I will use the commit iterator
              auto fuo = pslot.firstUndecidedOffset();
              while (commit_iter.hasNext(fuo)) {
                commit_iter.next();

                ParsedSlot pslot(*commit_iter);

                // Committing
                std::string str;
                auto [buf, len] = pslot.payload();
                auto bbuf = reinterpret_cast<char*>(buf);

                IGNORE(bbuf);
                IGNORE(len);
                // str.assign(bbuf, len);
                // std::cout << "Committing payload (len=" << len << ") `" <<
                // str << "`"
                //           << std::endl;
              }
            }

            request.store(false, std::memory_order_release);
          } else {
            std::cout
                << "Error occurred when writing the new value to a majority"
                << std::endl;
            auto err = majW.fastWriteError();
            majW.recoverFromError(err);
            encountered_error = true;
            continue;
          }
        }
      } else {  // Slow-path
        if (request.load(/*std::memory_order_acquire*/)) {
          // std::cout << "\nWorking on index " << i << std::endl;
          auto catchup_proposal_err = catchup.catchProposal(leader);
          // std::cout << "Here A" << std::endl;

          while (!catchup_proposal_err->ok()) {
            if (catchup_proposal_err->type() ==
                MaybeError::CatchProposalRetryError) {
              catchup_proposal_err = catchup.catchProposal(leader);
            } else {
              std::cout << "Error: received "
                        << MaybeError::type_str(catchup_proposal_err->type())
                        << std::endl;
              catchup.recoverFromError(catchup_proposal_err);
              encountered_error = true;
              break;
            }
          }

          if (encountered_error) {
            continue;
          }
          // std::cout << "Passed catchup.catchProposal()" << std::endl;

          auto catchup_update_proposal_err =
              catchup.updateWithCurrentProposal(leader);
          if (!catchup_update_proposal_err->ok()) {
            std::cout << "Error: received "
                      << MaybeError::type_str(
                             catchup_update_proposal_err->type())
                      << std::endl;
            catchup.recoverFromError(catchup_update_proposal_err);
            encountered_error = true;
          }

          if (encountered_error) {
            continue;
          }
          // std::cout << "Passed catchup.updateWithCurrentProposal()" <<
          // std::endl;

          proposal_nr = catchup.proposal();
          // std::cout << "I am going to attempt writes with proposal_nr=" <<
          // proposal_nr << std::endl;

          // Trying to get the freshest value from the remote logs on the same
          // IndexedIterator index
          auto local_fuo = re_ctx.log.headerFirstUndecidedOffset();
          auto local_fuo_entry = re_ctx.log.headerPtr() + local_fuo;

          // std::cout << "My loca_fuo=" << local_fuo << std::endl;

          // Find the freshest among the remote responses
          uint8_t* freshest = nullptr;
          uint64_t max_accepted_proposal = 0;

          auto resp = lsr.readSlotAt(local_fuo, leader);
          if (resp->ok()) {
            auto& successes = lsr.successes();

            ParsedSlot local_pslot(local_fuo_entry);
            if (local_pslot.isPopulated()) {
              freshest = local_fuo_entry;
              max_accepted_proposal = local_pslot.acceptedProposal();

              // std::cout << "Reading from process " << ctx.my_id << std::endl;
              ParsedSlot pslot(freshest);

              // std::cout << "Accepted proposal " << pslot.acceptedProposal()
              //           << std::endl;
              // std::cout << "First undecided offset " <<
              // pslot.firstUndecidedOffset()
              //           << std::endl;
              std::string str;
              auto [buf, len] = pslot.payload();
              auto bbuf = reinterpret_cast<char*>(buf);
              str.assign(bbuf, len);
              // std::cout << "Payload (len=" << len << ") `" << str << "`" <<
              // std::endl;
            }

            for (auto pid : successes) {
              if (pid < 0) {
                // std::cout << "Nothing to read from process " << -pid <<
                // std::endl;
              } else {
                // std::cout << "Reading from process " << pid << std::endl;
                auto store_addr = scratchpad.readLogEntrySlots()[pid];
                ParsedSlot pslot(store_addr);

                // std::cout << "Accepted proposal " << pslot.acceptedProposal()
                //           << std::endl;
                // std::cout << "First undecided offset " <<
                // pslot.firstUndecidedOffset()
                //           << std::endl;
                std::string str;
                auto [buf, len] = pslot.payload();
                auto bbuf = reinterpret_cast<char*>(buf);
                str.assign(bbuf, len);
                // std::cout << "Payload (len=" << len << ") `" << str << "`" <<
                // std::endl;

                if (max_accepted_proposal < pslot.acceptedProposal()) {
                  max_accepted_proposal = pslot.acceptedProposal();
                  freshest = store_addr;
                }
              }
            }
          } else {
            std::cout << resp->type_str(resp->type()) << std::endl;
            lsr.recoverFromError(resp);
          }

          // std::cout << "Got the freshest value" << std::endl;

          if (freshest != nullptr) {
            std::cout << "Proposing the freshest value in the slow path"
                      << std::endl;

            auto size = ParsedSlot::copy(local_fuo_entry, freshest);

            ParsedSlot fresh_pslot(freshest);
            fresh_pslot.setAcceptedProposal(proposal_nr);

            // std::cout << "Accepted proposal " <<
            // fresh_pslot.acceptedProposal()
            //           << std::endl;
            // std::cout << "First undecided offset " <<
            // fresh_pslot.firstUndecidedOffset()
            //           << std::endl;
            std::string str;
            auto [buf, len] = fresh_pslot.payload();
            auto bbuf = reinterpret_cast<char*>(buf);
            str.assign(bbuf, len);
            // std::cout << "Payload (len=" << len << ") `" << str << "`" <<
            // std::endl;

            std::transform(to_remote_memory.begin(), to_remote_memory.end(),
                           dest.begin(),
                           bind2nd(std::plus<uintptr_t>(), local_fuo));
            auto err = majW.write(local_fuo_entry, size, dest, leader);

            // for (auto d : dest) {
            //   std::cout << "Writing " << local_fuo << " to " << d <<
            //   std::endl;
            // }

            if (!err->ok()) {
              std::cout
                  << "Error occurred when writing the new value to a majority"
                  << std::endl;
              majW.recoverFromError(err);
              encountered_error = true;
              continue;
            } else {
              // std::cout << "Processes ";
              // for (auto pid : majW.successes()) {
              //   std:: cout << pid << ", ";
              // }
              // std::cout << "are in the majority" << std::endl;

              re_ctx.log.updateHeaderFirstUndecidedOffset(
                  LogConfig::round_up_powerof2(local_fuo + size));
              auto has_next = iter.sampleNext();
              if (has_next) {
                ParsedSlot pslot(*iter);

                // std::cout << "Accepted proposal " << pslot.acceptedProposal()
                //           << std::endl;
                // std::cout << "First undecided offset " <<
                // pslot.firstUndecidedOffset()
                //           << std::endl;
                // std::string str;
                // auto [buf, len] = pslot.payload();
                // auto bbuf = reinterpret_cast<char*>(buf);
                // str.assign(bbuf, len);
                // std::cout << "Payload (len=" << len << ") `" << str << "`" <<
                // std::endl;

                // Now that I got something, I will use the commit iterator
                auto fuo = pslot.firstUndecidedOffset();
                while (commit_iter.hasNext(fuo)) {
                  commit_iter.next();

                  ParsedSlot pslot(*commit_iter);

                  // Committing
                  std::string str;
                  auto [buf, len] = pslot.payload();
                  auto bbuf = reinterpret_cast<char*>(buf);
                  str.assign(bbuf, len);
                  std::cout << "Committing payload (len=" << len << ") `" << str
                            << "`" << std::endl;
                }
              }

              request.store(false /*, std::memory_order_relaxed*/);
            }

          } else {
            fast_path = true;
            std::cout << "Proposing the new value in the slow-path"
                      << std::endl;
            Slot slot(re_ctx.log);

            // TODO: Are these values correct?
            slot.storeAcceptedProposal(proposal_nr);
            slot.storeFirstUndecidedOffset(local_fuo);
            slot.storePayload(handover_buf, handover_buf_len);

            auto [address, offset, size] = slot.location();

            std::transform(to_remote_memory.begin(), to_remote_memory.end(),
                           dest.begin(),
                           bind2nd(std::plus<uintptr_t>(), offset));
            auto err = majW.write(address, size, dest, leader);

            // for (auto d : dest) {
            //   std::cout << "Writing " << offset << " to " << d << std::endl;
            // }

            if (!err->ok()) {
              std::cout
                  << "Error occurred when writing the new value to a majority"
                  << std::endl;
              majW.recoverFromError(err);
              encountered_error = true;
              continue;
            } else {
              // std::cout << "Processes ";
              // for (auto pid : majW.successes()) {
              //   std:: cout << pid << ", ";
              // }
              // std::cout << "are in the majority" << std::endl;

              re_ctx.log.updateHeaderFirstUndecidedOffset(
                  LogConfig::round_up_powerof2(offset + size));
              auto has_next = iter.sampleNext();
              if (has_next) {
                ParsedSlot pslot(*iter);

                // std::cout << "Accepted proposal " << pslot.acceptedProposal()
                //           << std::endl;
                // std::cout << "First undecided offset " <<
                // pslot.firstUndecidedOffset()
                //           << std::endl;
                // std::string str;
                // auto [buf, len] = pslot.payload();
                // auto bbuf = reinterpret_cast<char*>(buf);
                // str.assign(bbuf, len);
                // std::cout << "Payload (len=" << len << ") `" << str << "`" <<
                // std::endl;

                // Now that I got something, I will use the commit iterator
                auto fuo = pslot.firstUndecidedOffset();
                while (commit_iter.hasNext(fuo)) {
                  commit_iter.next();

                  ParsedSlot pslot(*commit_iter);

                  // Committing
                  std::string str;
                  auto [buf, len] = pslot.payload();
                  auto bbuf = reinterpret_cast<char*>(buf);
                  str.assign(bbuf, len);
                  std::cout << "Committing payload (len=" << len << ") `" << str
                            << "`" << std::endl;
                }
              }

              request.store(false /*, std::memory_order_relaxed*/);
            }
          }
        }
      }
    } else {  // Follower
      became_leader = true;

      not_leader = true;
      request.store(false /*, std::memory_order_relaxed*/);

      auto has_next = iter.sampleNext();
      if (!has_next) {
        continue;
      }

      ParsedSlot pslot(*iter);

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
      while (commit_iter.hasNext(fuo)) {
        commit_iter.next();

        ParsedSlot pslot(*commit_iter);

        // Committing
        // std::string str;
        auto [buf, len] = pslot.payload();
        // auto bbuf = reinterpret_cast<char*>(buf);
        // str.assign(bbuf, len);
        std::cout << "Committing payload (len=" << len << ") `"
                  << *reinterpret_cast<uint64_t*>(buf) << "`" << std::endl;
        // std::cout << "FUO: " << fuo << std::endl;

        // Bookkeeping
        re_ctx.log.updateHeaderFirstUndecidedOffset(fuo);
      }

      // std::cout << std::endl;
    }
  }
}
}  // namespace dory