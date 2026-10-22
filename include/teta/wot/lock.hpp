// The optional global lock: a port of teta-wot-core's `lock`.
//
// When enabled, property writes and action invocations take the lock, so
// they happen one at a time:
//
// - acquiring waits at most a short timeout (50 ms by default) and then
//   fails with GlobalLockBusy;
// - it is reentrant, but per *invocation* rather than per thread: the owner
//   is the root invocation, so an action that calls another action or writes
//   a property in-process, or a child invocation, doesn't block on the lock
//   its own invocation holds.

#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>

#include "teta/wot/uuid.hpp"

namespace teta::wot {

/// The default time to wait for the global lock.
inline constexpr std::chrono::milliseconds default_lock_timeout{50};

/// The global lock is held by someone else.
class GlobalLockBusy : public std::exception {
  public:
    const char* what() const noexcept override { return "The global lock could not be acquired."; }
};

/// A reentrant lock with an acquisition timeout, owned by invocations.
class GlobalLock : public std::enable_shared_from_this<GlobalLock> {
    struct Private {};

  public:
    /// Holds the lock until destroyed.
    class [[nodiscard]] Guard {
      public:
        Guard() = default;
        Guard(Guard&& other) noexcept : lock_(std::move(other.lock_)) {}
        Guard& operator=(Guard&& other) noexcept {
            if (this != &other) {
                release();
                lock_ = std::move(other.lock_);
            }
            return *this;
        }
        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;
        ~Guard() { release(); }

        /// Releases the lock now.
        void release() {
            if (lock_) {
                lock_->release();
                lock_.reset();
            }
        }

      private:
        friend class GlobalLock;
        explicit Guard(std::shared_ptr<GlobalLock> lock) : lock_(std::move(lock)) {}
        std::shared_ptr<GlobalLock> lock_;
    };

    /// A free lock with the given acquisition timeout.
    static std::shared_ptr<GlobalLock> create(std::chrono::nanoseconds timeout = default_lock_timeout);
    GlobalLock(Private, std::chrono::nanoseconds timeout) : timeout_(timeout) {}

    /// How long `acquire` waits.
    std::chrono::nanoseconds timeout() const { return timeout_; }

    /// The current owner, if the lock is held.
    std::optional<Uuid> owner() const;

    /// Takes the lock for `owner` if it is free or already `owner`'s; throws
    /// GlobalLockBusy otherwise.
    Guard try_acquire(const Uuid& owner);

    /// Takes the lock for `owner`, waiting up to the timeout for it to be
    /// released; throws GlobalLockBusy.
    Guard acquire(const Uuid& owner);

  private:
    bool take(const Uuid& owner);
    void release();

    std::chrono::nanoseconds timeout_;
    mutable std::mutex mutex_;
    std::condition_variable released_;
    std::optional<Uuid> owner_;
    std::size_t depth_ = 0;
};

}  // namespace teta::wot
