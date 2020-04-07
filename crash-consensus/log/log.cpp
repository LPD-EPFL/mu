#include "log.hpp"

namespace dory {

static bool is_zero(uint8_t *buf, size_t size) {
  // For very small buffers, we do not optimize
  if (size < 6 * sizeof(uint64_t)) {
    for (size_t i = 0; i < size; i++) {
      if (buf[i] != 0) {
        return false;
      }
    }

    return true;
  }

  // For larger buffers, we are guaranteed to be able to find an aligned segment
  // in the provided buffer. We use memcmp for this segment.

  // Get aligned start
  uint8_t *aligned_buf =
      reinterpret_cast<uint8_t *>((uintptr_t(buf) + sizeof(uint64_t) - 1) /
                                  sizeof(uint64_t) * sizeof(uint64_t));

  size_t unaligned_start_len = uintptr_t(aligned_buf) - uintptr_t(buf);
  size_t aligned_len =
      (size - unaligned_start_len) / sizeof(uint64_t) * sizeof(uint64_t);

  // Check unaligned start
  for (size_t i = 0; i < unaligned_start_len; i++) {
    if (buf[i] != 0) {
      return false;
    }
  }

  // Check unaligned end
  for (size_t i = unaligned_start_len + aligned_len; i < size; i++) {
    if (buf[i] != 0) {
      return false;
    }
  }

  // For this to work, we need an aligned buffer of at least 2 *
  // sizeof(uint64_t) bytes
  return *(uint64_t *)aligned_buf == 0 ||
         memcmp(aligned_buf, aligned_buf + sizeof(uint64_t),
                aligned_len - sizeof(uint64_t)) == 0;
}

Log::Log(void *underlying_buf, size_t buf_len)
    : buf{reinterpret_cast<uint8_t *>(underlying_buf)}, len{buf_len} {
  static_assert(LogConfig::is_powerof2(LogConfig::Alignment),
                "should use a power of 2 as template parameter");

  auto buf_addr = reinterpret_cast<uintptr_t>(underlying_buf);

  if (!is_zero(buf, buf_len)) {
    throw std::runtime_error("Provided buffer is not zeroed out");
  }

  auto offset = LogConfig::round_up_powerof2(buf_addr) - buf_addr;
  // std::cout << "Rounding up: " << round_up_powerof2(buf_addr) << " " <<
  // buf_addr << std::endl;
  if (buf + offset > buf + len) {
    throw std::runtime_error(
        "Alignment constraint leaves no space in the buffer");
  }

  // buf and len point at the beginning of the log. buf points to the
  // LogHeader.
  buf += offset;
  len -= offset;

  header = reinterpret_cast<LogHeader *>(buf);
  header->min_proposal = 0;
  header->first_undecided_offset = 0;
  header->free_bytes = len - LogConfig::round_up_powerof2(sizeof(LogHeader));

  offsets[MinProposal] =
      std::make_pair(reinterpret_cast<uint8_t *>(&(header->min_proposal)) -
                         reinterpret_cast<uint8_t *>(underlying_buf),
                     sizeof(header->min_proposal));
  offsets[FUO] = std::make_pair(
      reinterpret_cast<uint8_t *>(&(header->first_undecided_offset)) -
          reinterpret_cast<uint8_t *>(underlying_buf),
      sizeof(header->first_undecided_offset));
  offsets[Entries] =
      std::make_pair(len - header->free_bytes, dory::constants::MAX_ENTRY_SIZE);

  header->first_undecided_offset = len - header->free_bytes;
}

Log::Entry Log::newEntry() {
  // std::cout << "Adding entry with absolute offset " << len -
  // header->free_bytes << std::endl;
  return Entry(buf + len - header->free_bytes, header->free_bytes);
}

void Log::finalizeEntry(Entry &entry) {
  auto bytes_used = entry.finalize();
  header->free_bytes -= LogConfig::round_up_powerof2(bytes_used);
}

std::vector<uint8_t> Log::dump() const {
  std::vector<uint8_t> v;

  for (size_t i = 0; i < len; i++) {
    v.push_back(buf[i]);
  }

  return v;
}
}  // namespace dory