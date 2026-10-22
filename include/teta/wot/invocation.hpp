// Action invocations and their manager: a port of teta-wot-core's
// `invocation`.
//
// An invocation goes `pending` → `running` → `completed`, `error` or
// `cancelled`; it can also go straight from `pending` to `error` when the
// global lock is busy. Finished invocations are kept for their action's
// retention time (300 s by default), then removed by
// `InvocationManager::expire`, which runs on every new invocation.

#pragma once

#include <chrono>
#include <condition_variable>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "teta/wot/broker.hpp"
#include "teta/wot/cancel.hpp"
#include "teta/wot/json.hpp"
#include "teta/wot/log.hpp"
#include "teta/wot/problem.hpp"
#include "teta/wot/uuid.hpp"

namespace teta::wot {

/// Where an invocation is in its life.
enum class InvocationStatus { pending, running, completed, cancelled, error };

/// The name on the wire, such as `running`.
std::string_view to_string(InvocationStatus status);

/// Whether the invocation has finished.
inline bool is_finished(InvocationStatus status) {
    return status != InvocationStatus::pending && status != InvocationStatus::running;
}

/// The monotonic clock retention is measured with (replaceable for tests).
using Clock = std::function<std::chrono::steady_clock::time_point()>;

/// A snapshot of an invocation, with everything.
struct InvocationRecord {
    InvocationStatus status = InvocationStatus::pending;
    Uuid id;
    std::string thing;
    std::string action;
    std::chrono::system_clock::time_point time_requested;
    std::optional<std::chrono::system_clock::time_point> time_started;
    std::optional<std::chrono::system_clock::time_point> time_completed;
    /// The validated input.
    json::Value input;
    /// The output, once completed.
    std::optional<json::Value> output;
    std::vector<LogRecord> log;
    /// What went wrong, for `error` and `cancelled`.
    std::optional<ProblemDetails> error;
};

/// One invocation of an action.
class Invocation {
  public:
    Invocation(Uuid id, std::string thing, std::string action, json::Value input,
               std::chrono::nanoseconds retention, CancelToken cancel, std::shared_ptr<LogBuffer> logs,
               std::shared_ptr<MessageBroker> broker, Clock clock);

    const Uuid& id() const { return id_; }
    const std::string& thing() const { return thing_; }
    const std::string& action() const { return action_; }
    /// The validated input.
    const json::Value& input() const { return input_; }
    InvocationStatus status() const;
    /// The output, once completed.
    std::optional<json::Value> output() const;
    /// What went wrong, for `error` and `cancelled` invocations.
    std::optional<ProblemDetails> error() const;
    /// The log so far.
    std::vector<LogRecord> logs() const { return logs_->records(); }
    const CancelToken& cancel_token() const { return cancel_; }
    /// A snapshot of everything.
    InvocationRecord record() const;

    /// Waits until the invocation has finished, and returns its status.
    InvocationStatus wait() const;
    /// `wait`, for at most `timeout`; `nullopt` if it hasn't finished.
    std::optional<InvocationStatus> wait_for(std::chrono::nanoseconds timeout) const;
    /// Calls `callback` once the invocation has finished (at once if it has).
    void on_finished(std::function<void()> callback);

    // For the runtime.
    void announce();
    void set_running();
    void finish(InvocationStatus status, std::optional<json::Value> output,
                std::optional<ProblemDetails> error);
    bool expired(std::chrono::steady_clock::time_point now) const;

  private:
    void publish(InvocationStatus status);

    Uuid id_;
    std::string thing_;
    std::string action_;
    json::Value input_;
    std::chrono::nanoseconds retention_;
    CancelToken cancel_;
    std::shared_ptr<LogBuffer> logs_;
    std::shared_ptr<MessageBroker> broker_;
    Clock clock_;

    mutable std::mutex mutex_;
    mutable std::condition_variable finished_condition_;
    InvocationStatus status_ = InvocationStatus::pending;
    std::chrono::system_clock::time_point requested_;
    std::optional<std::chrono::system_clock::time_point> started_;
    std::optional<std::chrono::system_clock::time_point> completed_;
    std::optional<json::Value> output_;
    std::optional<ProblemDetails> error_;
    std::optional<std::chrono::steady_clock::time_point> expires_;
    std::vector<std::function<void()>> finished_callbacks_;
};

/// An invocation can't be cancelled.
class CancelError : public std::exception {
  public:
    enum class Kind { not_found, not_cancellable };
    CancelError(Kind kind, std::string message) : kind_(kind), message_(std::move(message)) {}
    Kind kind() const { return kind_; }
    const char* what() const noexcept override { return message_.c_str(); }

  private:
    Kind kind_;
    std::string message_;
};

/// All invocations that are running or finished recently, oldest first.
class InvocationManager {
  public:
    explicit InvocationManager(Clock clock = {});

    void insert(std::shared_ptr<Invocation> invocation);
    /// An invocation by ID.
    std::shared_ptr<Invocation> get(const Uuid& id) const;
    /// Every invocation, oldest first.
    std::vector<std::shared_ptr<Invocation>> list() const;
    /// The invocations of one action of one Thing, oldest first.
    std::vector<std::shared_ptr<Invocation>> list_for(std::string_view thing, std::string_view action) const;
    /// Asks a pending or running invocation to stop; throws CancelError.
    void cancel(const Uuid& id);
    /// Asks every unfinished invocation to stop (for shutdown).
    void cancel_all();
    /// Waits until no invocation is unfinished, for at most `timeout`;
    /// returns whether they all finished.
    bool wait_idle(std::chrono::nanoseconds timeout) const;
    /// Removes finished invocations whose retention has passed; returns how
    /// many were removed.
    std::size_t expire();
    const Clock& clock() const { return clock_; }

  private:
    Clock clock_;
    mutable std::mutex mutex_;
    std::vector<std::shared_ptr<Invocation>> order_;
    std::unordered_map<Uuid, std::shared_ptr<Invocation>> by_id_;
};

}  // namespace teta::wot
