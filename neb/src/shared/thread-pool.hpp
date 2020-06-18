#pragma once

#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <vector>

#include <dory/shared/pinning.hpp>

#include <tbb/concurrent_queue.h>

/**
 * A thread pool using a concurrent queue from which workers try to pop out
 * tasks if any available. Workers loop and pop constantly without suspension.
 * */
class SpinningThreadPool {
 public:
  SpinningThreadPool(size_t, std::string);
  SpinningThreadPool(size_t, std::string, std::vector<int>);

  template <class F, class... Args>
  auto enqueue(F&& f, Args&&... args)
      -> std::future<typename std::result_of<F(Args...)>::type>;
  ~SpinningThreadPool();

 private:
  tbb::concurrent_queue<std::function<void()>> tasks;
  std::promise<void> exit_signal;
  std::vector<std::thread> workers;
};

/**
 * A thread pool using the stl queue and a condition variable to notify workers
 * upon a new task. While waiting, the worker thread is suspended.
 * */
class SuspendedThreadPool {
 public:
  SuspendedThreadPool(size_t, std::string);
  SuspendedThreadPool(size_t, std::string, std::vector<int>);

  template <class F, class... Args>
  auto enqueue(F&& f, Args&&... args)
      -> std::future<typename std::result_of<F(Args...)>::type>;
  ~SuspendedThreadPool();

 private:
  std::vector<std::thread> workers;
  std::queue<std::function<void()>> tasks;
  std::mutex queue_mutex;
  std::condition_variable condition;
  volatile bool stop;
};

inline SpinningThreadPool::SpinningThreadPool(size_t threads,
                                              std::string name) {
  auto sf = std::shared_future(exit_signal.get_future());

  for (size_t i = 0; i < threads; ++i) {
    workers.emplace_back([&, sf] {
      for (;;) {
        std::function<void()> task;

        if (tasks.try_pop(task)) task();
        if (unlikely(sf.wait_for(std::chrono::seconds(0)) ==
                     std::future_status::ready) &&
            tasks.unsafe_size() == 0)
          return;
      }
    });
    dory::set_thread_name(workers[i], (name + std::to_string(i)).c_str());
  }
}

inline SpinningThreadPool::SpinningThreadPool(size_t threads, std::string name,
                                              std::vector<int> proc_aff)
    : SpinningThreadPool(threads, name) {
  assert(proc_aff.size() == threads);

  for (size_t i = 0; i < threads; ++i) {
    dory::pinThreadToCore(workers[i], proc_aff[i]);
  }
}

template <class F, class... Args>
auto SpinningThreadPool::enqueue(F&& f, Args&&... args)
    -> std::future<typename std::result_of<F(Args...)>::type> {
  using return_type = typename std::result_of<F(Args...)>::type;

  auto task = std::make_shared<std::packaged_task<return_type()>>(
      std::bind(std::forward<F>(f), std::forward<Args>(args)...));

  std::future<return_type> res = task->get_future();
  tasks.push([task]() { (*task)(); });

  return res;
}

inline SpinningThreadPool::~SpinningThreadPool() {
  exit_signal.set_value();
  for (std::thread& worker : workers) worker.join();
}

// the constructor just launches some amount of workers
inline SuspendedThreadPool::SuspendedThreadPool(size_t threads,
                                                std::string name)
    : stop(false) {
  // When using a signle thread per pool, its better to swap the queue rather
  // than popping out only one entry, since it minimizes thread suspensions.
  if (threads == 1) {
    workers.emplace_back([&] {
      for (;;) {
        std::queue<std::function<void()>> next_tasks;

        {
          std::unique_lock<std::mutex> lock(queue_mutex);
          condition.wait(lock, [&] { return stop || !tasks.empty(); });
          if (stop && tasks.empty()) return;
          next_tasks.swap(tasks);
        }

        while (!next_tasks.empty()) {
          auto task = std::move(next_tasks.front());
          next_tasks.pop();
          task();
        }
      }
    });
    dory::set_thread_name(workers[0], (name + std::to_string(0)).c_str());
  } else {
    for (size_t i = 0; i < threads; ++i) {
      workers.emplace_back([&] {
        for (;;) {
          std::function<void()> task;

          {
            std::unique_lock<std::mutex> lock(queue_mutex);
            condition.wait(lock, [&] { return stop || !tasks.empty(); });
            if (stop && tasks.empty()) return;
            task = std::move(tasks.front());
            tasks.pop();
          }

          task();
        }
      });
      dory::set_thread_name(workers[i], (name + std::to_string(i)).c_str());
    }
  }
}

inline SuspendedThreadPool::SuspendedThreadPool(size_t threads,
                                                std::string name,
                                                std::vector<int> proc_aff)
    : SuspendedThreadPool(threads, name) {
  assert(proc_aff.size() == threads);

  for (size_t i = 0; i < threads; ++i) {
    dory::pinThreadToCore(workers[i], proc_aff[i]);
  }
}

template <class F, class... Args>
auto SuspendedThreadPool::enqueue(F&& f, Args&&... args)
    -> std::future<typename std::result_of<F(Args...)>::type> {
  using return_type = typename std::result_of<F(Args...)>::type;

  auto task = std::make_shared<std::packaged_task<return_type()>>(
      std::bind(std::forward<F>(f), std::forward<Args>(args)...));

  std::future<return_type> res = task->get_future();
  {
    std::unique_lock<std::mutex> lock(queue_mutex);

    // don't allow enqueueing after stopping the pool
    if (stop) throw std::runtime_error("enqueue on stopped ThreadPool");

    tasks.emplace([task]() { (*task)(); });
    condition.notify_one();
  }
  return res;
}

// the destructor joins all threads
inline SuspendedThreadPool::~SuspendedThreadPool() {
  {
    std::unique_lock<std::mutex> lock(queue_mutex);
    stop = true;
    condition.notify_all();
  }
  for (std::thread& worker : workers) worker.join();
}