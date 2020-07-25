#pragma once

#include <cstring>
#include <memory>
#include <queue>
#include <unordered_map>
#include <vector>

#include "consts.hpp"

/**
 * Memory slot structure with canaries:
 *
 * 0            54          62        63            127
 * +------------+-----------+---------+-------------+---------+
 * | content    | id        | cannary | signature   | canary  |
 * +------------+-----------+---------+-------------+---------+
 * | uint8_t[]  | uint64_t  | uint8_t | uint8_t[]   | uint8_t |
 * +------------+-----------+---------+-------------+---------+
 *
 * The content size is variable, and adjustable by changing the
 * `MEMORY_SLOT_SIZE`.
 **/
class MemorySlot {
 public:
  /**
   * @param start: reference to the entry
   **/
  MemorySlot(volatile const uint8_t *const buf) : buf(buf) {}

  MemorySlot &operator=(const MemorySlot &other) {
    this->buf = other.buf;

    return *this;
  }

  /**
   * @returns: the message id
   **/
  uint64_t id() const {
    return *reinterpret_cast<volatile const uint64_t *>(
        &buf[dory::neb::MSG_PAYLOAD_SIZE]);
  }

  /**
   * @returns: a pointer to the content
   **/
  volatile const uint8_t *content() const { return buf; }

  /**
   * @returns: a pointer to the signature
   **/
  volatile const uint8_t *signature() const {
    return reinterpret_cast<const volatile uint8_t *>(
        &buf[dory::neb::SLOT_SIGNATURE_OFFSET]);
  }

  void set_signature_canary() {
    *const_cast<volatile uint8_t *>(&buf[dory::neb::MEMORY_SLOT_SIZE - 1]) =
        dory::neb::CANARY_VALUE;
  }

  void set_content_canary() {
    *const_cast<volatile uint8_t *>(
        &buf[dory::neb::SLOT_SIGNATURE_OFFSET - 1]) = dory::neb::CANARY_VALUE;
  }

  bool has_content() const {
    return buf[dory::neb::SLOT_SIGNATURE_OFFSET - 1] == dory::neb::CANARY_VALUE;
  }

  /**
   * Checks the signature canary and determines if a signature is present or
   * not.
   * @returns: bool indicating if the signature is set
   **/
  bool has_signature() const {
    return buf[dory::neb::MEMORY_SLOT_SIZE - 1] == dory::neb::CANARY_VALUE;
  }

  /**
   * @returns: the address of the entry
   **/
  uintptr_t addr() const { return reinterpret_cast<uintptr_t>(buf); }

  /**
   * @param entry: where to copy the own contents
   **/
  inline void copy_to(const MemorySlot &entry) const {
    std::memcpy(reinterpret_cast<void *>(entry.addr()),
                const_cast<uint8_t const *>(buf), dory::neb::MEMORY_SLOT_SIZE);
  }

  inline void copy_signature_to(const MemorySlot &entry) const {
    std::memcpy(
        reinterpret_cast<void *>(entry.addr() +
                                 dory::neb::SLOT_SIGNATURE_OFFSET),
        const_cast<uint8_t const *>(buf) + dory::neb::SLOT_SIGNATURE_OFFSET,
        dory::neb::SIGNATURE_POST_WRITE_LEN);
  }

  bool has_same_data_content_as(const MemorySlot &entry) const {
    return has_content() && entry.has_content() &&
           (0 == std::memcmp(const_cast<uint8_t const *>(buf),
                             const_cast<uint8_t const *>(entry.buf),
                             dory::neb::SLOT_SIGN_DATA_SIZE));
  }

  bool operator==(const MemorySlot &other) const { return buf == other.buf; }

 private:
  volatile const uint8_t *buf;
};
