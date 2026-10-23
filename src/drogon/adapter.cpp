#include <atomic>
#include <cctype>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "teta/wot/drogon.hpp"

namespace teta::wot::drogon_adapter {

namespace {

// Whether the client wants the connection kept open (Drogon's
// `HttpRequestImpl::keepAlive` isn't public).
bool keeps_alive(const drogon::HttpRequest& request) {
    std::string value = request.getHeader("connection");
    for (auto& c : value) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (request.version() == drogon::Version::kHttp10) {
        return value == "keep-alive";
    }
    return value != "close";
}

// Whether a connection's client keeps up with what is sent to it. trantor
// neither says when its output has drained nor lets other threads read how
// much is left, so the gate counts what it hands over and, once more than
// `high_water_mark` may be unsent, checks the connection's `bytesSent()` on
// its loop until less than `low_water_mark` is left; then it resumes the
// producer.
class FlowGate final : public std::enable_shared_from_this<FlowGate> {
  public:
    static constexpr std::uint64_t high_water_mark = 256 * 1024;
    static constexpr std::uint64_t low_water_mark = 64 * 1024;
    static constexpr double check_interval = 0.02;  // seconds

    // On the connection's loop, before anything is handed over.
    explicit FlowGate(const std::weak_ptr<trantor::TcpConnection>& connection) : connection_(connection) {
        if (const auto c = connection.lock(); c && c->getLoop()->isInLoopThread()) {
            base_ = c->bytesSent();
        }
    }

    bool ready() const { return !congested_.load(); }

    void set_resume(std::function<void()> resume) {
        std::lock_guard lock(mutex_);
        resume_ = std::move(resume);
    }

    // Counts `bytes` handed to the connection.
    void sent(std::size_t bytes) {
        const auto queued = queued_ += bytes;
        if (queued - acknowledged_.load() <= high_water_mark || checking_.exchange(true)) {
            return;
        }
        congested_ = true;
        if (const auto connection = connection_.lock()) {
            connection->getLoop()->queueInLoop([self = shared_from_this()] { self->check(); });
        } else {
            release();
        }
    }

  private:
    // On the connection's loop.
    void check() {
        const auto connection = connection_.lock();
        if (!connection || connection->disconnected() || !base_) {
            release();  // the producer finds out on its next send
            return;
        }
        const std::uint64_t written = connection->bytesSent() - *base_;
        const std::uint64_t queued = queued_.load();
        const std::uint64_t pending = queued > written ? queued - written : 0;
        acknowledged_ = queued - pending;
        if (pending < low_water_mark) {
            release();
            return;
        }
        connection->getLoop()->runAfter(check_interval, [self = shared_from_this()] { self->check(); });
    }

    void release() {
        congested_ = false;
        checking_ = false;
        std::function<void()> resume;
        {
            std::lock_guard lock(mutex_);
            resume = resume_;
        }
        if (resume) {
            resume();
        }
    }

    std::weak_ptr<trantor::TcpConnection> connection_;
    std::optional<std::uint64_t> base_;  // the loop's
    std::atomic<std::uint64_t> queued_{0};
    std::atomic<std::uint64_t> acknowledged_{0};
    std::atomic<bool> congested_{false};
    std::atomic<bool> checking_{false};
    std::mutex mutex_;
    std::function<void()> resume_;
};

// A streamed body's chunks, sent through Drogon's response stream.
class DrogonSink final : public http::StreamSink {
  public:
    DrogonSink(drogon::ResponseStreamPtr stream, std::weak_ptr<trantor::TcpConnection> connection,
               bool keep_alive)
        : stream_(std::move(stream)),
          connection_(std::move(connection)),
          keep_alive_(keep_alive),
          gate_(std::make_shared<FlowGate>(connection_)) {}
    ~DrogonSink() override { close(); }

    bool send(std::string_view chunk) override {
        std::lock_guard lock(mutex_);
        if (!stream_) {
            return false;
        }
        if (chunk.empty()) {
            return true;  // an empty chunk would end the body
        }
        if (!stream_->send(std::string(chunk))) {
            return false;
        }
        gate_->sent(chunk.size());
        return true;
    }

    bool ready() const override { return gate_->ready(); }
    void set_resume(std::function<void()> resume) override { gate_->set_resume(std::move(resume)); }

    void close() override {
        std::lock_guard lock(mutex_);
        if (!stream_) {
            return;
        }
        stream_->close();
        stream_.reset();
        if (!keep_alive_) {
            // As Drogon would have done, had it streamed on a closing
            // connection at all.
            if (const auto connection = connection_.lock()) {
                connection->getLoop()->queueInLoop([connection] { connection->shutdown(); });
            }
        }
    }

