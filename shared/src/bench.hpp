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
 *  {
 *    BenchTimer t("foo");
 *    t.start();
 *    ...
 *    t.stop();     # this will print out the resulting time in ns
 *  }
 * ----------
 * Example use with implicit stop() call:
 *  {
 *    BenchTimer t("foo");
 *    t.start();
 *    ...
 *    ...
 *  } # the destructor calls the stop() method, once it goes out of scope
 * ----------
 * Example usage with implicit start() call:
 *  {
 *    BenchTimer t("bar", true);
 *    ...
 *    ...
 *  }
 * ----------
 **/
class BenchTimer {
 public:
  BenchTimer(std::string ucase)
      : logger(std_out_logger("BENCH")), ucase(ucase) {}

  BenchTimer(std::string ucase, bool start_flag)
      : logger(std_out_logger("BENCH")), ucase(ucase) {
    if (start_flag) {
      start();
    }
  }

  ~BenchTimer() { stop(); }

  /**
   * Sets the starting time point.
   **/
  inline void start() {
    LOGGER_INFO(logger, "Starting benchmark for {}", ucase);
    begin = std::chrono::steady_clock::now();
  }

  /**
   * Sets the end time point and prints the resulting difference to the starting
   * time point. This method will be called at latest in the destructor of this
   * class. It may be called before that explicitly.
   **/
  inline void stop() {
    if (completed) return;

    auto end = std::chrono::steady_clock::now();

    auto diff =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin);

    LOGGER_INFO(logger, "{} took: {} ns", ucase, diff.count());

    completed = true;
  }

 private:
  std::chrono::time_point<std::chrono::steady_clock> begin;
  dory::logger logger;
  std::string ucase;
  volatile bool completed = false;
};
}  // namespace dory
