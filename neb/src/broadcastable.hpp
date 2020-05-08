#pragma once

#include <cstddef>

namespace dory {
namespace neb {
/**
 * Required interface to implement for messages to get broadcasted by this
 * module
 **/
class Broadcastable {
 public:
  /**
   * Marshalls the contents of a message into the provided buffer. Should
   * not exceed the `dory::neb::MSG_PAYLOAD_SIZE`.
   * @param: pointer to buffer where to marshall the contents of the message
   * @return: number of written bytes
   **/
  virtual size_t marshall(volatile void *buf) const = 0;
  virtual size_t size() const = 0;
};
}  // namespace neb
}  // namespace dory