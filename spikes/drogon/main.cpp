// Phase 0 spike: can Drogon be a transport only, with every answer ours?
//
// Answers questions recorded in docs/notes/phase-00-baseline.md:
//
// - one default handler receives every method and path (no Drogon routing);
// - a regex WebSocket controller serves `/{thing}/ws`, and a plain GET there
//   falls through to the default handler;
// - async stream responses carry SSE and multipart/x-mixed-replace;
// - a response can be completed from a worker thread;
// - the `server` and `date` headers can be controlled;
// - shutdown can drain: new connections closed, in-flight requests finished,
//   then the loops quit.
//
// It is not library code: Phase 4 builds the real adapter from what it shows.

#include <drogon/WebSocketController.h>
#include <drogon/drogon.h>

#include <atomic>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

using namespace drogon;
using namespace std::chrono_literals;

namespace {

std::atomic<bool> draining{false};
std::atomic<int> in_flight{0};

HttpResponsePtr text(HttpStatusCode code, const std::string& body, const std::string& type) {
    auto response = HttpResponse::newHttpResponse();
    response->setStatusCode(code);
    response->setBody(body);
    response->setContentTypeString(type);
    return response;
}

// Ends a stream. If the client asked for the connection to close, it is shut
// down after the body, as Drogon would have done had it streamed at all (see
// the pre-sending advice in main).
void finish(ResponseStreamPtr& stream, const std::weak_ptr<trantor::TcpConnection>& connection,
            bool keep_alive) {
    stream->close();
    if (!keep_alive) {
        if (auto conn = connection.lock()) {
            conn->getLoop()->queueInLoop([conn]() { conn->shutdown(); });
        }
    }
}

// Whether the client wants the connection kept open (HttpRequestImpl's
// keepAlive() isn't public).
bool keeps_alive(const HttpRequestPtr& request) {
    std::string value = request->getHeader("connection");
    for (auto& c : value) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (request->version() == Version::kHttp10) {
        return value == "keep-alive";
    }
    return value != "close";
}

std::string method_name(const HttpRequestPtr& request) {
    return std::string(request->methodString());
}

void handle(const HttpRequestPtr& request, std::function<void(const HttpResponsePtr&)>&& callback) {
    const std::string path = request->path();
    const auto connection = request->getConnectionPtr();
    const bool keep_alive = keeps_alive(request);
    if (path == "/sse") {
        auto response = HttpResponse::newAsyncStreamResponse(
            [connection, keep_alive](ResponseStreamPtr stream) {
                std::thread([stream = std::move(stream), connection, keep_alive]() mutable {
                    for (int i = 0; i < 3; ++i) {
                        std::this_thread::sleep_for(100ms);
                        if (!stream->send("data: " + std::to_string(i) + "\n\n")) {
                            return;
                        }
                    }
                    finish(stream, connection, keep_alive);
                }).detach();
            },
            true);
        response->setContentTypeString("text/event-stream; charset=utf-8");
        response->addHeader("cache-control", "no-cache");
        callback(response);
        return;
    }
    if (path == "/mjpeg") {
        auto response = HttpResponse::newAsyncStreamResponse(
            [connection, keep_alive](ResponseStreamPtr stream) {
                std::thread([stream = std::move(stream), connection, keep_alive]() mutable {
                    for (int i = 0; i < 3; ++i) {
                        std::string frame = "\xFF\xD8 frame " + std::to_string(i) + " \xFF\xD9";
                        std::string part = "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: " +
                                           std::to_string(frame.size()) + "\r\n\r\n" + frame + "\r\n";
                        if (!stream->send(part)) {
                            return;
                        }
                        std::this_thread::sleep_for(50ms);
                    }
                    finish(stream, connection, keep_alive);
                }).detach();
            },
            true);
        response->setContentTypeString("multipart/x-mixed-replace; boundary=frame");
        callback(response);
        return;
    }
    if (path == "/slow") {
        // Completed from a worker thread, as Thing code will be.
        const int ms = std::atoi(request->getParameter("ms").c_str());
        ++in_flight;
        std::thread([ms, callback = std::move(callback)]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
            callback(text(k200OK, "slept " + std::to_string(ms) + " ms", "text/plain; charset=utf-8"));
            --in_flight;
        }).detach();
        return;
    }
    if (path == "/body") {
        callback(text(k200OK, std::to_string(request->body().size()), "text/plain; charset=utf-8"));
        return;
    }
    // Everything else: what the handler saw, so the probe can compare.
    std::string seen = "{\"method\":\"" + method_name(request) + "\",\"path\":\"" + path +
                       "\",\"original_path\":\"" + request->getOriginalPath() + "\",\"query\":\"" +
                       request->query() + "\",\"body\":" + std::to_string(request->body().size()) + "}";
    callback(text(k200OK, seen, "application/json"));
}

}  // namespace

