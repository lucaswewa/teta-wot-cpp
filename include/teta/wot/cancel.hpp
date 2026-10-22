// Cooperative cancellation: a port of teta-wot-core's `cancel`.
//
// Every invocation has a CancelToken. Cancelling an invocation (for example
// with `DELETE /action_invocations/{id}`) sets the token; action code notices
// at its next `check`, `sleep` or `cancelled`.
//
// Observing a cancellation *consumes* it, as LabThings' `raise_if_set` does:
// the flag is cleared and Cancelled is thrown (or returned). An action that
// handles Cancelled can therefore carry on, and a later cancellation is seen
// again. Nothing is ever interrupted by force.

#pragma once

#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>

namespace teta::wot {

/// The invocation was cancelled. Throw it from an action (or let it
/// propagate) to end the invocation with the status `cancelled`.
class Cancelled : public std::exception {
  public:
    const char* what() const noexcept override { return "The action was cancelled."; }
};

/// A resettable cancellation flag, shared by its copies, usable from any
/// thread.
class CancelToken {
    struct State;

  public:
    /// A new token that isn't cancelled.
    CancelToken();

    /// Requests cancellation, and wakes everything waiting on the token.
    void cancel() const;

    /// Whether cancellation has been requested and not yet observed. This
    /// doesn't consume it.
    bool is_cancelled() const;

    /// Consumes a pending cancellation: throws Cancelled, clearing the flag,
    /// if one was requested.
    void check() const;

    /// Consumes a pending cancellation and says whether there was one.
    bool take() const;

    /// Waits until cancellation is requested, then consumes it. Returns the
    /// Cancelled to throw.
    Cancelled cancelled() const;

    /// Sleeps for `duration`, unless cancellation is requested first: then
    /// it is consumed and Cancelled is thrown.
    void sleep(std::chrono::nanoseconds duration) const;

    /// Waits up to `duration` for cancellation; consumes it and returns true
    /// if it came.
    bool wait_for(std::chrono::nanoseconds duration) const;

    /// Calls `callback` (from the cancelling thread) each time cancellation
    /// is requested, until the returned registration is destroyed.
    class Registration {
      public:
        Registration() = default;
        Registration(Registration&&) noexcept = default;
        Registration& operator=(Registration&&) noexcept = default;
        ~Registration();

      private:
        friend class CancelToken;
        std::shared_ptr<CancelToken::State> state_;
        std::uint64_t id_ = 0;
    };
    [[nodiscard]] Registration on_cancel(std::function<void()> callback) const;

    /// Whether two tokens share their flag.
    bool same_as(const CancelToken& other) const { return state_ == other.state_; }

  private:
    std::shared_ptr<State> state_;
};

}  // namespace teta::wot
