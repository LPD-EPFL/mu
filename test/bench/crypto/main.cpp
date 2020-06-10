#include <dory/crypto/sign/dalek.hpp>
#include <dory/shared/bench.hpp>
#include <dory/shared/logger.hpp>
#include <dory/shared/pointer-wrapper.hpp>
#include <dory/store.hpp>

#include <atomic>
#include <fstream>
#include <iostream>
#include <numeric>
#include <thread>

auto logger = dory::std_out_logger("MAIN");

inline void pin_thread_to_core(std::thread &thd, size_t cpu_id) {
  cpu_set_t cpuset;

  CPU_ZERO(&cpuset);
  CPU_SET(cpu_id, &cpuset);

  int rc =
      pthread_setaffinity_np(thd.native_handle(), sizeof(cpu_set_t), &cpuset);

  if (rc != 0) {
    throw std::runtime_error("Error calling pthread_setadaleknity_np: " +
                             std::string(std::strerror(errno)));
  }
}

inline void stats(size_t tid, std::vector<long long> &numbers) {
  std::vector<long long> filtered;

  const int upper_bound = 200000;

  std::cout
      << "\n================================================================"
      << "\nTHREAD NUMBER: " << tid
      << "\n================================================================"
      << std::endl;
  std::cout << "Keep only values lower than " << upper_bound << "ns"
            << std::endl;
  std::copy_if(numbers.begin(), numbers.end(), std::back_inserter(filtered),
               [&](int i) { return i < upper_bound; });

  if (filtered.size() == 0) {
    std::cout << "All samples are above the upper bound of " << upper_bound
              << std::endl;
    return;
  }

  sort(filtered.begin(), filtered.end());

  long long sum = std::accumulate(filtered.begin(), filtered.end(), 0ll);
  double mean = double(sum) / static_cast<double>(filtered.size());

  auto max_idx = filtered.size() - 1;
  auto [min_elem, max_elem] =
      std::minmax_element(filtered.begin(), filtered.end());

  std::cout << "Samples #: " << filtered.size() << std::endl;
  std::cout << "Skipped: " << numbers.size() - filtered.size() << std::endl;
  std::cout << "(Min, Max): " << *min_elem << ", " << *max_elem << std::endl;
  std::cout << "Average: " << mean << "ns" << std::endl;
  std::cout << "25th %-tile: "
            << filtered[static_cast<size_t>(
                   std::roundl(static_cast<long double>(max_idx) * 0.25))]
            << "ns" << std::endl;
  std::cout << "50th %-tile: "
            << filtered[static_cast<size_t>(
                   std::roundl(static_cast<long double>(max_idx) * 0.50))]
            << "ns" << std::endl;
  std::cout << "75th %-tile: "
            << filtered[static_cast<size_t>(
                   std::roundl(static_cast<long double>(max_idx) * 0.75))]
            << "ns" << std::endl;
  std::cout << "90th %-tile: "
            << filtered[static_cast<size_t>(
                   std::roundl(static_cast<long double>(max_idx) * 0.90))]
            << "ns" << std::endl;
  std::cout << "95th %-tile: "
            << filtered[static_cast<size_t>(
                   std::roundl(static_cast<long double>(max_idx) * 0.95))]
            << "ns" << std::endl;
  std::cout << "98th %-tile: "
            << filtered[static_cast<size_t>(
                   std::roundl(static_cast<long double>(max_idx) * 0.98))]
            << "ns" << std::endl;
  std::cout << "99th %-tile: "
            << filtered[static_cast<size_t>(
                   std::roundl(static_cast<long double>(max_idx) * 0.99))]
            << "ns" << std::endl;
}

inline void write(size_t tid, std::vector<long long> &numbers) {
  sort(numbers.begin(), numbers.end());

  auto filename =
      std::string("/tmp/verify") + std::to_string(tid) + std::string(".dat");

  std::cout << "Writing samples to" << filename << std::endl;

  std::ofstream fs;
  fs.open(filename);
  for (auto &p : numbers) fs << p << "\n";
}

/**
 * NOTE:  For this to successfully run, you need to have memcached running.
 *        Refer to the memstore package for further information.
 * */
int main() {
  logger->info("Creating and publishing key and verifying own signature");

  dory::crypto::dalek::init();

  dory::crypto::dalek::publish_pub_key("p1-pk");

  auto pk = dory::crypto::dalek::get_public_key("p1-pk");

  static constexpr unsigned long long len = 64;
  unsigned char msg[len];
  auto *raw = reinterpret_cast<unsigned char *>(
      malloc(dory::crypto::dalek::SIGNATURE_LENGTH));

  auto sig = dory::deleted_unique_ptr<unsigned char>(
      raw, [](unsigned char *sig) { free(sig); });

  msg[0] = 'a';

  dory::crypto::dalek::sign(sig.get(), msg, len);

  std::vector<size_t> thread_pins = {2, 4, 6, 8, 10};
  std::vector<std::thread> workers(thread_pins.size());
  std::atomic<size_t> done(0);
  std::atomic<size_t> printed(0);

  for (size_t j = 0; j < workers.size(); ++j) {
    workers[j] = std::thread([&, j]() {
      const size_t validations = 100000;

      std::vector<long long> numbers(validations);

      for (size_t i = 0; i < validations; i++) {
        struct timespec t1, t2;
        double elapsed;

        clock_gettime(CLOCK_MONOTONIC, &t1);
        if (!dory::crypto::dalek::verify(sig.get(), msg, len, pk)) {
          throw std::runtime_error("sig not valid");
        };
        clock_gettime(CLOCK_MONOTONIC, &t2);

        elapsed = static_cast<double>(t2.tv_nsec + t2.tv_sec * 1000000000UL -
                                      t1.tv_nsec - t1.tv_sec * 1000000000UL);

        printf("took %.0f ns for %lu\n", elapsed, j);

        numbers[i] = static_cast<long long>(elapsed);
      }

      done++;

      // wait for others to finish!
      while (done != workers.size()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }

      // please, wait until it's your turn!
      while (printed != j) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }

      stats(j, numbers);
      write(j, numbers);
      printed++;
    });

    pin_thread_to_core(workers[j], thread_pins[j]);
  }

  for (auto &w : workers) {
    w.join();
  }

  logger->info("Testing finished successfully!");

  return 0;
}
