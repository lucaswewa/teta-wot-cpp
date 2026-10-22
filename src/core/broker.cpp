#include "teta/wot/broker.hpp"

#include <algorithm>

#include "teta/wot/log.hpp"

namespace teta::wot {

struct Subscription::Channel {
    std::mutex mutex;
    std::condition_variable condition;
    std::deque<Message> queue;
    bool closed = false;
    std::atomic<std::uint64_t> dropped{0};
    std::function<void()> notify;
};

std::string_view to_string(MessageKind kind) {
    switch (kind) {
        case MessageKind::property:
            return "property";
        case MessageKind::action:
            return "action";
        case MessageKind::event:
            break;
    }
    return "event";
}

Message Message::now(std::string thing, std::string affordance, MessageKind kind, json::Value payload) {
    return {std::move(thing), std::move(affordance), kind, std::move(payload),
            std::chrono::system_clock::now()};
}

// ---- Subscription ----------------------------------------------------------------

std::optional<Message> Subscription::recv() {
    std::unique_lock lock(channel_->mutex);
    channel_->condition.wait(lock, [&] { return !channel_->queue.empty() || channel_->closed; });
    if (channel_->queue.empty()) {
        return std::nullopt;
    }
    Message message = std::move(channel_->queue.front());
    channel_->queue.pop_front();
    return message;
}

std::optional<Message> Subscription::recv_for(std::chrono::nanoseconds timeout) {
    std::unique_lock lock(channel_->mutex);
    channel_->condition.wait_for(lock, timeout, [&] { return !channel_->queue.empty() || channel_->closed; });
    if (channel_->queue.empty()) {
        return std::nullopt;
    }
    Message message = std::move(channel_->queue.front());
    channel_->queue.pop_front();
    return message;
}

std::optional<Message> Subscription::try_recv() {
    std::lock_guard lock(channel_->mutex);
    if (channel_->queue.empty()) {
        return std::nullopt;
    }
    Message message = std::move(channel_->queue.front());
    channel_->queue.pop_front();
    return message;
}

bool Subscription::finished() const {
    std::lock_guard lock(channel_->mutex);
    return channel_->closed && channel_->queue.empty();
}

std::uint64_t Subscription::dropped() const {
    return channel_->dropped.load();
}

void Subscription::set_notifier(std::function<void()> notify) {
    std::lock_guard lock(channel_->mutex);
    channel_->notify = std::move(notify);
}

// ---- MessageBroker ---------------------------------------------------------------

Subscription MessageBroker::subscription() {
    auto channel = std::make_shared<Subscription::Channel>();
    std::lock_guard lock(mutex_);
    channel->closed = closed_;
    std::erase_if(channels_, [](const auto& weak) { return weak.expired(); });
    channels_.push_back(channel);
    return Subscription(std::move(channel));
}

Subscription MessageBroker::subscribe(std::string_view thing, std::string_view affordance) {
    Subscription subscription = this->subscription();
    add(subscription, thing, affordance);
    return subscription;
}

bool MessageBroker::add(const Subscription& subscription, std::string_view thing,
                        std::string_view affordance) {
    std::lock_guard lock(mutex_);
    if (closed_) {
        return false;
    }
    auto& list = subscribers_[{std::string(thing), std::string(affordance)}];
    const bool already =
        std::ranges::any_of(list, [&](const auto& weak) { return weak.lock() == subscription.channel_; });
    if (already) {
        return false;
    }
    list.push_back(subscription.channel_);
    return true;
}

bool MessageBroker::has_subscribers(std::string_view thing, std::string_view affordance) const {
    std::lock_guard lock(mutex_);
    const auto it = subscribers_.find({std::string(thing), std::string(affordance)});
    return it != subscribers_.end() &&
           std::ranges::any_of(it->second, [](const auto& weak) { return !weak.expired(); });
}

void MessageBroker::publish(const Message& message) {
    std::vector<std::function<void()>> notifications;
    std::uint64_t full = 0;
    {
        std::lock_guard lock(mutex_);
        const auto it = subscribers_.find({message.thing, message.affordance});
        if (it == subscribers_.end()) {
            return;
        }
        auto& list = it->second;
        std::erase_if(list, [](const auto& weak) { return weak.expired(); });
        for (const auto& weak : list) {
            const auto channel = weak.lock();
            if (!channel) {
                continue;
            }
            std::lock_guard channel_lock(channel->mutex);
            if (channel->queue.size() >= subscriber_buffer) {
                channel->dropped.fetch_add(1);
                ++full;
                continue;
            }
            channel->queue.push_back(message);
            channel->condition.notify_all();
            if (channel->notify) {
                notifications.push_back(channel->notify);
            }
        }
        if (list.empty()) {
            subscribers_.erase(it);
        }
    }
    for (std::uint64_t i = 0; i < full; ++i) {
        // Not captured: this isn't part of the publishing invocation's log.
        log::emit({log::Level::warn,
                   "Could not pass a notification to a subscriber, as its buffer was full. thing=" +
                       message.thing + " affordance=" + message.affordance,
                   __FILE__, __LINE__, std::chrono::system_clock::now(), std::nullopt, std::nullopt},
                  false);
    }
    for (const auto& notify : notifications) {
        notify();
    }
}

void MessageBroker::close() {
    std::vector<std::function<void()>> notifications;
    {
        std::lock_guard lock(mutex_);
        if (closed_) {
            return;
        }
        closed_ = true;
        subscribers_.clear();
        for (const auto& weak : channels_) {
            if (const auto channel = weak.lock()) {
                std::lock_guard channel_lock(channel->mutex);
                channel->closed = true;
                channel->condition.notify_all();
                if (channel->notify) {
                    notifications.push_back(channel->notify);
                }
            }
        }
        channels_.clear();
        notifications.insert(notifications.end(), close_callbacks_.begin(), close_callbacks_.end());
        close_callbacks_.clear();
    }
    closed_condition_.notify_all();
    for (const auto& notify : notifications) {
        notify();
    }
}

bool MessageBroker::is_closed() const {
    std::lock_guard lock(mutex_);
    return closed_;
}

void MessageBroker::wait_closed() const {
    std::unique_lock lock(mutex_);
    closed_condition_.wait(lock, [&] { return closed_; });
}

void MessageBroker::on_close(std::function<void()> callback) {
    {
        std::lock_guard lock(mutex_);
        if (!closed_) {
            close_callbacks_.push_back(std::move(callback));
            return;
        }
    }
    callback();
}

}  // namespace teta::wot
