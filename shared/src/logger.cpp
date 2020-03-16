#include <memory>

#include "logger.hpp"

namespace dory {

logger std_out_logger(std::string prefix) {
  auto logger = spdlog::get(prefix);

  if (logger == nullptr) {
    logger = spdlog::stdout_color_mt(prefix);

    logger->set_pattern(FORMAT_STR_DEFAULT);
    logger->set_level(spdlog::level::debug);
  }

  return logger;
}
}  // namespace dory