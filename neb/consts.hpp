#pragma once

#include <dory/shared/units.hpp>

using namespace dory::units;

namespace dory {
namespace neb {
static constexpr size_t BUFFER_SIZE = 5_MiB;
static constexpr size_t MEMORY_SLOT_SIZE = 128_B;
static constexpr size_t MSG_HEADER_SIZE = 8_B;
static constexpr size_t MSG_PAYLOAD_SIZE = MEMORY_SLOT_SIZE - MSG_HEADER_SIZE;

static constexpr int DEFAULT_NUM_PROCESSES = 4;
}  // namespace neb
}  // namespace dory
