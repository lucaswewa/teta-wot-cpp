// Publish/subscribe for property changes, action status and events: a port
// of teta-wot-core's `broker`.
//
// A Subscription is one channel with a small buffer (5 messages), and it
// can subscribe to several affordances: a WebSocket connection has one
// channel for everything it observes. The broker never waits: when a
// subscriber's buffer is full the message is dropped for that subscriber,
// with a warning, and counted. Destroyed subscriptions are forgotten on the
// next publish. `MessageBroker::close` ends every subscription, so that
// streams to clients end when the server stops.

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "teta/wot/json.hpp"

namespace teta::wot {

/// The per-subscriber buffer size.
inline constexpr std::size_t subscriber_buffer = 5;

/// What kind of affordance a message comes from.
enum class MessageKind { property, action, event };

/// `property`, `action` or `event`.
std::string_view to_string(MessageKind kind);

/// A published message.
struct Message {
    std::string thing;
    std::string affordance;
    MessageKind kind = MessageKind::property;
    /// The property's value, the invocation's status, or the event's data.
    json::Value payload;
    std::chrono::system_clock::time_point time;

    /// A message published now.
    static Message now(std::string thing, std::string affordance, MessageKind kind, json::Value payload);

    bool operator==(const Message&) const = default;
};

class MessageBroker;

/// Receives the messages of the affordances it is subscribed to.
/// Destroying it unsubscribes from all of them.
class Subscription {
  public:
    Subscription(Subscription&&) noexcept = default;
    Subscription& operator=(Subscription&&) noexcept = default;
    ~Subscription() = default;

    /// The next message, waiting for one; `nullopt` once the broker is
    /// closed and the messages already buffered have been received.
    std::optional<Message> recv();
    /// `recv`, waiting at most `timeout` (`nullopt` if nothing came).
    std::optional<Message> recv_for(std::chrono::nanoseconds timeout);
    /// The next message if one is waiting.
    std::optional<Message> try_recv();
    /// Whether the broker is closed and nothing is buffered.
    bool finished() const;
    /// How many messages were dropped because the buffer was full.
    std::uint64_t dropped() const;
    /// Calls `notify` (from the publishing or closing thread, without locks
    /// held) whenever a message is buffered or the broker closes: for
    /// consumers that don't wait in `recv`.
    void set_notifier(std::function<void()> notify);

  private:
    friend class MessageBroker;
    struct Channel;
    explicit Subscription(std::shared_ptr<Channel> channel) : channel_(std::move(channel)) {}
    std::shared_ptr<Channel> channel_;
};

/// Relays messages to the subscribers of each (Thing, affordance) pair.
class MessageBroker {
  public:
    MessageBroker() = default;
    MessageBroker(const MessageBroker&) = delete;
    MessageBroker& operator=(const MessageBroker&) = delete;

    /// A subscription to nothing yet: add affordances with `add`.
    Subscription subscription();
    /// A subscription to one affordance.
    Subscription subscribe(std::string_view thing, std::string_view affordance);
    /// Adds an affordance to a subscription. Returns false if it was already
    /// subscribed (it still receives each message once), or if the broker is
    /// closed.
    bool add(const Subscription& subscription, std::string_view thing, std::string_view affordance);
    /// Whether anyone is subscribed to the affordance (so that publishers can
    /// skip making a payload nobody reads).
    bool has_subscribers(std::string_view thing, std::string_view affordance) const;
    /// Sends a message to every subscriber of its affordance, without waiting.
    void publish(const Message& message);
    /// Ends every subscription (they receive what is already buffered, then
    /// `nullopt`) and refuses new ones.
    void close();
    bool is_closed() const;
    /// Waits until the broker is closed.
    void wait_closed() const;
    /// Calls `callback` once the broker is closed (at once if it is).
    void on_close(std::function<void()> callback);

  private:
    using Key = std::pair<std::string, std::string>;
    mutable std::mutex mutex_;
    mutable std::condition_variable closed_condition_;
    bool closed_ = false;
    std::map<Key, std::vector<std::weak_ptr<Subscription::Channel>>> subscribers_;
    std::vector<std::weak_ptr<Subscription::Channel>> channels_;
    std::vector<std::function<void()>> close_callbacks_;
};

}  // namespace teta::wot
