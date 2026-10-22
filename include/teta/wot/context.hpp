// The invocation context: a port of teta-wot-core's `context`.
//
// Code that runs for an invocation runs inside its InvocationScope: a
// thread-local that says which invocation it is, its cancel token and its
// log. That is how `check_cancelled` and `sleep` find the token, how the
// global lock recognises its owner, and how log events reach the
// invocation's log. Actions get the same things explicitly through their
// ActionCtx. A thread an action starts carries the scope with
// `scope.run(...)`.

#pragma once

#include <chrono>
#include <condition_variable>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "teta/wot/cancel.hpp"
#include "teta/wot/lock.hpp"
#include "teta/wot/log.hpp"
#include "teta/wot/pool.hpp"
#include "teta/wot/server.hpp"
#include "teta/wot/uuid.hpp"

namespace teta::wot {

/// The identity, cancel token and log of the invocation that code runs for.
class InvocationScope {
  public:
    InvocationScope(Uuid id, Uuid lock_owner, CancelToken cancel, std::shared_ptr<LogBuffer> logs)
        : id_(id), lock_owner_(lock_owner), cancel_(std::move(cancel)), logs_(std::move(logs)) {}

    /// A scope for a new, stand-alone invocation, for running action code in
    /// tests.
    static InvocationScope fake();
    /// The scope of the code running on this thread, if any.
    static std::optional<InvocationScope> current();

    const Uuid& id() const { return id_; }
    /// Who owns the global lock on this invocation's behalf: the root
    /// invocation, so child invocations share their parent's ownership.
    const Uuid& lock_owner() const { return lock_owner_; }
    const CancelToken& cancel_token() const { return cancel_; }
    /// The invocation's log so far.
    std::vector<LogRecord> logs() const { return logs_->records(); }
    const std::shared_ptr<LogBuffer>& log_buffer() const { return logs_; }

    /// Runs `f` in this scope, on this thread, and returns what it returns.
    template <typename F>
    decltype(auto) run(F&& f) const {
        Enter enter(*this);
        return std::forward<F>(f)();
    }

    /// A scope for a child invocation: a new ID, cancel token and log, the
    /// same lock owner.
    InvocationScope child() const;

  private:
    class Enter {
      public:
        explicit Enter(const InvocationScope& scope);
        Enter(const Enter&) = delete;
        Enter& operator=(const Enter&) = delete;
        ~Enter();

      private:
        const InvocationScope* previous_;
        log::CaptureScope capture_;
    };

    Uuid id_;
    Uuid lock_owner_;
    CancelToken cancel_;
    std::shared_ptr<LogBuffer> logs_;
};

/// Sleeps for `duration`, throwing Cancelled early if the current invocation
/// is cancelled. Outside an invocation it is a plain sleep.
void cancellable_sleep(std::chrono::nanoseconds duration);

/// Throws Cancelled if the current invocation has been cancelled, consuming
/// the cancellation. Outside an invocation it does nothing.
void check_cancelled();

/// `hold_global_lock` on a server without a global lock.
class GlobalLockNotEnabled : public std::exception {
  public:
    const char* what() const noexcept override { return "The global lock is required, but is not enabled."; }
};

template <typename R>
class ChildInvocation;

/// What an action gets besides its input: its invocation's identity,
/// cancellation, log, the global lock and the server.
class ActionCtx {
  public:
    ActionCtx(InvocationScope scope, std::string thing, std::shared_ptr<GlobalLock> lock, Server server,
              std::shared_ptr<WorkerPool> pool);

    /// A context for running action code outside a runtime (tests).
    static ActionCtx detached(std::string thing = "thing");

    const Server& server() const { return server_; }
    const Uuid& invocation_id() const { return scope_.id(); }
    /// The name of the Thing the action belongs to.
    const std::string& thing_name() const { return thing_; }
    const InvocationScope& scope() const { return scope_; }
    const CancelToken& cancel_token() const { return scope_.cancel_token(); }

