#include "teta/wot/pool.hpp"

namespace teta::wot {

namespace {

// The pool being destroyed by a task running on one of its own threads: that
// thread must then leave without touching the pool.
thread_local const WorkerPool* destroyed_from_task = nullptr;

}  // namespace

WorkerPool::WorkerPool(std::size_t max_threads) : max_threads_(max_threads == 0 ? 1 : max_threads) {}

WorkerPool::~WorkerPool() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        tasks_.clear();
    }
    available_.notify_all();
    for (auto& thread : threads_) {
        // The last owner may be a task on this pool: it can't wait for itself.
        if (thread.get_id() == std::this_thread::get_id()) {
            destroyed_from_task = this;
            thread.detach();
        } else if (thread.joinable()) {
            thread.join();
        }
    }
}

void WorkerPool::submit(std::function<void()> task) {
    {
        std::lock_guard lock(mutex_);
        if (stopping_) {
            return;
        }
        tasks_.push_back(std::move(task));
        if (idle_ < tasks_.size() && threads_.size() < max_threads_) {
            threads_.emplace_back([this] { work(); });
        }
    }
    available_.notify_one();
}

std::size_t WorkerPool::size() const {
    std::lock_guard lock(mutex_);
    return threads_.size();
}

void WorkerPool::work() {
    std::unique_lock lock(mutex_);
    while (true) {
        ++idle_;
        available_.wait(lock, [&] { return stopping_ || !tasks_.empty(); });
        --idle_;
        if (stopping_) {
            return;
        }
        auto task = std::move(tasks_.front());
        tasks_.pop_front();
        lock.unlock();
        task();
        // Destroy what the task holds before waiting again.
        task = nullptr;
        if (destroyed_from_task == this) {
            destroyed_from_task = nullptr;
            return;
        }
        lock.lock();
    }
}

std::shared_ptr<WorkerPool> WorkerPool::shared() {
    static const std::shared_ptr<WorkerPool> pool = std::make_shared<WorkerPool>();
    return pool;
}

}  // namespace teta::wot
