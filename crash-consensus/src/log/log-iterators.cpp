#include "log-iterators.hpp"
#include "log-config.hpp"

namespace dory {
SnapshotIterator::SnapshotIterator(uint8_t* entry_ptr, size_t length)
    : entry_ptr{entry_ptr}, end_ptr{entry_ptr + length} {}

SnapshotIterator& SnapshotIterator::next() {
  auto length = *reinterpret_cast<uint64_t*>(entry_ptr);
  auto canary = entry_ptr + length + sizeof(uint64_t);

  if (*canary != 0xff) {
    throw std::runtime_error("Missing canary value on the snapshot iterator");
  }

  entry_ptr += LogConfig::round_up_powerof2(canary + 1 - entry_ptr);

  return *this;
}
}  // namespace dory

namespace dory {
BlockingIterator::BlockingIterator(uint8_t* entry_ptr)
    : entry_ptr{entry_ptr}, increment{0} {}

BlockingIterator& BlockingIterator::next() {
  entry_ptr += increment;

  volatile auto length_ptr = reinterpret_cast<uint64_t*>(entry_ptr);
  while (*length_ptr == 0) {
    ;
  }

  volatile auto canary_ptr = entry_ptr + *length_ptr + sizeof(uint64_t);
  while (*canary_ptr == 0) {
    ;
  }

  increment = LogConfig::round_up_powerof2(canary_ptr + 1 - entry_ptr);
  return *this;
}

bool BlockingIterator::sampleNext() {
  auto tmp_entry_ptr = entry_ptr + increment;

  volatile auto length_ptr = reinterpret_cast<uint64_t*>(tmp_entry_ptr);
  if (*length_ptr == 0) {
    return false;
  }

  volatile auto canary_ptr = tmp_entry_ptr + *length_ptr + sizeof(uint64_t);
  if (*canary_ptr == 0) {
    return false;
  }

  entry_ptr = tmp_entry_ptr;
  increment = LogConfig::round_up_powerof2(canary_ptr + 1 - entry_ptr);
  return true;
}
}  // namespace dory

namespace dory {
LiveIterator::LiveIterator(uint8_t* base_ptr, uint8_t* entry_ptr)
    : base_ptr{base_ptr}, entry_ptr{entry_ptr}, increment{0} {}

bool LiveIterator::hasNext(ptrdiff_t limit) {
  entry_ptr += increment;
  increment = 0;
  return entry_ptr < base_ptr + limit;
}

LiveIterator& LiveIterator::next(bool check) {
  entry_ptr += increment;

  volatile auto length_ptr = reinterpret_cast<uint64_t*>(entry_ptr);
  if (check) {
    while (*length_ptr == 0) {
      ;
    }
  }

  volatile auto canary_ptr = entry_ptr + *length_ptr + sizeof(uint64_t);
  if (check) {
    while (*canary_ptr == 0) {
      ;
    }
  }

  increment = LogConfig::round_up_powerof2(canary_ptr + 1 - entry_ptr);
  return *this;
}
}  // namespace dory

namespace dory {
RemoteIterator::RemoteIterator(size_t entry_header_size)
    : predictor{entry_header_size} {}

RemoteIterator::RemoteIterator(int remote_id, size_t remote_offset,
                               size_t entry_header_size)
    : remote_id{remote_id},
      remote_offset{remote_offset},
      prev_remote_offset{remote_offset},
      predictor{entry_header_size} {}

std::pair<ptrdiff_t, size_t> RemoteIterator::lookAt(uint64_t rem_offset) {
  if (rem_offset != remote_offset) {
    prev_remote_offset = remote_offset;
  }

  remote_offset = rem_offset;
  remote_size = predictor.predict();
  return std::make_pair(remote_offset, remote_size);
}

bool RemoteIterator::isPopulated(uint8_t* data, size_t length) {
  if (length < sizeof(uint64_t)) {
    throw std::runtime_error(
        "First make sure that you `canMove`, before calling "
        "`isEntryComplete`.");
  }

  auto length_ptr = reinterpret_cast<uint64_t*>(data);
  return *length_ptr != 0;
}

bool RemoteIterator::canMove(uint8_t* data, size_t length) {
  auto length_ptr = reinterpret_cast<uint64_t*>(data);

  // std::cout << "Length entry " << *length_ptr << std::endl;

  if (*length_ptr + 1 + sizeof(uint64_t) <= length) {
    // std::cout << "True: Adjusting the predictor to " << *length_ptr + 1 +
    // sizeof(uint64_t) << std::endl;
    predictor.adjust(*length_ptr + 1 + sizeof(uint64_t));
    return true;
  }

  // std::cout << "False: Adjusting the predictor to " << *length_ptr + 1 +
  // sizeof(uint64_t) << std::endl;
  predictor.adjust(*length_ptr + 1 + sizeof(uint64_t));
  return false;
}

bool RemoteIterator::isEntryComplete(uint8_t* data, size_t length) {
  auto length_ptr = reinterpret_cast<uint64_t*>(data);

  if (*length_ptr + 1 + sizeof(uint64_t) > length) {
    throw std::runtime_error(
        "First make sure that you `canMove`, before calling "
        "`isEntryComplete`.");
  }

  auto canary_ptr = data + *length_ptr + sizeof(uint64_t);
  return *canary_ptr != 0;
}
}  // namespace dory