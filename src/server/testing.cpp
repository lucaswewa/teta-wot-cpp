#include "teta/wot/testing.hpp"

#include <algorithm>
#include <condition_variable>
#include <future>
#include <mutex>
#include <stdexcept>
#include <variant>

namespace teta::wot::testing {

namespace detail {

/// The sink a streamed answer is sent into, read by the test.
class ChunkQueue final : public http::StreamSink {
  public:
    bool send(std::string_view chunk) override {
        std::lock_guard lock(mutex_);
        if (closed_ || gone_) {
            return false;
        }
        chunks_.emplace_back(chunk);
        available_.notify_all();
        return true;
    }

    void close() override {
        std::lock_guard lock(mutex_);
        closed_ = true;
        available_.notify_all();
    }

    /// The client has gone away: later sends are refused.
    void abandon() {
        std::lock_guard lock(mutex_);
        gone_ = true;
    }

    /// The next chunk, waiting up to `timeout`: a chunk, or "" at the end of
    /// the body, or nullopt on a timeout.
    std::optional<std::string> next(std::chrono::nanoseconds timeout) {
        std::unique_lock lock(mutex_);
        if (!available_.wait_for(lock, timeout, [&] { return !chunks_.empty() || closed_; })) {
            return std::nullopt;
        }
        if (chunks_.empty()) {
            return std::string();
        }
        std::string chunk = std::move(chunks_.front());
        chunks_.pop_front();
        return chunk;
    }

  private:
    std::mutex mutex_;
    std::condition_variable available_;
    std::deque<std::string> chunks_;
    bool closed_ = false;
    bool gone_ = false;
};

/// A test WebSocket's connection: what the server sends, read by the test.
class SocketQueue final : public http::WebSocketPeer {
  public:
    bool send_text(std::string_view text) override {
        std::lock_guard lock(mutex_);
        if (close_ || gone_) {
            return false;
        }
        messages_.emplace_back(text);
        available_.notify_all();
        return true;
    }

    void close(std::uint16_t code, std::string_view reason) override {
        std::lock_guard lock(mutex_);
        if (!close_) {
            close_ = TestClose{code, std::string(reason)};
        }
        available_.notify_all();
    }

    /// The client has gone away: later sends are refused.
    void abandon() {
        std::lock_guard lock(mutex_);
        gone_ = true;
    }

    /// The next message, or the server's close once every message is read;
    /// nullopt on a timeout.
    std::optional<std::variant<std::string, TestClose>> next(std::chrono::nanoseconds timeout) {
        std::unique_lock lock(mutex_);
        if (!available_.wait_for(lock, timeout, [&] { return !messages_.empty() || close_; })) {
            return std::nullopt;
        }
        if (messages_.empty()) {
            return std::variant<std::string, TestClose>(*close_);
        }
        std::string message = std::move(messages_.front());
        messages_.pop_front();
        return std::variant<std::string, TestClose>(std::move(message));
    }

