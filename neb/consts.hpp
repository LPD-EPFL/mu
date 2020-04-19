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
static constexpr size_t SLOT_SIGNATURE_OFFSET =
    MEMORY_SLOT_SIZE - MSG_HEADER_SIZE - MSG_PAYLOAD_SIZE;
static constexpr size_t SLOT_SIGN_DATA_SIZE =
    MSG_HEADER_SIZE + MSG_PAYLOAD_SIZE;

static constexpr int DEFAULT_NUM_PROCESSES = 4;
static constexpr auto LOG_LEVEL = spdlog::level::debug;
}  // namespace neb
}  // namespace dory
