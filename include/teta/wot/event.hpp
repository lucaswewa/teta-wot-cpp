// Events: a port of teta-wot-core's `event`.
//
// An event is an Event<T> member of the Thing, declared in its definition;
// emitting it sends the data to its subscribers: clients over WebSocket and
// SSE, through the server's message broker, and C++ code through
// `Event::subscribe`. The TD describes the data with T's schema.

#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "teta/wot/broker.hpp"
#include "teta/wot/codec.hpp"
#include "teta/wot/log.hpp"

namespace teta::wot {

/// How many events a C++ subscriber can fall behind before it misses some.
inline constexpr std::size_t local_event_buffer = 16;

/// A type that can be an event's data.
template <typename T>
concept EventData = Encodable<T> && std::copyable<T>;

template <EventData T>
class EventReceiver;

/// An event that a Thing emits. A cheap handle: copies emit the same event,
/// from any thread, without waiting.
template <EventData T>
class Event {
  public:
    Event() : shared_(std::make_shared<Shared>()) {}

    /// Sends `data` to every subscriber, without waiting. Subscribers whose
    /// buffers are full miss it.
    void emit(const T& data) const {
        std::optional<Binding> binding;
        std::vector<std::shared_ptr<typename EventReceiver<T>::Queue>> receivers;
        {
            std::lock_guard lock(shared_->mutex);
            binding = shared_->binding;
            std::erase_if(shared_->receivers, [](const auto& weak) { return weak.expired(); });
            for (const auto& weak : shared_->receivers) {
                if (auto queue = weak.lock()) {
                    receivers.push_back(std::move(queue));
                }
            }
        }
        if (binding && binding->broker->has_subscribers(binding->thing, binding->name)) {
            binding->broker->publish(
                Message::now(binding->thing, binding->name, MessageKind::event, encode(data)));
        }
        for (const auto& queue : receivers) {
            queue->push(data);
        }
    }

    /// Receives the events emitted from now on, in C++. A receiver that falls
    /// more than `local_event_buffer` events behind misses the oldest ones.
    EventReceiver<T> subscribe() const {
        auto queue = std::make_shared<typename EventReceiver<T>::Queue>();
        std::lock_guard lock(shared_->mutex);
        shared_->receivers.push_back(queue);
        return EventReceiver<T>(std::move(queue));
    }

    /// The schema of the event's data, or none for `std::monostate` (Rust's
    /// `()`).
    static std::optional<td::DataSchema> data_schema() {
        td::DataSchema schema = for_type<T>();
        td::DataSchema null_schema;
        null_schema.data_type = td::DataType::null;
        if (schema == null_schema) {
            return std::nullopt;
        }
        return schema;
    }

    // For the runtime: where the event publishes; the first binding stays.
    void bind(std::string thing, std::string name, std::shared_ptr<MessageBroker> broker) const {
        std::lock_guard lock(shared_->mutex);
        if (!shared_->binding) {
            shared_->binding = Binding{std::move(thing), std::move(name), std::move(broker)};
        }
    }

  private:
    struct Binding {
        std::string thing;
        std::string name;
        std::shared_ptr<MessageBroker> broker;
    };
    struct Shared {
        std::mutex mutex;
        std::optional<Binding> binding;
        std::vector<std::weak_ptr<typename EventReceiver<T>::Queue>> receivers;
    };
    std::shared_ptr<Shared> shared_;
};

/// Receives an event's data in C++.
template <EventData T>
class EventReceiver {
  public:
    /// The next event if one is waiting.
    std::optional<T> try_recv() {
        std::lock_guard lock(queue_->mutex);
        return pop();
    }
    /// The next event, waiting at most `timeout`.
    std::optional<T> recv_for(std::chrono::nanoseconds timeout) {
        std::unique_lock lock(queue_->mutex);
        queue_->arrived.wait_for(lock, timeout, [&] { return !queue_->items.empty(); });
        return pop();
    }
    /// How many events were missed because the receiver fell behind.
    std::uint64_t missed() const {
        std::lock_guard lock(queue_->mutex);
        return queue_->missed;
    }

  private:
    template <EventData>
    friend class Event;

    struct Queue {
        std::mutex mutex;
        std::condition_variable arrived;
        std::deque<T> items;
        std::uint64_t missed = 0;
        void push(const T& item) {
            {
                std::lock_guard lock(mutex);
                if (items.size() == local_event_buffer) {
                    items.pop_front();
                    ++missed;
                }
                items.push_back(item);
            }
            arrived.notify_all();
        }
    };

    explicit EventReceiver(std::shared_ptr<Queue> queue) : queue_(std::move(queue)) {}

    std::optional<T> pop() {
        if (queue_->items.empty()) {
            return std::nullopt;
        }
        T item = std::move(queue_->items.front());
        queue_->items.pop_front();
        return item;
    }

    std::shared_ptr<Queue> queue_;
};

/// One event of a running Thing, as clients see it.
struct EventEntry {
    std::string name;
    std::string title;
    std::optional<std::string> description;
    std::vector<std::string> semantic_types;
    /// The schema of the event's data (none for events without data).
    std::optional<td::DataSchema> data;
};

}  // namespace teta::wot