  private:
    std::mutex mutex_;
    drogon::ResponseStreamPtr stream_;
    std::weak_ptr<trantor::TcpConnection> connection_;
    bool keep_alive_;
    std::shared_ptr<FlowGate> gate_;
};

// A WebSocket connection, as the binding sends through it.
class DrogonPeer final : public http::WebSocketPeer {
  public:
    DrogonPeer(const drogon::WebSocketConnectionPtr& socket,
               const std::weak_ptr<trantor::TcpConnection>& connection)
        : socket_(socket), gate_(std::make_shared<FlowGate>(connection)) {}

    bool send_text(std::string_view text) override {
        const auto socket = socket_.lock();
        if (!socket || socket->disconnected()) {
            return false;
        }
        socket->send(text, drogon::WebSocketMessageType::Text);
        gate_->sent(text.size());
        return true;
    }

    void close(std::uint16_t code, std::string_view reason) override {
        if (const auto socket = socket_.lock()) {
            socket->shutdown(static_cast<drogon::CloseCode>(code), std::string(reason));
        }
    }

    bool ready() const override { return gate_->ready(); }
    void set_resume(std::function<void()> resume) override { gate_->set_resume(std::move(resume)); }

  private:
    std::weak_ptr<drogon::WebSocketConnection> socket_;
    std::shared_ptr<FlowGate> gate_;
};

}  // namespace

ThingSockets::ThingSockets(std::shared_ptr<const http::HttpBinding> binding) : binding_(std::move(binding)) {}

void ThingSockets::handleNewConnection(const drogon::HttpRequestPtr& request,
                                       const drogon::WebSocketConnectionPtr& connection) {
    const auto converted = to_request(*request);
    if (!binding_->is_websocket(converted)) {
        connection->forceClose();
        return;
    }
    auto peer = std::make_shared<DrogonPeer>(connection, request->getConnectionPtr());
    connection->setContext(binding_->open_websocket(converted, std::move(peer)));
}

void ThingSockets::handleNewMessage(const drogon::WebSocketConnectionPtr& connection, std::string&& message,
                                    const drogon::WebSocketMessageType& type) {
    const auto session = connection->getContext<http::WebSocketSession>();
    if (!session) {
        return;
    }
    if (type == drogon::WebSocketMessageType::Text) {
        session->on_text(message);
    } else if (type == drogon::WebSocketMessageType::Binary) {
        session->on_binary();
    }
}

void ThingSockets::handleConnectionClosed(const drogon::WebSocketConnectionPtr& connection) {
    if (const auto session = connection->getContext<http::WebSocketSession>()) {
        session->on_closed();
        connection->clearContext();
    }
}

http::Request to_request(const drogon::HttpRequest& request) {
    http::Request out;
    // Drogon answers a HEAD as a GET (and sends no body); it is a HEAD here.
    out.method = request.isHead() ? "HEAD" : request.methodString();
    out.path = request.getOriginalPath();
    if (const auto& query = request.query(); !query.empty()) {
        out.query = query;
    }
    for (const auto& [name, value] : request.headers()) {
        out.headers.add(name, value);
    }
    out.body = std::string(request.body());
    return out;
}

drogon::HttpResponsePtr to_response(http::Response response, const drogon::HttpRequestPtr& request) {
    drogon::HttpResponsePtr out;
    if (response.stream) {
        const bool keep_alive = keeps_alive(*request);
        std::weak_ptr<trantor::TcpConnection> connection = request->getConnectionPtr();
        out = drogon::HttpResponse::newAsyncStreamResponse(
            [producer = std::move(response.stream), connection,
             keep_alive](drogon::ResponseStreamPtr stream) {
                producer(std::make_shared<DrogonSink>(std::move(stream), connection, keep_alive));
            },
            true);
    } else {
        out = drogon::HttpResponse::newHttpResponse();
        out->setBody(std::move(response.body));
    }
    out->setCustomStatusCode(response.status, http::reason_phrase(response.status));
    out->setContentTypeCode(drogon::CT_NONE);
    for (auto& [name, value] : response.headers) {
        if (http::equals_ignoring_case(name, "content-type")) {
            out->setContentTypeString(value);
        } else {
            out->addHeader(name, value);
        }
    }
    return out;
}

Handler::Handler(std::shared_ptr<const http::HttpBinding> binding)
    : service_([binding = std::move(binding)](http::Request request, http::HttpBinding::Respond respond) {
          binding->handle(std::move(request), std::move(respond));
      }) {}

Handler::Handler(Service service) : service_(std::move(service)) {}

void Handler::operator()(const drogon::HttpRequestPtr& request,
                         std::function<void(const drogon::HttpResponsePtr&)>&& callback) const {
    ++*in_flight_;
    service_(to_request(*request),
             [request, callback = std::move(callback), in_flight = in_flight_](http::Response response) {
                 callback(to_response(std::move(response), request));
                 --*in_flight;
             });
}

std::shared_ptr<Handler> mount(std::shared_ptr<const http::HttpBinding> binding,
                               drogon::HttpAppFramework& app) {
    auto handler = mount(Service([binding](http::Request request, http::HttpBinding::Respond respond) {
                             binding->handle(std::move(request), std::move(respond));
                         }),
                         app);
    // The WebSockets of the binding's Things: `{prefix}/(a|b|…)/ws`. Other
    // paths ending in `/ws` reach the default handler, which answers 404.
    std::string names;
    for (const auto& thing : binding->runtime()->things()) {
        names += (names.empty() ? "" : "|") + thing->name();
    }
    if (!names.empty()) {
        std::string prefix;
        for (const char c : binding->options().api_prefix) {
            if (std::string_view(".^$|()[]{}*+?\\").find(c) != std::string_view::npos) {
                prefix += '\\';
            }
            prefix += c;
        }
        app.registerController(std::make_shared<ThingSockets>(binding));
        app.registerWebSocketControllerRegex("^" + prefix + "/(" + names + ")/ws$",
                                             ThingSockets::classTypeName());
    }
    // A WebSocket request for `{prefix}/{other}/ws` is answered as teta-wot
    // answers that path (a 404), not with Drogon's page.
    answer_upgrades(binding, app, [prefix = binding->options().api_prefix](const http::Request& request) {
        return request.path.starts_with(prefix + "/") && request.path.ends_with("/ws");
    });
    app.setClientMaxWebSocketMessageSize(max_websocket_message_size);
    return handler;
}

void answer_upgrades(std::shared_ptr<const http::HttpBinding> binding, drogon::HttpAppFramework& app,
                     std::function<bool(const http::Request&)> claims) {
    app.registerPreRoutingAdvice([binding, claims = std::move(claims)](const drogon::HttpRequestPtr& request,
                                                                       drogon::AdviceCallback&& answer,
                                                                       drogon::AdviceChainCallback&& pass) {
        if (!http::equals_ignoring_case(request->getHeader("upgrade"), "websocket")) {
            pass();
            return;
        }
        auto converted = to_request(*request);
        if (binding->is_websocket(converted) || !claims(converted)) {
            pass();
            return;
        }
        binding->handle(std::move(converted), [request, answer = std::move(answer)](http::Response response) {
            answer(to_response(std::move(response), request));
        });
    });
}

void answer_all(std::shared_ptr<Handler> handler, drogon::HttpAppFramework& app,
                std::function<bool(const drogon::HttpRequest&)> passes) {
    app.registerPreRoutingAdvice([handler = std::move(handler), passes = std::move(passes)](
                                     const drogon::HttpRequestPtr& request, drogon::AdviceCallback&& answer,
                                     drogon::AdviceChainCallback&& pass) {
        if (passes && passes(*request)) {
            pass();
            return;
        }
        (*handler)(request, std::move(answer));
    });
}

std::shared_ptr<Handler> mount(Service service, drogon::HttpAppFramework& app) {
    auto handler = std::make_shared<Handler>(std::move(service));
    app.setDefaultHandler([handler](const drogon::HttpRequestPtr& request,
                                    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
           (*handler)(request, std::move(callback));
       })
        // Otherwise Drogon asks for `/index.html` when `/` is requested.
        .setHomePage("")
        .setClientMaxBodySize(max_body_size)
        .setClientMaxMemoryBodySize(max_body_size)
        .enableServerHeader(false)
        .enableDateHeader(true)
        .registerPreSendingAdvice(
            [](const drogon::HttpRequestPtr& request, const drogon::HttpResponsePtr& response) {
                // Drogon won't start an async stream on a connection it will
                // close: keep it open, and let the stream close it.
                if (response->asyncStreamCallback() && !keeps_alive(*request)) {
                    response->setCloseConnection(false);
                    response->addHeader("connection", "close");
                }
            });
    return handler;
}

}  // namespace teta::wot::drogon_adapter
