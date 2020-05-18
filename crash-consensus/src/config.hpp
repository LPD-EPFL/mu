#pragma once

namespace dory {
namespace ConsensusConfig {
static constexpr bool pinThreads = true;

// If pinThreads is true, make sure to set the core id appropriately.
// You can use `numatcl -H` to see which cores each numa domain has.
// In hyperthreaded CPUs, numactl first lists the non-hyperthreaded cores
// and then the hyperthreaded ones.

static constexpr int handoverThreadCoreID = 0;
static constexpr int consensusThreadCoreID = 2;
static constexpr int switcherThreadCoreID = 4;
static constexpr int heartbeatThreadCoreID = 6;
static constexpr int followerThreadCoreID = 8;
static constexpr int fileWatcherThreadCoreID = 10;

static constexpr bool nameThreads = true;
static const char handoverThreadName[] = "thd_handover";
static const char consensusThreadName[] = "thd_consensus";
static const char switcherThreadName[] = "thd_switcher";
static const char heartbeatThreadName[] = "thd_heartbeat";
static const char followerThreadName[] = "thd_follower";
static const char fileWatcherThreadName[] = "thd_filewatcher";

}  // namespace ConsensusConfig
}  // namespace dory