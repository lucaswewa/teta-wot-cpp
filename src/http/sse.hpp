// Server-sent events: a port of the SSE half of teta-wot-http's `observe.rs`,
// written as axum's `Sse` writes them.
//
// Each stream is driven by its broker subscription's notifier: a published
// message is sent by the publishing thread, and no thread waits on a
// client. One keep-alive thread per binding sends axum's `:` comment to any
// stream that has been quiet for 15 seconds, and so finds the clients that
// have gone away.

#pragma once

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "render.hpp"
#include "teta/wot/broker.hpp"
#include "teta/wot/http/message.hpp"

namespace teta::wot::http::detail {

/// Whether a request asks for server-sent events (`Accept:
/// text/event-stream`, among other ranges or not).
bool wants_event_stream(const Headers& headers);

class SseStream;

/// The streams of one binding, and their keep-alives.
class SseStreams : public std::enable_shared_from_this<SseStreams> {
  public:
    /// How long a stream may be quiet before a keep-alive (axum's default).
    static constexpr std::chrono::seconds keep_alive_interval{15};

    SseStreams() = default;
    SseStreams(const SseStreams&) = delete;
    SseStreams& operator=(const SseStreams&) = delete;
    /// Ends the keep-alive thread; streams still open are left to end with
    /// the broker.
    ~SseStreams();

    /// A `text/event-stream` answer relaying `subscription`'s messages: the
    /// payload as `data`, and with `named` the affordance as `event` and the
    /// time as `id`.
    Response response(Subscription subscription, bool named, Urls urls);

  private:
    friend class SseStream;
    void track(std::shared_ptr<SseStream> stream);
    void forget(const SseStream* stream);

    std::mutex mutex_;
    std::condition_variable_any wake_;
    std::vector<std::shared_ptr<SseStream>> streams_;
    std::jthread thread_;
};

}  // namespace teta::wot::http::detail
