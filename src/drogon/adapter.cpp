#include <cctype>
#include <mutex>
#include <string>
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

// A streamed body's chunks, sent through Drogon's response stream.
class DrogonSink final : public http::StreamSink {
  public:
    DrogonSink(drogon::ResponseStreamPtr stream, std::weak_ptr<trantor::TcpConnection> connection,
               bool keep_alive)
        : stream_(std::move(stream)), connection_(std::move(connection)), keep_alive_(keep_alive) {}
    ~DrogonSink() override { close(); }

    bool send(std::string_view chunk) override {
        std::lock_guard lock(mutex_);
        if (!stream_) {
            return false;
        }
        if (chunk.empty()) {
            return true;  // an empty chunk would end the body
        }
        return stream_->send(std::string(chunk));
    }

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
};

}  // namespace

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
    return mount(
        Service([binding = std::move(binding)](http::Request request, http::HttpBinding::Respond respond) {
            binding->handle(std::move(request), std::move(respond));
        }),
        app);
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
