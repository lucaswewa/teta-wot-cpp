// Transport-neutral WebSockets: what the HTTP binding needs from a transport
// to serve a Thing's WebSocket, `{prefix}/{thing}/ws` (ADR-0004).
//
// The transport accepts the upgrade (`HttpBinding::is_websocket` tells which
// requests are WebSockets), gives the binding a WebSocketPeer to send with,
// and passes what the client sends to the WebSocketSession the binding
// returns (`HttpBinding::open_websocket`).

#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

namespace teta::wot::http {

/// The transport's side of one WebSocket connection. Its functions may be
/// called from any thread.
class WebSocketPeer {
  public:
    virtual ~WebSocketPeer() = default;
    /// Sends a text message; false if the connection is gone.
    virtual bool send_text(std::string_view text) = 0;
    /// Starts the close handshake with a code and a reason.
    virtual void close(std::uint16_t code, std::string_view reason) = 0;
    /// Whether the client keeps up: false while the transport holds more
    /// unsent data than it should. A session takes no more notifications
    /// from the broker until it is ready again (and the broker drops what
    /// doesn't fit in the subscription's buffer).
    virtual bool ready() const { return true; }
    /// Calls `resume` whenever the connection becomes ready again.
    virtual void set_resume(std::function<void()> resume) { (void)resume; }
};

/// The binding's side of one WebSocket connection: the transport calls it as
/// the client sends.
class WebSocketSession {
  public:
    virtual ~WebSocketSession() = default;
    /// A text message.
    virtual void on_text(std::string_view text) = 0;
    /// A binary message.
    virtual void on_binary() = 0;
    /// The connection is gone (closed by either side).
    virtual void on_closed() = 0;
};

}  // namespace teta::wot::http