  private:
    std::mutex mutex_;
    std::condition_variable available_;
    std::deque<std::string> messages_;
    std::optional<TestClose> close_;
    bool gone_ = false;
};

}  // namespace detail

// ---- TestWebSocket ------------------------------------------------------------------------

TestWebSocket::TestWebSocket(TestWebSocket&&) noexcept = default;
TestWebSocket& TestWebSocket::operator=(TestWebSocket&&) noexcept = default;

TestWebSocket::~TestWebSocket() {
    close();
}

void TestWebSocket::send_json(const json::Value& value) {
    send_text(json::dump(value));
}

void TestWebSocket::send_text(std::string_view text) {
    if (!session_) {
        throw std::runtime_error("the WebSocket is closed");
    }
    session_->on_text(text);
}

json::Value TestWebSocket::receive_json() {
    const auto next = queue_->next(receive_timeout);
    if (!next) {
        throw std::runtime_error("no WebSocket message within the timeout");
    }
    if (const auto* close = std::get_if<TestClose>(&*next)) {
        throw std::runtime_error("the WebSocket closed: " + std::to_string(close->code) + " " +
                                 close->reason);
    }
    const auto& text = std::get<std::string>(*next);
    try {
        return json::parse(text);
    } catch (const json::ParseError&) {
        return text;
    }
}

std::optional<json::Value> TestWebSocket::receive_json_within(std::chrono::nanoseconds timeout) {
    const auto next = queue_->next(timeout);
    if (!next || std::holds_alternative<TestClose>(*next)) {
        return std::nullopt;
    }
    const auto& text = std::get<std::string>(*next);
    try {
        return json::parse(text);
    } catch (const json::ParseError&) {
        return text;
    }
}

TestClose TestWebSocket::closed() {
    const auto deadline = std::chrono::steady_clock::now() + receive_timeout;
    for (;;) {
        const auto left = deadline - std::chrono::steady_clock::now();
        const auto next = queue_->next(std::max(left, std::chrono::steady_clock::duration::zero()));
        if (!next) {
            throw std::runtime_error("the WebSocket didn't close within the timeout");
        }
        if (const auto* close = std::get_if<TestClose>(&*next)) {
            return *close;
        }
    }
}

void TestWebSocket::close() {
    if (session_) {
        queue_->abandon();
        session_->on_closed();
        session_.reset();
    }
}

namespace {

std::optional<std::string> first_header(const http::Headers& headers, std::string_view name) {
    if (const auto value = headers.get(name)) {
        return std::string(*value);
    }
    return std::nullopt;
}

}  // namespace

json::Value TestResponse::json() const {
    try {
        return json::parse(body);
    } catch (const json::ParseError&) {
        return nullptr;
    }
}

std::optional<std::string> TestResponse::header(std::string_view name) const {
    return first_header(headers, name);
}

// ---- TestEventStream ------------------------------------------------------------------------

TestEventStream::TestEventStream(TestEventStream&&) noexcept = default;
TestEventStream& TestEventStream::operator=(TestEventStream&&) noexcept = default;

TestEventStream::~TestEventStream() {
    if (chunks_) {
        chunks_->abandon();
    }
}

std::optional<std::string> TestEventStream::header(std::string_view name) const {
    return first_header(headers, name);
}

std::optional<json::Value> TestEventStream::next() {
    if (auto message = next_message()) {
        return std::move(message->data);
    }
    return std::nullopt;
}

std::optional<TestSseMessage> TestEventStream::next_message() {
    return receive(receive_timeout, true);
}

std::optional<json::Value> TestEventStream::next_within(std::chrono::nanoseconds timeout) {
    if (auto message = receive(timeout, false)) {
        return std::move(message->data);
    }
    return std::nullopt;
}

std::optional<TestSseMessage> TestEventStream::receive(std::chrono::nanoseconds timeout, bool strict) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        if (const auto end = buffer_.find("\n\n"); end != std::string::npos) {
            const std::string event = buffer_.substr(0, end + 2);
            buffer_.erase(0, end + 2);
            std::optional<std::string> data;
            TestSseMessage message;
            for (std::size_t start = 0; start < event.size();) {
                const auto newline = event.find('\n', start);
                std::string_view line(event.data() + start, newline - start);
                start = newline + 1;
                auto field = [&](std::string_view name) -> std::optional<std::string> {
                    if (!line.starts_with(name)) {
                        return std::nullopt;
                    }
                    auto value = line.substr(name.size());
                    if (value.starts_with(' ')) {
                        value.remove_prefix(1);
                    }
                    return std::string(value);
                };
                if (auto value = field("data:")) {
                    data = data ? *data + "\n" + *value : *value;
                } else if (auto name = field("event:"); name && !message.event) {
                    message.event = std::move(name);
                } else if (auto id = field("id:"); id && !message.id) {
                    message.id = std::move(id);
                }
            }
            if (!data) {
                continue;  // a comment, such as a keep-alive
            }
            try {
                message.data = json::parse(*data);
            } catch (const json::ParseError&) {
                message.data = *data;
            }
            return message;
        }
        const auto left = deadline - std::chrono::steady_clock::now();
        auto chunk = chunks_ ? chunks_->next(std::max(left, std::chrono::steady_clock::duration::zero()))
                             : std::optional<std::string>(std::string());
        if (!chunk) {
            if (strict) {
                throw std::runtime_error("no server-sent event within the timeout");
            }
            return std::nullopt;
        }
        if (chunk->empty()) {
            return std::nullopt;  // the end of the stream
        }
        for (std::size_t i = 0; i < chunk->size(); ++i) {
            if ((*chunk)[i] == '\r' && i + 1 < chunk->size() && (*chunk)[i + 1] == '\n') {
                continue;
            }
            buffer_ += (*chunk)[i];
        }
    }
}

// ---- TestStream ---------------------------------------------------------------------------

TestStream::TestStream(TestStream&&) noexcept = default;
TestStream& TestStream::operator=(TestStream&&) noexcept = default;

TestStream::~TestStream() {
    if (chunks_) {
        chunks_->abandon();
    }
}

std::optional<std::string> TestStream::header(std::string_view name) const {
    return first_header(headers, name);
}

std::optional<std::string> TestStream::next_chunk() {
    return receive(receive_timeout, true);
}

std::optional<std::string> TestStream::next_chunk_within(std::chrono::nanoseconds timeout) {
    return receive(timeout, false);
}

std::optional<std::string> TestStream::receive(std::chrono::nanoseconds timeout, bool strict) {
    if (!chunks_) {
        // Not a stream: its body, then the end.
        return std::exchange(body_, std::nullopt);
    }
    auto chunk = chunks_->next(timeout);
    if (!chunk) {
        if (strict) {
            throw std::runtime_error("no chunk of the stream within the timeout");
        }
        return std::nullopt;
    }
    if (chunk->empty()) {
        return std::nullopt;  // the end of the stream
    }
    return chunk;
}

