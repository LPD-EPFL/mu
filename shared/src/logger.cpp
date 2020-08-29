// For parsing the environment variables
#include <cstdlib>
#include <sstream>
#include <string>
#include <utility>

#include <memory>

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace dory {

static constexpr auto FORMAT_STR_DEFAULT = "[%n:%^%l%$] %v";
static constexpr auto FORMAT_STR_WITH_SOURCE = "[%n:%^%l%$:%@] %v";

using logger = std::shared_ptr<spdlog::logger>;
logger std_out_logger(std::string prefix);

}  // namespace dory

namespace dory {
// Code taken from master of spdlog. Eventually, when spdlog creates an new
// release, this piece of code will no longer be necessary.

// Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

// inplace convert to lowercase
static inline std::string &to_lower_(std::string &str) {
  std::transform(str.begin(), str.end(), str.begin(), [](char ch) {
    return static_cast<char>((ch >= 'A' && ch <= 'Z') ? ch + ('a' - 'A') : ch);
  });
  return str;
}

// inplace trim spaces
static inline std::string &trim_(std::string &str) {
  const char *spaces = " \n\r\t";
  str.erase(str.find_last_not_of(spaces) + 1);
  str.erase(0, str.find_first_not_of(spaces));
  return str;
}

// return (name,value) trimmed pair from given "name=value" string.
// return empty string on missing parts
// "key=val" => ("key", "val")
// " key  =  val " => ("key", "val")
// "key=" => ("key", "")
// "val" => ("", "val")
static inline std::pair<std::string, std::string> extract_kv_(
    char sep, const std::string &str) {
  auto n = str.find(sep);
  std::string k, v;
  if (n == std::string::npos) {
    v = str;
  } else {
    k = str.substr(0, n);
    v = str.substr(n + 1);
  }
  return std::make_pair(trim_(k), trim_(v));
}

// return vector of key/value pairs from sequence of "K1=V1,K2=V2,.."
// "a=AAA,b=BBB,c=CCC,.." => {("a","AAA"),("b","BBB"),("c", "CCC"),...}
static inline std::unordered_map<std::string, std::string> extract_key_vals_(
    const std::string &str) {
  std::string token;
  std::istringstream token_stream(str);
  std::unordered_map<std::string, std::string> rv{};
  while (std::getline(token_stream, token, ',')) {
    if (token.empty()) {
      continue;
    }
    auto kv = extract_kv_('=', token);
    rv[kv.first] = kv.second;
  }
  return rv;
}

static inline void set_level(const std::string &levels,
                             const std::string &input_name, logger &lgr) {
  auto key_vals = extract_key_vals_(levels);

  auto name_level = key_vals.find(input_name);
  if (name_level != key_vals.end()) {
    auto &logger_name = name_level->first;
    auto level_name = to_lower_(name_level->second);
    auto level = spdlog::level::from_str(level_name);

    // fallback to "info" if unrecognized level name
    if (level == spdlog::level::off && level_name != "off") {
      level = spdlog::level::info;
    }
    lgr->set_level(level);
  } else {
    lgr->set_level(spdlog::level::info);
  }
}

logger std_out_logger(std::string prefix) {
  auto logger = spdlog::get(prefix);

  if (logger == nullptr) {
    logger = spdlog::stdout_color_mt(prefix);

    logger->set_pattern(FORMAT_STR_DEFAULT);

    auto env_val_raw = std::getenv("SPDLOG_LEVEL");
    std::string env_val{env_val_raw == nullptr ? "" : env_val_raw};
    set_level(env_val, prefix, logger);
  }

  return logger;
}
}  // namespace dory