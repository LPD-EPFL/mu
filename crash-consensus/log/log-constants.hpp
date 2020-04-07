#pragma once

#include <dory/shared/units.hpp>

namespace dory {
namespace constants {
using dory::units::operator""_MiB;
static constexpr size_t MAX_ENTRY_SIZE = 4_MiB;
}  // namespace constants
}  // namespace dory