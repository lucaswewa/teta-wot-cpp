// The worker pool that runs Thing code (ADR-0005).
//
// Invocations, child invocations and (from Phase 4) the requests that run a
// getter or a setter are tasks on a pool of threads. The pool grows when a
// task arrives and no thread is idle, up to a high cap, so an action waiting
// on a nested in-process call never starves the others; threads stay for
// later tasks.

#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace teta::wot {

class WorkerPool {
  public:
    /// The default cap on threads.
    static constexpr std::size_t default_max_threads = 1024;

    explicit WorkerPool(std::size_t max_threads = default_max_threads);
    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;
    /// Waits for the running tasks; tasks not yet started are dropped.
    ~WorkerPool();

    /// Runs `task` on a pool thread. A task must not throw.
    void submit(std::function<void()> task);

    /// The number of threads.
    std::size_t size() const;

    /// A pool for code that runs outside any runtime (tests, detached
    /// contexts), created on first use.
    static std::shared_ptr<WorkerPool> shared();

  private:
    void work();

    std::size_t max_threads_;
    mutable std::mutex mutex_;
    std::condition_variable available_;
    std::deque<std::function<void()>> tasks_;
    std::vector<std::thread> threads_;
    std::size_t idle_ = 0;
    bool stopping_ = false;
};

}  // namespace teta::wot
