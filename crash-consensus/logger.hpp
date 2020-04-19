#pragma once

#include <dory/shared/logger.hpp>
#include <dory/shared/unused-suppressor.hpp>

namespace dory {
namespace ConsensusConfig {
static const char logger_prefix[] = "CONS";
}
}  // namespace dory

#if SPDLOG_ACTIVE_LEVEL == SPDLOG_LEVEL_OFF
#define LOGGER_DECL(x) char x
#define LOGGER_INIT(name, prefix) name(prefix[0])
#else
#define LOGGER_DECL(x) dory::logger x
#define LOGGER_INIT(name, prefix) name(std_out_logger(prefix))
#endif

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
#define LOGGER_TRACE(...) SPDLOG_LEVEL_TRACE(__VA_ARGS__)
#else
#define LOGGER_TRACE(...) dory::IGNORE(__VA_ARGS__)
#endif

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_DEBUG
#define LOGGER_DEBUG(...) SPDLOG_LEVEL_DEBUG(__VA_ARGS__)
#else
#define LOGGER_DEBUG(...) dory::IGNORE(__VA_ARGS__)
#endif

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_INFO
#define LOGGER_INFO(...) SPDLOG_LEVEL_INFO(__VA_ARGS__)
#else
#define LOGGER_INFO(...) dory::IGNORE(__VA_ARGS__)
#endif

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_WARN
#define LOGGER_WARN(...) SPDLOG_LEVEL_WARN(__VA_ARGS__)
#else
#define LOGGER_WARN(...) dory::IGNORE(__VA_ARGS__)
#endif

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_ERROR
#define LOGGER_ERROR(...) SPDLOG_LEVEL_ERROR(__VA_ARGS__)
#else
#define LOGGER_ERROR(...) dory::IGNORE(__VA_ARGS__)
#endif

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_CRITICAL
#define LOGGER_CRITICAL(...) SPDLOG_LEVEL_CRITICAL(__VA_ARGS__)
#else
#define LOGGER_CRITICAL(...) dory::IGNORE(__VA_ARGS__)
#endif