// Registered by name, so outside the anonymous namespace.
class EchoSocket : public WebSocketController<EchoSocket, false> {
  public:
    void handleNewMessage(const WebSocketConnectionPtr& connection, std::string&& message,
                          const WebSocketMessageType& type) override {
        if (type == WebSocketMessageType::Text) {
            connection->send("echo: " + message);
        }
    }
    void handleNewConnection(const HttpRequestPtr&, const WebSocketConnectionPtr& connection) override {
        connection->send("hello");
    }
    void handleConnectionClosed(const WebSocketConnectionPtr&) override {}
    WS_PATH_LIST_BEGIN
    WS_PATH_LIST_END
};

namespace {

void drain_then_quit(std::chrono::milliseconds grace) {
    if (draining.exchange(true)) {
        return;
    }
    std::cerr << "draining: " << in_flight.load() << " in flight\n";
    std::thread([grace]() {
        const auto deadline = std::chrono::steady_clock::now() + grace;
        while (in_flight.load() > 0 && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(10ms);
        }
        std::cerr << "quitting: " << in_flight.load() << " still in flight\n";
        app().getLoop()->queueInLoop([]() { app().quit(); });
    }).detach();
}

}  // namespace

int main(int argc, char** argv) {
    const int port = argc > 1 ? std::atoi(argv[1]) : 5100;
    app()
        .addListener("127.0.0.1", static_cast<uint16_t>(port))
        .setThreadNum(2)
        .enableServerHeader(false)
        .enableDateHeader(true)
        .setClientMaxBodySize(2 * 1024 * 1024)
        .disableSigtermHandling()
        .registerNewConnectionAdvice([](const trantor::InetAddress&, const trantor::InetAddress&) {
            // While draining, new connections are closed at once.
            return !draining.load();
        })
        .registerPreSendingAdvice([](const HttpRequestPtr& request, const HttpResponsePtr& response) {
            // Drogon won't start an async stream on a connection it will
            // close (a request with `Connection: close`, or HTTP/1.0): it
            // sends the headers and never calls the stream callback. Keeping
            // the connection open lets the stream run; the chunked
            // terminator still ends the body.
            if (response->asyncStreamCallback() && !keeps_alive(request)) {
                response->setCloseConnection(false);
                response->addHeader("connection", "close");
            }
        })
        .setDefaultHandler(handle)
        // The controller is an instance we create (the real adapter passes it
        // the runtime), registered before its regex path.
        .registerController(std::make_shared<EchoSocket>())
        .registerWebSocketControllerRegex("^/[^/]+/ws$", "EchoSocket");
    // Drogon's own handlers would quit at once; ours drain first.
    std::signal(SIGINT, [](int) { drain_then_quit(5000ms); });
    std::signal(SIGTERM, [](int) { drain_then_quit(5000ms); });
    std::cerr << "listening on 127.0.0.1:" << port << "\n";
    app().run();
    std::cerr << "stopped\n";
    return 0;
}
