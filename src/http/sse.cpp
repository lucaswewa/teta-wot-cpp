#include "sse.hpp"

#include <algorithm>

#include "render.hpp"

namespace teta::wot::http::detail {

bool wants_event_stream(const Headers& headers) {
    for (const auto value : headers.get_all("accept")) {
        for (std::size_t start = 0; start <= value.size();) {
            const auto comma = value.find(',', start);
            auto range =
                value.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start);
            range = range.substr(0, range.find(';'));
            while (!range.empty() && (range.front() == ' ' || range.front() == '\t')) {
                range.remove_prefix(1);
            }
            while (!range.empty() && (range.back() == ' ' || range.back() == '\t')) {
                range.remove_suffix(1);
            }
            if (equals_ignoring_case(range, "text/event-stream")) {
                return true;
            }
            if (comma == std::string_view::npos) {
                break;
            }
            start = comma + 1;
        }
    }
    return false;
}

/// One client's stream.
class SseStream : public std::enable_shared_from_this<SseStream> {
  public:
    SseStream(std::weak_ptr<SseStreams> owner, Subscription subscription, bool named, Urls urls)
        : owner_(std::move(owner)),
          subscription_(std::move(subscription)),
          named_(named),
          urls_(std::move(urls)) {}

    void start(std::shared_ptr<StreamSink> sink) {
        sink->set_resume([weak = weak_from_this()] {
            if (auto self = weak.lock()) {
                self->pump();
            }
        });
        {
            std::lock_guard lock(mutex_);
            sink_ = std::move(sink);
            last_sent_ = std::chrono::steady_clock::now();
        }
        if (const auto owner = owner_.lock()) {
            owner->track(shared_from_this());
        }
        subscription_.set_notifier([weak = weak_from_this()] {
            if (auto self = weak.lock()) {
                self->pump();
            }
        });
        pump();  // what came before, or a broker already closed
    }

    // Sends what is buffered while the client keeps up; ends the stream when
    // the broker is closed or the client has gone.
    void pump() {
        std::unique_lock lock(mutex_);
        if (!sink_) {
            return;
        }
        while (sink_->ready()) {
            auto message = subscription_.try_recv();
            if (!message) {
                break;
            }
            resolve_blobs(message->payload, urls_);
            std::string event = "data: " + json::dump(message->payload) + "\n";
            if (named_) {
                event += "event: " + message->affordance + "\nid: " + rfc3339(message->time, 9) + "\n";
            }
            event += "\n";
            if (!sink_->send(event)) {
                end(lock);
                return;
            }
            last_sent_ = std::chrono::steady_clock::now();
        }
        if (subscription_.finished()) {
            end(lock);
        }
    }

    void keep_alive(std::chrono::steady_clock::time_point now) {
        std::unique_lock lock(mutex_);
        if (!sink_ || now - last_sent_ < SseStreams::keep_alive_interval) {
            return;
        }
        if (!sink_->send(":\n\n")) {
            end(lock);
            return;
        }
        last_sent_ = now;
    }

  private:
    void end(std::unique_lock<std::mutex>& lock) {
        const auto sink = std::move(sink_);
        sink_.reset();
        lock.unlock();
        sink->close();
        if (const auto owner = owner_.lock()) {
            owner->forget(this);
        }
    }

    std::weak_ptr<SseStreams> owner_;
    Subscription subscription_;
    bool named_;
    Urls urls_;
    std::mutex mutex_;
    std::shared_ptr<StreamSink> sink_;
    std::chrono::steady_clock::time_point last_sent_;
};

SseStreams::~SseStreams() {
    if (thread_.joinable()) {
        thread_.request_stop();
        // The keep-alive thread itself may hold the last reference.
        if (thread_.get_id() == std::this_thread::get_id()) {
            thread_.detach();
        } else {
            thread_.join();
        }
    }
}

Response SseStreams::response(Subscription subscription, bool named, Urls urls) {
    auto stream =
        std::make_shared<SseStream>(weak_from_this(), std::move(subscription), named, std::move(urls));
    Response response;
    response.status = 200;
    response.headers.set("content-type", "text/event-stream");
    response.headers.set("cache-control", "no-cache");
    response.stream = [stream](std::shared_ptr<StreamSink> sink) { stream->start(std::move(sink)); };
    return response;
}

void SseStreams::track(std::shared_ptr<SseStream> stream) {
    std::lock_guard lock(mutex_);
    streams_.push_back(std::move(stream));
    if (!thread_.joinable()) {
        thread_ = std::jthread([this](std::stop_token stop) {
            std::unique_lock lock(mutex_);
            while (
                !wake_.wait_for(lock, stop, std::chrono::seconds(1), [&] { return stop.stop_requested(); })) {
                const auto streams = streams_;
                lock.unlock();
                const auto now = std::chrono::steady_clock::now();
                for (const auto& stream : streams) {
                    stream->keep_alive(now);
                }
                lock.lock();
            }
        });
    }
}

void SseStreams::forget(const SseStream* stream) {
    std::shared_ptr<SseStream> last;  // destroyed after the lock is released
    std::lock_guard lock(mutex_);
    const auto it = std::ranges::find_if(streams_, [&](const auto& s) { return s.get() == stream; });
    if (it != streams_.end()) {
        last = std::move(*it);
        streams_.erase(it);
    }
}

}  // namespace teta::wot::http::detail
