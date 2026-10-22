#include "teta/wot/cancel.hpp"

#include <condition_variable>
#include <map>
#include <mutex>
#include <vector>

namespace teta::wot {

struct CancelToken::State {
    std::mutex mutex;
    std::condition_variable condition;
    bool flag = false;
    std::uint64_t next_id = 1;
    std::map<std::uint64_t, std::shared_ptr<std::function<void()>>> callbacks;
};

CancelToken::CancelToken() : state_(std::make_shared<State>()) {}

void CancelToken::cancel() const {
    std::vector<std::shared_ptr<std::function<void()>>> callbacks;
    {
        std::lock_guard lock(state_->mutex);
        state_->flag = true;
        for (const auto& [id, callback] : state_->callbacks) {
            callbacks.push_back(callback);
        }
    }
    state_->condition.notify_all();
    // Called without the token's lock, so that they may use the token.
    for (const auto& callback : callbacks) {
        (*callback)();
    }
}

bool CancelToken::is_cancelled() const {
    std::lock_guard lock(state_->mutex);
    return state_->flag;
}

bool CancelToken::take() const {
    std::lock_guard lock(state_->mutex);
    const bool was = state_->flag;
    state_->flag = false;
    return was;
}

void CancelToken::check() const {
    if (take()) {
        throw Cancelled();
    }
}

Cancelled CancelToken::cancelled() const {
    std::unique_lock lock(state_->mutex);
    state_->condition.wait(lock, [&] { return state_->flag; });
    state_->flag = false;
    return Cancelled();
}

bool CancelToken::wait_for(std::chrono::nanoseconds duration) const {
    std::unique_lock lock(state_->mutex);
    const bool cancelled = state_->condition.wait_for(lock, duration, [&] { return state_->flag; });
    state_->flag = false;
    return cancelled;
}

void CancelToken::sleep(std::chrono::nanoseconds duration) const {
    if (wait_for(duration)) {
        throw Cancelled();
    }
}

CancelToken::Registration CancelToken::on_cancel(std::function<void()> callback) const {
    Registration registration;
    std::lock_guard lock(state_->mutex);
    registration.state_ = state_;
    registration.id_ = state_->next_id++;
    state_->callbacks.emplace(registration.id_, std::make_shared<std::function<void()>>(std::move(callback)));
    return registration;
}

CancelToken::Registration::~Registration() {
    if (state_) {
        std::lock_guard lock(state_->mutex);
        state_->callbacks.erase(id_);
    }
}

}  // namespace teta::wot
