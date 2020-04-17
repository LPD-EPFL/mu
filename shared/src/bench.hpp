#pragma once

#include <chrono>
#include <memory>

#include "logger.hpp"

namespace dory {
/**
 * BenchTimer represents a simple benchmarking timer for performance
 * evaluation. It may be used in two different ways described below:
 *
 * Example use with explicit stop() call:
 *
 *  BenchTimer t("foo");
 *  t.start();
 *  ...
 *  t.stop();     # this will print out the resulting time in ns
 *
 * ----------
 *
 * Example use with implicit stop() call:
 *  {
 *    BenchTimer t("foo");
 *    t.start();
 *    ...
 *    ...
 *  } # the destructor calls the stop() method, once it goes out of scope
 **/
class BenchTimer {
 public:
  BenchTimer(std::string ucase)
      : logger(std_out_logger("BENCH")), ucase(ucase) {}

  ~BenchTimer() { stop(); }

  /**
   * Sets the starting time point.
   **/
  void start() {
    SPDLOG_LOGGER_INFO(logger, "Starting benchmark for {}", ucase);
    begin = std::chrono::high_resolution_clock::now();
  }

  /**
   * Sets the end time point and prints the resulting difference to the starting
   * time point. This method will be called at latest in the destructor of this
   * class. It may be called before that explicitly.
   **/
  inline void stop() {
    if (completed) return;

    auto end = std::chrono::high_resolution_clock::now();

    auto from = std::chrono::time_point_cast<std::chrono::nanoseconds>(begin);
    auto to = std::chrono::time_point_cast<std::chrono::nanoseconds>(end);

    auto diff = to - from;
    auto ns = std::chrono::duration<long, std::nano>(diff);

    SPDLOG_LOGGER_INFO(logger, "{} took: {} ns", ucase, ns.count());

    completed = true;
  }

 private:
  std::chrono::time_point<std::chrono::high_resolution_clock> begin;
  dory::logger logger;
  std::string ucase;
  volatile bool completed = false;
};
}  // namespace dory