    /// Sleeps, throwing Cancelled early if the invocation is cancelled.
    void sleep(std::chrono::nanoseconds duration) const { scope_.cancel_token().sleep(duration); }
    /// Throws Cancelled if the invocation has been cancelled, consuming the
    /// cancellation.
    void check_cancelled() const { scope_.cancel_token().check(); }
    /// Waits until the invocation is cancelled, consuming the cancellation,
    /// and returns the Cancelled to throw.
    Cancelled cancelled() const { return scope_.cancel_token().cancelled(); }
    /// The invocation's log so far.
    std::vector<LogRecord> logs() const { return scope_.logs(); }

    /// Holds the global lock until the guard is destroyed, for actions that
    /// opted out of holding it automatically. Throws GlobalLockNotEnabled or
    /// GlobalLockBusy.
    GlobalLock::Guard hold_global_lock() const;

    /// Runs `f(ActionCtx&)` as a child invocation on another thread, with
    /// its own ID, cancel token and log; it shares this invocation's
    /// global-lock ownership.
    template <typename F>
    auto spawn_child(F f) const;

    /// Runs blocking code: a direct call (every action already runs on a
    /// worker thread), passing the cancel token. Kept for parity.
    template <typename F>
    decltype(auto) blocking(F&& f) const {
        return std::forward<F>(f)(cancel_token());
    }

  private:
    InvocationScope scope_;
    std::string thing_;
    std::shared_ptr<GlobalLock> lock_;
    Server server_;
    std::shared_ptr<WorkerPool> pool_;
};

/// A child invocation started with `ActionCtx::spawn_child`.
template <typename R>
class [[nodiscard]] ChildInvocation {
    using Stored = std::conditional_t<std::is_void_v<R>, std::monostate, R>;

  public:
    struct State {
        std::mutex mutex;
        std::condition_variable done_condition;
        bool done = false;
        std::optional<Stored> value;
        std::exception_ptr error;
    };

    ChildInvocation(InvocationScope scope, CancelToken parent, std::shared_ptr<State> state)
        : scope_(std::move(scope)), parent_(std::move(parent)), state_(std::move(state)) {}

    /// The child's invocation ID.
    const Uuid& id() const { return scope_.id(); }
    /// Asks the child to stop.
    void cancel() const { scope_.cancel_token().cancel(); }
    /// The child's log so far.
    std::vector<LogRecord> logs() const { return scope_.logs(); }

    /// Waits for the child, passing on cancellation: each time the parent is
    /// cancelled while waiting, the cancellation is consumed and the child is
    /// cancelled. If that happened at least once, Cancelled is thrown once
    /// the child has finished; otherwise the child's result is returned (or
    /// its exception rethrown).
    R join() {
        bool parent_cancelled = false;
        auto registration = parent_.on_cancel([state = state_] {
            std::lock_guard lock(state->mutex);
            state->done_condition.notify_all();
        });
        {
            std::unique_lock lock(state_->mutex);
            while (!state_->done) {
                if (parent_.take()) {
                    parent_cancelled = true;
                    scope_.cancel_token().cancel();
                    continue;
                }
                state_->done_condition.wait(lock);
            }
        }
        if (parent_cancelled) {
            throw Cancelled();
        }
        if (state_->error) {
            std::rethrow_exception(state_->error);
        }
        if constexpr (!std::is_void_v<R>) {
            return std::move(*state_->value);
        }
    }

  private:
    InvocationScope scope_;
    CancelToken parent_;
    std::shared_ptr<State> state_;
};

template <typename F>
auto ActionCtx::spawn_child(F f) const {
    using R = std::invoke_result_t<F&, ActionCtx&>;
    using Child = ChildInvocation<R>;
    auto state = std::make_shared<typename Child::State>();
    InvocationScope scope = scope_.child();
    ActionCtx child(scope, thing_, lock_, server_, pool_);
    pool_->submit([scope, child = std::move(child), f = std::move(f), state]() mutable {
        scope.run([&] {
            try {
                if constexpr (std::is_void_v<R>) {
                    f(child);
                    state->value.emplace();
                } else {
                    state->value.emplace(f(child));
                }
            } catch (...) {
                state->error = std::current_exception();
            }
        });
        std::lock_guard lock(state->mutex);
        state->done = true;
        state->done_condition.notify_all();
    });
    return Child(std::move(scope), scope_.cancel_token(), std::move(state));
}

}  // namespace teta::wot
