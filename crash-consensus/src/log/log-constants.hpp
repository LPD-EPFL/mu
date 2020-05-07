#pragma once

#include <dory/shared/units.hpp>

namespace dory {
namespace constants {
using dory::units::operator""_MiB;
static constexpr size_t MAX_ENTRY_SIZE = 1_MiB;
static constexpr size_t CRITICAL_LOG_FREE_SPACE = 3 * MAX_ENTRY_SIZE;
}  // namespace constants
}  // namespace dory