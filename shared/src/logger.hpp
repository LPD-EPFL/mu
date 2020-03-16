#pragma once

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace dory {

static constexpr auto FORMAT_STR_DEFAULT = "[%n:%^%l%$] %v";
static constexpr auto FORMAT_STR_WITH_SOURCE = "[%n:%^%l%$:%@] %v";

using logger = std::shared_ptr<spdlog::logger>;

/**
 * Default std out logger with log level set to `spdlog::level::debug`.
 * @param prefix: string prefix to prepend on every log
 **/
logger std_out_logger(std::string prefix);
}  // namespace dory