#include "teta/wot/invocation.hpp"

#include <algorithm>
#include <format>

namespace teta::wot {

std::string_view to_string(InvocationStatus status) {
    switch (status) {
        case InvocationStatus::pending:
            return "pending";
        case InvocationStatus::running:
            return "running";
        case InvocationStatus::completed:
            return "completed";
        case InvocationStatus::cancelled:
            return "cancelled";
        case InvocationStatus::error:
            break;
    }
    return "error";
}

namespace {

Clock or_steady(Clock clock) {
    if (clock) {
        return clock;
    }
    return [] { return std::chrono::steady_clock::now(); };
}

}  // namespace

Invocation::Invocation(Uuid id, std::string thing, std::string action, json::Value input,
                       std::chrono::nanoseconds retention, CancelToken cancel,
                       std::shared_ptr<LogBuffer> logs, std::shared_ptr<MessageBroker> broker, Clock clock)
    : id_(id),
      thing_(std::move(thing)),
      action_(std::move(action)),
      input_(std::move(input)),
      retention_(retention),
      cancel_(std::move(cancel)),
      logs_(std::move(logs)),
      broker_(std::move(broker)),
      clock_(or_steady(std::move(clock))),
      requested_(std::chrono::system_clock::now()) {}

InvocationStatus Invocation::status() const {
    std::lock_guard lock(mutex_);
    return status_;
}

std::optional<json::Value> Invocation::output() const {
    std::lock_guard lock(mutex_);
    return output_;
}

std::optional<ProblemDetails> Invocation::error() const {
    std::lock_guard lock(mutex_);
    return error_;
}

InvocationRecord Invocation::record() const {
    std::lock_guard lock(mutex_);
    return {status_,    id_,    thing_,  action_,          requested_, started_,
            completed_, input_, output_, logs_->records(), error_};
}

InvocationStatus Invocation::wait() const {
    std::unique_lock lock(mutex_);
    finished_condition_.wait(lock, [&] { return is_finished(status_); });
    return status_;
}

std::optional<InvocationStatus> Invocation::wait_for(std::chrono::nanoseconds timeout) const {
    std::unique_lock lock(mutex_);
    if (!finished_condition_.wait_for(lock, timeout, [&] { return is_finished(status_); })) {
        return std::nullopt;
    }
    return status_;
}

void Invocation::on_finished(std::function<void()> callback) {
    {
        std::lock_guard lock(mutex_);
        if (!is_finished(status_)) {
            finished_callbacks_.push_back(std::move(callback));
            return;
        }
    }
    callback();
}

void Invocation::publish(InvocationStatus status) {
    if (broker_) {
        broker_->publish(Message::now(thing_, action_, MessageKind::action, std::string(to_string(status))));
    }
}

void Invocation::announce() {
    publish(InvocationStatus::pending);
}

void Invocation::set_running() {
    {
        std::lock_guard lock(mutex_);
        status_ = InvocationStatus::running;
        started_ = std::chrono::system_clock::now();
    }
    publish(InvocationStatus::running);
}

void Invocation::finish(InvocationStatus status, std::optional<json::Value> output,
                        std::optional<ProblemDetails> error) {
    std::vector<std::function<void()>> callbacks;
    {
        std::lock_guard lock(mutex_);
        status_ = status;
        output_ = std::move(output);
        error_ = std::move(error);
        completed_ = std::chrono::system_clock::now();
        expires_ = clock_() + retention_;
        callbacks.swap(finished_callbacks_);
    }
    publish(status);
    finished_condition_.notify_all();
    for (const auto& callback : callbacks) {
        callback();
    }
}

bool Invocation::expired(std::chrono::steady_clock::time_point now) const {
    std::lock_guard lock(mutex_);
    return expires_ && *expires_ <= now;
}

std::string format_rfc3339(std::chrono::system_clock::time_point time) {
    using namespace std::chrono;
    auto nanos = duration_cast<nanoseconds>(time.time_since_epoch()).count() % 1'000'000'000;
    if (nanos < 0) {
        nanos += 1'000'000'000;
    }
    std::string text = format_utc(time_point_cast<seconds>(time - nanoseconds(nanos)));
    text.pop_back();  // the `Z`
    if (nanos % 1'000'000 == 0 && nanos != 0) {
        text += std::format(".{:03}", nanos / 1'000'000);
    } else if (nanos % 1'000 == 0 && nanos != 0) {
        text += std::format(".{:06}", nanos / 1'000);
    } else if (nanos != 0) {
        text += std::format(".{:09}", nanos);
    }
    return text + "Z";
}

json::Value to_json(const InvocationRecord& record) {
    auto time = [](const std::optional<std::chrono::system_clock::time_point>& t) -> json::Value {
        if (t) {
            return format_rfc3339(*t);
        }
        return nullptr;
    };
    json::Value log = json::Value::array();
    for (const auto& entry : record.log) {
        log.push_back(to_json(entry));
    }
    return {{"status", std::string(to_string(record.status))},
            {"id", record.id.to_string()},
            {"thing", record.thing},
            {"action", record.action},
            {"time_requested", format_rfc3339(record.time_requested)},
            {"time_started", time(record.time_started)},
            {"time_completed", time(record.time_completed)},
            {"input", record.input},
            {"output", record.output ? *record.output : json::Value(nullptr)},
            {"log", std::move(log)},
            {"error", record.error ? to_json(*record.error) : json::Value(nullptr)}};
}

// ---- InvocationManager ---------------------------------------------------------------

InvocationManager::InvocationManager(Clock clock) : clock_(or_steady(std::move(clock))) {}

void InvocationManager::insert(std::shared_ptr<Invocation> invocation) {
    std::lock_guard lock(mutex_);
    by_id_[invocation->id()] = invocation;
    order_.push_back(std::move(invocation));
}

std::shared_ptr<Invocation> InvocationManager::get(const Uuid& id) const {
    std::lock_guard lock(mutex_);
    const auto it = by_id_.find(id);
    return it == by_id_.end() ? nullptr : it->second;
}

std::vector<std::shared_ptr<Invocation>> InvocationManager::list() const {
    std::lock_guard lock(mutex_);
    return order_;
}

std::vector<std::shared_ptr<Invocation>> InvocationManager::list_for(std::string_view thing,
                                                                     std::string_view action) const {
    std::vector<std::shared_ptr<Invocation>> out;
    std::lock_guard lock(mutex_);
    for (const auto& invocation : order_) {
        if (invocation->thing() == thing && invocation->action() == action) {
            out.push_back(invocation);
        }
    }
    return out;
}

void InvocationManager::cancel(const Uuid& id) {
    const auto invocation = get(id);
    if (!invocation) {
        // teta-wot fills the ID in (reference bug B1).
        throw CancelError(CancelError::Kind::not_found,
                          "No action invocation found with ID " + id.to_string());
    }
    const auto status = invocation->status();
    if (is_finished(status)) {
        // A plain status name (reference bug B2).
        throw CancelError(
            CancelError::Kind::not_cancellable,
            "The invocation is " + std::string(to_string(status)) + " and may not be cancelled.");
    }
    invocation->cancel_token().cancel();
}

void InvocationManager::cancel_all() {
    for (const auto& invocation : list()) {
        if (!is_finished(invocation->status())) {
            invocation->cancel_token().cancel();
        }
    }
}

bool InvocationManager::wait_idle(std::chrono::nanoseconds timeout) const {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (const auto& invocation : list()) {
        const auto left = deadline - std::chrono::steady_clock::now();
        if (!invocation->wait_for(std::max(left, std::chrono::steady_clock::duration::zero()))) {
            return false;
        }
    }
    return true;
}

std::size_t InvocationManager::expire() {
    const auto now = clock_();
    std::lock_guard lock(mutex_);
    const auto before = order_.size();
    std::erase_if(order_, [&](const auto& invocation) {
        if (invocation->expired(now)) {
            by_id_.erase(invocation->id());
            return true;
        }
        return false;
    });
    return before - order_.size();
}

}  // namespace teta::wot
