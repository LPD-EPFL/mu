#pragma once

#include <dory/crypto/sign/dalek.hpp>
#include <dory/shared/logger.hpp>
#include <dory/shared/units.hpp>

using namespace dory::units;

namespace dory {
namespace neb {

static constexpr size_t BUFFER_SIZE = 256_MiB;
static constexpr size_t MEMORY_SLOT_SIZE = 128_B;
static constexpr size_t MSG_HEADER_SIZE = 8_B;
static constexpr size_t CANARY_SIZE = 1_B;
static constexpr size_t CANARY_VALUE = 0xff;

static constexpr size_t MSG_PAYLOAD_SIZE =
    MEMORY_SLOT_SIZE - MSG_HEADER_SIZE - 2 * CANARY_SIZE -
    dory::crypto::dalek::SIGNATURE_LENGTH;

static constexpr size_t SLOT_SIGN_DATA_SIZE =
    MSG_HEADER_SIZE + MSG_PAYLOAD_SIZE;

static constexpr size_t CONTENT_POST_WRITE_LEN =
    SLOT_SIGN_DATA_SIZE + CANARY_SIZE;

static constexpr size_t SLOT_SIGNATURE_OFFSET = CONTENT_POST_WRITE_LEN;

static constexpr size_t SIGNATURE_POST_WRITE_LEN =
    dory::crypto::dalek::SIGNATURE_LENGTH + CANARY_SIZE;

static constexpr auto LOG_LEVEL = spdlog::level::trace;

// Sets the maximum allowed currently pending slots. Those are slots which are
// replayed but not delivered yet.
static constexpr auto MAX_CONCURRENTLY_PENDING_SLOTS = 100;

static constexpr auto MAX_CONCURRENTLY_PENDING_PER_PROCESS = 1;

// Sets the size of the verify thread-pool.
static constexpr auto VERIFY_POOL_SIZE = 3;
// Sets the size of the signing thread-pool.
static constexpr auto SIGN_POOL_SIZE = 1;

}  // namespace neb
}  // namespace dory
