// HTTP requests and responses, independent of any server (ADR-0004): what
// the HTTP binding answers, what custom endpoints receive and return, and
// what `TestClient` sends. The Drogon adapter converts them to and from
// Drogon's.

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "teta/wot/json.hpp"

namespace teta::wot::http {

/// Header fields, in order. Names are compared without regard to case, and
/// may repeat.
class Headers {
  public:
    using Field = std::pair<std::string, std::string>;

    /// The value of the first field named `name`.
    std::optional<std::string_view> get(std::string_view name) const;
    /// The values of every field named `name`.
    std::vector<std::string_view> get_all(std::string_view name) const;
    bool contains(std::string_view name) const { return get(name).has_value(); }
    /// Adds a field, after any others of the same name.
    Headers& add(std::string name, std::string value);
    /// Replaces every field named `name` with one.
    Headers& set(std::string name, std::string value);
    void remove(std::string_view name);

    const std::vector<Field>& fields() const { return fields_; }
    auto begin() const { return fields_.begin(); }
    auto end() const { return fields_.end(); }
    bool empty() const { return fields_.empty(); }

  private:
    std::vector<Field> fields_;
};

/// A status's reason phrase, as the `http` crate writes it (`Not Found`), or
/// "" for a status it doesn't name. Static storage.
std::string_view reason_phrase(int status);

/// Whether two header names (or other ASCII tokens) are equal without regard
/// to case.
bool equals_ignoring_case(std::string_view a, std::string_view b);

/// What a transport gives a streamed body to send its chunks through.
class StreamSink {
  public:
    virtual ~StreamSink() = default;
    /// Sends a chunk, from any thread; false once the client has gone away
    /// (or the sink is closed), when the producer should stop.
    virtual bool send(std::string_view chunk) = 0;
    /// Ends the body. Later sends are refused.
    virtual void close() = 0;
};

/// Starts a streamed body (server-sent events, MJPEG). The transport calls it
/// once, when it is ready for chunks: the producer keeps the sink, sends from
/// whatever threads produce the chunks, and closes it at the end. No thread
/// waits on a client.
using StreamProducer = std::function<void(std::shared_ptr<StreamSink>)>;

/// An HTTP request.
struct Request {
    /// The method as sent (`GET`, `PROPFIND`).
    std::string method = "GET";
    /// The path as sent, still percent-encoded (`/a%2Fb`), without the query.
    std::string path = "/";
    /// The query, without the `?`; none if the target has no `?`.
    std::optional<std::string> query;
    /// The scheme and authority of a target in absolute form
    /// (`GET http://host:8000/path`), if it was in that form.
    std::optional<std::string> scheme;
    std::optional<std::string> authority;
    Headers headers;
    std::string body;

    /// A request for `target`: a path with an optional query (`/x?y=1`), or
    /// an absolute URL (`http://host/x`).
    static Request make(std::string method, std::string_view target);
    /// The path and query.
    std::string target() const;
};

/// An HTTP response.
struct Response {
    int status = 200;
    Headers headers;
    std::string body;
    /// When set, the body is streamed by this producer instead of `body`.
    StreamProducer stream;

    /// `value` as JSON (serde_json's text), as `application/json`.
    static Response json(int status, const teta::wot::json::Value& value);
    /// Text, as `text/plain; charset=utf-8` (what axum makes of a `String`).
    static Response text(std::string text, int status = 200);
    /// A body of any media type.
    static Response bytes(std::string body, std::string content_type, int status = 200);
    /// No body and no `content-type`, such as a 204 (what axum makes of a bare
    /// status code).
    static Response empty(int status);
};

/// The handler of a custom endpoint: runs on a worker thread, as all Thing
/// code does.
using EndpointHandler = std::function<Response(const Request&)>;

}  // namespace teta::wot::http
