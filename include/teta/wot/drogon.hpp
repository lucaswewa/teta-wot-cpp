// The Drogon transport (ADR-0004): Drogon parses requests and writes
// responses; the HTTP binding decides every answer.
//
// `mount` serves a binding from a Drogon application: its default handler
// answers every request that the application's own Drogon routes don't, so
// Things can be served next to existing controllers (the
// `drogon-integration` example). `ThingServer::serve` uses it, with a
// listener, the shutdown sequence and signal handling of its own.
//
// What the Phase 0 spike established, and this relies on:
// - the default handler receives every path and every method Drogon's parser
//   knows; `getOriginalPath()` is the path as sent;
// - a streamed body (`newAsyncStreamResponse`) is fed from any thread; but
//   Drogon doesn't start one on a connection it will close (`Connection:
//   close`, HTTP/1.0), so a pre-sending advice keeps such a connection open,
//   says `connection: close`, and the stream shuts the connection after its
//   last chunk;
// - the body size limit is lifted to 128 MiB (the binding refuses more than
//   64 MiB, as teta-wot does), and held in memory;
// - Drogon's home page is turned off: otherwise it turns a request for `/`
//   into one for `/index.html` before the default handler sees it;
// - no `server` header; a `date` header;
// - a regex WebSocket controller (an instance registered with
//   `registerController` first) serves `{prefix}/(thing|...)/ws`; a
//   pre-routing advice answers other WebSocket requests as the binding
//   answers their path, since Drogon would answer them with its own 404 page;
// - trantor neither says when a connection's output has drained nor lets
//   other threads read it, so streams and WebSockets count what they send and
//   check `bytesSent()` on the connection's loop once more than 256 KiB may be
//   unsent: until less than 64 KiB is, they take no more notifications, and
//   the broker drops what doesn't fit (backpressure).

#pragma once

// Drogon includes <windows.h>, whose `min` and `max` macros break
// `std::numeric_limits<T>::max()`.
#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <drogon/HttpAppFramework.h>
#include <drogon/WebSocketController.h>

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>

#include "teta/wot/http/binding.hpp"

namespace teta::wot::drogon_adapter {

/// The largest body Drogon reads; larger ones get Drogon's own 413.
inline constexpr std::size_t max_body_size = 128 * 1024 * 1024;

/// The binding's request for a Drogon request.
http::Request to_request(const drogon::HttpRequest& request);

/// Drogon's response for the binding's. A streamed body becomes an async
/// stream response, which ends the connection after it if `request` didn't
/// keep it alive.
drogon::HttpResponsePtr to_response(http::Response response, const drogon::HttpRequestPtr& request);

/// What answers requests: a binding's `handle`, or the fallback server's.
using Service = std::function<void(http::Request, http::HttpBinding::Respond)>;

/// Answers Drogon's requests with a binding (or another service).
class Handler {
  public:
    explicit Handler(std::shared_ptr<const http::HttpBinding> binding);
    explicit Handler(Service service);

    void operator()(const drogon::HttpRequestPtr& request,
                    std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;

    /// Requests being answered (a streamed answer counts until its headers
    /// are sent).
    std::size_t in_flight() const { return in_flight_->load(); }

  private:
    Service service_;
    std::shared_ptr<std::atomic<std::size_t>> in_flight_ = std::make_shared<std::atomic<std::size_t>>(0);
};

/// Serves the Things' WebSockets (`{prefix}/{thing}/ws`) with a binding: the
/// WebSocket controller `mount` registers, for the binding's Things.
class ThingSockets final : public drogon::WebSocketController<ThingSockets, false> {
  public:
    explicit ThingSockets(std::shared_ptr<const http::HttpBinding> binding);

    void handleNewConnection(const drogon::HttpRequestPtr& request,
                             const drogon::WebSocketConnectionPtr& connection) override;
    void handleNewMessage(const drogon::WebSocketConnectionPtr& connection, std::string&& message,
                          const drogon::WebSocketMessageType& type) override;
    void handleConnectionClosed(const drogon::WebSocketConnectionPtr& connection) override;

    WS_PATH_LIST_BEGIN
    WS_PATH_LIST_END

  private:
    std::shared_ptr<const http::HttpBinding> binding_;
};

/// The largest WebSocket message read (tungstenite's limit).
inline constexpr std::size_t max_websocket_message_size = 64 * 1024 * 1024;

/// Serves `binding` from `app`: sets its default handler, the WebSocket
/// controller for its Things, the body and message size limits, the `server`
/// and `date` headers, and the advice that lets streams run on closing
/// connections. Call before `app.run()`, once per application. Listeners,
/// threads and shutdown stay the application's.
std::shared_ptr<Handler> mount(std::shared_ptr<const http::HttpBinding> binding,
                               drogon::HttpAppFramework& app = drogon::app());
/// Answers the WebSocket requests that `claims` and that aren't a Thing's
/// WebSocket as `binding` answers their path (as teta-wot does), rather than
/// with Drogon's 404 page. `mount` claims `{prefix}/{other}/ws`; a server that
/// owns the whole application (ThingServer) claims every request.
void answer_upgrades(std::shared_ptr<const http::HttpBinding> binding, drogon::HttpAppFramework& app,
                     std::function<bool(const http::Request&)> claims);

/// Serves `service` from `app`, as `mount` serves a binding.
std::shared_ptr<Handler> mount(Service service, drogon::HttpAppFramework& app = drogon::app());

/// Answers every request with `handler` before Drogon routes it, but those
/// that `passes` lets through (the Things' WebSockets, for their
/// controller): for a server that owns the whole application (ThingServer,
/// the fallback server), so that none of Drogon's own routing applies, its
/// static files from the working directory in particular. `mount` alone
/// leaves an application's routes, and its static files, before the
/// binding.
void answer_all(std::shared_ptr<Handler> handler, drogon::HttpAppFramework& app,
                std::function<bool(const drogon::HttpRequest&)> passes = {});

}  // namespace teta::wot::drogon_adapter
