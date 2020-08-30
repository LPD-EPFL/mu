#pragma once

#include <algorithm>
#include <cstdint>
#include <map>
#include <tuple>

namespace dory {

class Identifiers {
 public:
  static int maxID(std::vector<int> &ids) {
    auto min_max_remote = std::minmax_element(ids.begin(), ids.end());
    return *min_max_remote.second;
  }

  static int maxID(int id, std::vector<int> &ids) {
    return std::max(id, maxID(ids));
  }
};

namespace quorum {
enum Kind : uint64_t {
  ProposalRd = 0,  // Used when reading the remote proposals from the log header
  ProposalWr = 1,  // Used when writing a proposal number to the the log header
                   // of remote replicas
  FUORd = 2,
  FUODiffWr = 3,

  EntryRd = 4,
  EntryWr = 5,

  LeaderReqWr = 6,    // Used to ask for permissions from followers
  LeaderGrantWr = 7,  // Used from followers when they grant you permissions
  LeaderHeartbeat = 8,

  RecyclingDone = 9,

  MAX = 15
};

[[maybe_unused]] static const char *type_str(Kind k) {
  const std::map<Kind, const char *> MyEnumStrings{
      {Kind::ProposalRd, "Kind::ProposalRd"},
      {Kind::ProposalWr, "Kind::ProposalWr"},
      {Kind::FUORd, "Kind::FUORd"},
      {Kind::FUODiffWr, "Kind::FUODiffWr"},
      {Kind::EntryRd, "Kind::EntryRd"},
      {Kind::EntryWr, "Kind::EntryWr"},
      {Kind::LeaderReqWr, "Kind::LeaderReqWr"},
      {Kind::LeaderGrantWr, "Kind::LeaderGrantWr"},
      {Kind::LeaderHeartbeat, "Kind::LeaderHeartbeat"}};
  auto it = MyEnumStrings.find(k);
  return it == MyEnumStrings.end() ? "Out of range" : it->second;
}

static constexpr unsigned numberOfBits(uint64_t x) {
  return x < 2UL ? static_cast<int>(x) : 1 + numberOfBits(x >> 1);
}

static constexpr uint64_t numberOfOnes(int x) { return (1ULL << x) - 1; }

static constexpr int kind_size = numberOfBits(MAX);
static constexpr int pid_size = sizeof(uint16_t) * 8 - kind_size;
static constexpr int kind_shift = sizeof(Kind) * 8 - kind_size;
static constexpr int pid_shift = kind_shift - pid_size;
static constexpr int seq_erase_shift = kind_size + pid_size;
static constexpr uint64_t pid_mask = numberOfOnes(pid_size) << pid_shift;
static constexpr uint64_t seq_mask =
    numberOfOnes(sizeof(uint64_t) * 8 - kind_size - pid_size);

template <typename T, typename U>
inline constexpr uint64_t pack(Kind k, T pid, U seq) {
  return (k << kind_shift) | (static_cast<uint64_t>(pid) << pid_shift) | seq;
}

template <typename T>
inline constexpr T unpackPID(uint64_t packed) {
  return (packed & pid_mask) >> pid_shift;
}

inline constexpr Kind unpackKind(uint64_t packed) {
  return static_cast<Kind>(packed >> (kind_shift));
}

template <typename T>
inline constexpr T unpackSEQ(uint64_t packed) {
  return (packed & seq_mask);
}

template <typename T, typename U>
inline std::tuple<Kind, T, U> unpackAll(uint64_t packed) {
  return std::make_tuple(unpackKind(packed), unpackPID<T>(packed),
                         unpackSEQ<U>(packed));
}

template <typename T>
inline constexpr T majority(T quorum_size) {
  return (quorum_size + 1) / 2;
}

template <typename T>
inline constexpr T minority(T quorum_size) {
  return (quorum_size + 1) / 2 - 1;
}

}  // namespace quorum
}  // namespace dory