// ---- TestClient ---------------------------------------------------------------------------

TestClient TestClient::start(const ThingServer& server) {
    server.runtime()->start();
    return TestClient(server.binding());
}

TestClient TestClient::with_host(std::string host) const {
    TestClient client = *this;
    client.host_ = std::move(host);
    return client;
}

TestClient TestClient::with_header(std::string name, std::string value) const {
    TestClient client = *this;
    client.headers_.emplace_back(std::move(name), std::move(value));
    return client;
}

http::Request TestClient::make(std::string method, std::string_view path) const {
    http::Request request = http::Request::make(std::move(method), path);
    request.headers.add("host", host_);
    for (const auto& [name, value] : headers_) {
        request.headers.add(name, value);
    }
    return request;
}

http::Response TestClient::send(http::Request request) const {
    auto answer = std::make_shared<std::promise<http::Response>>();
    auto future = answer->get_future();
    binding_->handle(std::move(request),
                     [answer](http::Response response) { answer->set_value(std::move(response)); });
    return future.get();
}

TestResponse TestClient::request(std::string method, std::string_view path,
                                 const std::vector<std::pair<std::string, std::string>>& headers,
                                 std::optional<std::string> body) const {
    const bool head = method == "HEAD";
    http::Request request = make(std::move(method), path);
    for (const auto& [name, value] : headers) {
        request.headers.add(name, value);
    }
    if (body) {
        request.body = std::move(*body);
    }
    http::Response response = send(std::move(request));
    TestResponse out{response.status, std::move(response.headers), std::move(response.body)};
    if (response.stream) {
        // Read to the end.
        auto chunks = std::make_shared<detail::ChunkQueue>();
        response.stream(chunks);
        while (auto chunk = chunks->next(std::chrono::hours(24))) {
            if (chunk->empty()) {
                break;
            }
            out.body += *chunk;
        }
    }
    if (head) {
        // A HEAD's headers are those of a GET, including its length.
        if (!out.headers.contains("content-length")) {
            out.headers.set("content-length", std::to_string(out.body.size()));
        }
        out.body.clear();
    }
    return out;
}

TestResponse TestClient::get(std::string_view path) const {
    return request("GET", path);
}

TestResponse TestClient::del(std::string_view path) const {
    return request("DELETE", path);
}

TestResponse TestClient::put_json(std::string_view path, const json::Value& body) const {
    return request("PUT", path, {{"content-type", "application/json"}}, json::dump(body));
}

TestResponse TestClient::post_json(std::string_view path, const std::optional<json::Value>& body) const {
    if (!body) {
        return request("POST", path);
    }
    return request("POST", path, {{"content-type", "application/json"}}, json::dump(*body));
}

TestEventStream TestClient::events(std::string_view path) const {
    http::Request request = make("GET", path);
    request.headers.add("accept", "text/event-stream");
    http::Response response = send(std::move(request));
    TestEventStream stream;
    stream.status = response.status;
    stream.headers = std::move(response.headers);
    if (response.stream) {
        stream.chunks_ = std::make_shared<detail::ChunkQueue>();
        response.stream(stream.chunks_);
    } else {
        // Not a stream: its body, then the end.
        stream.buffer_ = std::move(response.body);
    }
    return stream;
}

TestStream TestClient::stream(std::string_view path) const {
    http::Response response = send(make("GET", path));
    TestStream stream;
    stream.status = response.status;
    stream.headers = std::move(response.headers);
    if (response.stream) {
        stream.chunks_ = std::make_shared<detail::ChunkQueue>();
        response.stream(stream.chunks_);
    } else if (!response.body.empty()) {
        stream.body_ = std::move(response.body);
    }
    return stream;
}

TestWebSocket TestClient::websocket(std::string_view path) const {
    http::Request request = make("GET", path);
    request.headers.add("connection", "Upgrade");
    request.headers.add("upgrade", "websocket");
    request.headers.add("sec-websocket-version", "13");
    request.headers.add("sec-websocket-key", "dGhlIHNhbXBsZSBub25jZQ==");
    if (!binding_->is_websocket(request)) {
        throw std::runtime_error("the WebSocket handshake failed: " + std::string(path) +
                                 " isn't a Thing's WebSocket");
    }
    TestWebSocket socket;
    socket.queue_ = std::make_shared<detail::SocketQueue>();
    socket.session_ = binding_->open_websocket(request, socket.queue_);
    return socket;
}

void TestClient::stop() const {
    binding_->runtime()->shutdown(std::chrono::seconds(1));
}

// ---- Harness ------------------------------------------------------------------------------

detail::TemporaryFolder::TemporaryFolder()
    : path_(std::filesystem::temp_directory_path() / ("teta-wot-" + Uuid::v4().to_string())) {
    std::filesystem::create_directories(path_);
}

detail::TemporaryFolder::~TemporaryFolder() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
}

}  // namespace teta::wot::testing
