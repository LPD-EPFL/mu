#pragma once
#include <memory>
#include "unused-suppressor.hpp"

#ifndef SPDLOG_ACTIVE_LEVEL
# error "Please define the SPDLOG_ACTIVE_LEVEL for the conan package"
#endif

#include <spdlog/common.h>

#if SPDLOG_ACTIVE_LEVEL == SPDLOG_LEVEL_OFF
namespace spdlog {
class logger {
public:
  logger(char c): c{c} {}
private:
  char c;
};
}
#else
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#endif

namespace dory {
using logger = std::shared_ptr<spdlog::logger>;
}

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
#define LOGGER_TRACE(...) SPDLOG_LOGGER_TRACE(__VA_ARGS__)
#else
#define LOGGER_TRACE(...) dory::IGNORE(__VA_ARGS__)
#endif

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_DEBUG
#define LOGGER_DEBUG(...) SPDLOG_LOGGER_DEBUG(__VA_ARGS__)
#else
#define LOGGER_DEBUG(...) dory::IGNORE(__VA_ARGS__)
#endif

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_INFO
#define LOGGER_INFO(...) SPDLOG_LOGGER_INFO(__VA_ARGS__)
#else
#define LOGGER_INFO(...) dory::IGNORE(__VA_ARGS__)
#endif

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_WARN
#define LOGGER_WARN(...) SPDLOG_LOGGER_WARN(__VA_ARGS__)
#else
#define LOGGER_WARN(...) dory::IGNORE(__VA_ARGS__)
#endif

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_ERROR
#define LOGGER_ERROR(...) SPDLOG_LOGGER_ERROR(__VA_ARGS__)
#else
#define LOGGER_ERROR(...) dory::IGNORE(__VA_ARGS__)
#endif

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_CRITICAL
#define LOGGER_CRITICAL(...) SPDLOG_LOGGER_CRITICAL(__VA_ARGS__)
#else
#define LOGGER_CRITICAL(...) dory::IGNORE(__VA_ARGS__)
#endif

#if SPDLOG_ACTIVE_LEVEL == SPDLOG_LEVEL_OFF
#define LOGGER_INIT(name, prefix) name(std::make_shared<spdlog::logger>(prefix[0]))
#else
namespace dory {
/**
 * Default std out logger with log level set to `spdlog::level::info`.
 * @param prefix: string prefix to prepend on every log
 **/
logger std_out_logger(std::string prefix);

}  // namespace dory
#define LOGGER_INIT(name, prefix) name(std_out_logger(prefix))
#endif

#define LOGGER_DECL(x) dory::logger x