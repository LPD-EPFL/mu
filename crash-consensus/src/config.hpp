#pragma once

namespace dory {
namespace ConsensusConfig {

static constexpr bool nameThreads = true;
static const char handoverThreadName[] = "thd_handover";
static const char consensusThreadName[] = "thd_consensus";
static const char switcherThreadName[] = "thd_switcher";
static const char heartbeatThreadName[] = "thd_heartbeat";
static const char followerThreadName[] = "thd_follower";
static const char fileWatcherThreadName[] = "thd_filewatcher";

static constexpr int handoverThreadBankAB_ID = 0;
static constexpr int fileWatcherThreadBankAB_ID = 10;

static constexpr int consensusThreadBankA_ID = 2;
static constexpr int consensusThreadBankB_ID = 12;

static constexpr int switcherThreadBankA_ID = 4;
static constexpr int switcherThreadBankB_ID = 14;

static constexpr int heartbeatThreadBankA_ID = 6;
static constexpr int heartbeatThreadBankB_ID = 16;

static constexpr int followerThreadBankA_ID = 8;
static constexpr int followerThreadBankB_ID = 18;



struct ThreadConfig {
  ThreadConfig()
      : pinThreads{true},
        handoverThreadCoreID{handoverThreadBankAB_ID},
        consensusThreadCoreID{consensusThreadBankA_ID},
        switcherThreadCoreID{switcherThreadBankA_ID},
        heartbeatThreadCoreID{heartbeatThreadBankA_ID},
        followerThreadCoreID{followerThreadBankA_ID},
        fileWatcherThreadCoreID{fileWatcherThreadBankAB_ID} {}

  bool pinThreads;

  // If pinThreads is true, make sure to set the core id appropriately.
  // You can use `numatcl -H` to see which cores each numa domain has.
  // In hyperthreaded CPUs, numactl first lists the non-hyperthreaded cores
  // and then the hyperthreaded ones.

  int handoverThreadCoreID;
  int consensusThreadCoreID;
  int switcherThreadCoreID;
  int heartbeatThreadCoreID;
  int followerThreadCoreID;
  int fileWatcherThreadCoreID;
};

}  // namespace ConsensusConfig
}  // namespace dory
