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
// - no `server` header; a `date` header.

#pragma once

// Drogon includes <windows.h>, whose `min` and `max` macros break
// `std::numeric_limits<T>::max()`.
#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <drogon/HttpAppFramework.h>

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

/// Answers Drogon's requests with a binding.
class Handler {
  public:
    explicit Handler(std::shared_ptr<const http::HttpBinding> binding);

    void operator()(const drogon::HttpRequestPtr& request,
                    std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;

    /// Requests being answered (a streamed answer counts until its headers
    /// are sent).
    std::size_t in_flight() const { return in_flight_->load(); }

  private:
    std::shared_ptr<const http::HttpBinding> binding_;
    std::shared_ptr<std::atomic<std::size_t>> in_flight_ = std::make_shared<std::atomic<std::size_t>>(0);
};

/// Serves `binding` from `app`: sets its default handler, the body size
/// limit, the `server` and `date` headers, and the advice that lets streams
/// run on closing connections. Call before `app.run()`. Listeners, threads
/// and shutdown stay the application's.
std::shared_ptr<Handler> mount(std::shared_ptr<const http::HttpBinding> binding,
                               drogon::HttpAppFramework& app = drogon::app());

}  // namespace teta::wot::drogon_adapter
