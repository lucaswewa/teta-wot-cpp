// A Thing's WebSocket: a port of the WebSocket half of teta-wot-http's
// `observe.rs`.
//
// - one broker subscription per connection, which each
//   `addPropertyObservation`, `addActionObservation` and
//   `addEventSubscription` message extends;
// - `propertyStatus`, `actionStatus` (with its `"action name"` key) and
//   `event` messages;
// - an error response in the Web Thing Protocol's style for a name that
//   isn't there (404) or isn't observable (403), after which the rest of
//   that message's names are skipped;
// - other message types are ignored; a message that isn't a JSON object with
//   a `messageType`, or whose `data` isn't an object, is logged and ignored.
//
// Notifications are sent by the publishing thread, as long as the transport
// keeps up; the socket closes (1001) when the broker closes at shutdown.

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "render.hpp"
#include "teta/wot/broker.hpp"
#include "teta/wot/http/message.hpp"
#include "teta/wot/http/websocket.hpp"
#include "teta/wot/runtime.hpp"

namespace teta::wot::http::detail {

/// Where the error `type` URIs of the Web Thing Protocol live.
inline constexpr std::string_view webthing_error_url = "https://w3c.github.io/web-thing-protocol/errors";

/// The close code for a server going away (RFC 6455).
inline constexpr std::uint16_t going_away = 1001;

/// The Thing whose WebSocket `request` opens, if it is a WebSocket upgrade
/// (`Upgrade: websocket`) of `GET {prefix}/{thing}/ws` for one of the
/// runtime's Things.
const ThingHandle* websocket_thing(const Runtime& runtime, std::string_view prefix, const Request& request);

/// What a client observes, as the dialect names it.
enum class Target { property, action, event };

/// Why a name can't be observed.
enum class Refusal { not_found, not_observable };

/// The error response for a name that can't be observed
/// (`observation_error_response`).
json::Value error_response(std::string_view name, Target target, Refusal refusal);

/// A notification, as the socket sends it.
json::Value relay(const Message& message);

/// Serves one connection to `thing`'s WebSocket.
std::shared_ptr<WebSocketSession> open_session(std::shared_ptr<MessageBroker> broker,
                                               std::shared_ptr<const ThingHandle> thing,
                                               std::shared_ptr<WebSocketPeer> peer);

}  // namespace teta::wot::http::detail
