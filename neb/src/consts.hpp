#pragma once

#include <dory/crypto/sign.hpp>
#include <dory/shared/logger.hpp>
#include <dory/shared/units.hpp>

using namespace dory::units;

namespace dory {
namespace neb {

static constexpr size_t BUFFER_SIZE = 256_MiB;
static constexpr size_t MEMORY_SLOT_SIZE = 128_B;
static constexpr size_t MSG_HEADER_SIZE = 8_B;
static constexpr size_t MSG_PAYLOAD_SIZE =
    MEMORY_SLOT_SIZE - MSG_HEADER_SIZE - dory::crypto::SIGN_BYTES;
static constexpr size_t SLOT_SIGN_DATA_SIZE =
    MSG_HEADER_SIZE + MSG_PAYLOAD_SIZE;
static constexpr size_t SLOT_SIGNATURE_OFFSET = SLOT_SIGN_DATA_SIZE;

static constexpr auto LOG_LEVEL = spdlog::level::trace;

// Sets the maximum allowed currently pending slots. Those are slots which are
// replayed but not delivered yet. Setting a high value increases the throughput
// but also the latency. A low value ensures a lower latency (when combined with
// a small verify pool) while providing less throughput
static constexpr auto MAX_CONCURRENTLY_PENDING_SLOTS = 100;

// Sets the size of the verify thread-pool. A higher value increases the
// throughput, but also the latency of verifying a signle signature.
static constexpr auto VERIFY_POOL_SIZE = 10;
// Sets the size of the signing thread-pool.
static constexpr auto SIGN_POOL_SIZE = 10;

}  // namespace neb
}  // namespace dory
