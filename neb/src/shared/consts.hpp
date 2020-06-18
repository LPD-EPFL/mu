#pragma once

#include <dory/crypto/sign/dalek.hpp>
#include <dory/shared/logger.hpp>
#include <dory/shared/units.hpp>

using namespace dory::units;

namespace dory {
namespace neb {

static constexpr size_t BUFFER_SIZE = 64_MiB;
static constexpr size_t MEMORY_SLOT_SIZE = 128_B;
static constexpr size_t MSG_HEADER_SIZE = 8_B;
static constexpr size_t SIGNATURE_CANARY_SIZE = 1_B;
static constexpr size_t SIGNATURE_CANARY_VALUE = 0xff;
static constexpr size_t MSG_PAYLOAD_SIZE =
    MEMORY_SLOT_SIZE - MSG_HEADER_SIZE - SIGNATURE_CANARY_SIZE -
    dory::crypto::dalek::SIGNATURE_LENGTH;
static constexpr size_t SLOT_SIGN_DATA_SIZE =
    MSG_HEADER_SIZE + MSG_PAYLOAD_SIZE;
static constexpr size_t SLOT_SIGNATURE_OFFSET = SLOT_SIGN_DATA_SIZE;
static constexpr size_t SIGNATURE_POST_WRITE_LEN =
    dory::crypto::dalek::SIGNATURE_LENGTH + SIGNATURE_CANARY_SIZE;
static constexpr auto LOG_LEVEL = spdlog::level::trace;

// Sets the maximum allowed currently pending slots. Those are slots which are
// replayed but not delivered yet. Setting a high value increases the throughput
// but also the latency.
static constexpr auto MAX_CONCURRENTLY_PENDING_SLOTS = 4;
// TODO(Kristian): upon remote process crash we might want to update this value
static constexpr auto MAX_CONCURRENTLY_PENDING_PER_PROCESS = 2;

// Sets the size of the verify thread-pool. A higher value increases the
// throughput, but also the latency of verifying a signle signature.
// Depends highly on which cpu the worker threads are executed, cf.
// numa & hyperthreading
static constexpr auto VERIFY_POOL_SIZE = 4;
// Sets the size of the signing thread-pool.
static constexpr auto SIGN_POOL_SIZE = 2;

}  // namespace neb
}  // namespace dory
