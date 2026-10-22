#include "teta/wot/lock.hpp"

namespace teta::wot {

std::shared_ptr<GlobalLock> GlobalLock::create(std::chrono::nanoseconds timeout) {
    return std::make_shared<GlobalLock>(Private{}, timeout);
}

std::optional<Uuid> GlobalLock::owner() const {
    std::lock_guard lock(mutex_);
    return owner_;
}

bool GlobalLock::take(const Uuid& owner) {
    if (!owner_) {
        owner_ = owner;
        depth_ = 1;
        return true;
    }
    if (*owner_ == owner) {
        ++depth_;
        return true;
    }
    return false;
}

GlobalLock::Guard GlobalLock::try_acquire(const Uuid& owner) {
    std::lock_guard lock(mutex_);
    if (!take(owner)) {
        throw GlobalLockBusy();
    }
    return Guard(shared_from_this());
}

GlobalLock::Guard GlobalLock::acquire(const Uuid& owner) {
    std::unique_lock lock(mutex_);
    const auto deadline = std::chrono::steady_clock::now() + timeout_;
    // The predicate takes the lock when it can, so it is true once taken.
    if (!released_.wait_until(lock, deadline, [&] { return take(owner); })) {
        throw GlobalLockBusy();
    }
    return Guard(shared_from_this());
}

void GlobalLock::release() {
    {
        std::lock_guard lock(mutex_);
        if (!owner_) {
            return;
        }
        if (--depth_ == 0) {
            owner_.reset();
        } else {
            return;
        }
    }
    released_.notify_all();
}

}  // namespace teta::wot
