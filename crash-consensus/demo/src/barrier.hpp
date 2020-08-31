#pragma once

#include <thread>
#include <cassert>
#include <condition_variable>

class Barrier {
 public:
  Barrier(std::size_t nb_threads)
      : m_mutex(), m_condition(), m_nb_threads(nb_threads) {
    assert(0u != m_nb_threads);
  }

  Barrier(const Barrier& barrier) = delete;

  Barrier(Barrier&& barrier) = delete;

  ~Barrier() noexcept { assert(0u == m_nb_threads); }

  Barrier& operator=(const Barrier& barrier) = delete;

  Barrier& operator=(Barrier&& barrier) = delete;

  void Wait() {
    std::unique_lock<std::mutex> lock(m_mutex);

    assert(0u != m_nb_threads);

    if (0u == --m_nb_threads) {
      m_condition.notify_all();
    } else {
      m_condition.wait(lock, [this]() { return 0u == m_nb_threads; });
    }
  }

 private:
  std::mutex m_mutex;

  std::condition_variable m_condition;

  std::size_t m_nb_threads;
};