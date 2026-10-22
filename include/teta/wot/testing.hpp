// An in-process test client: a port of teta-wot-server's `testing`.
//
// `TestClient` sends requests straight to a server's HTTP binding, without a
// socket. Starting it starts the Things; `stop` shuts them down. Requests
// carry `Host: testserver` by default, so absolute URLs match the reference's
// fixtures. Calls wait for their answers, so call them from a test's thread,
// never from Thing code. `Harness` tests one Thing, with stand-ins for its
// slots and a temporary settings folder.

#pragma once

#include <chrono>
#include <concepts>
#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "teta/wot/http/message.hpp"
#include "teta/wot/json.hpp"
#include "teta/wot/thing_server.hpp"

namespace teta::wot::testing {

/// How long a TestEventStream waits for an event before failing.
inline constexpr std::chrono::seconds receive_timeout{10};

/// A response received by a TestClient.
struct TestResponse {
    int status = 0;
    http::Headers headers;
    std::string body;

    /// The body as JSON (`null` if it is empty or not JSON).
    json::Value json() const;
    /// The body as text.
    const std::string& text() const { return body; }
    /// A header's value, if present.
    std::optional<std::string> header(std::string_view name) const;
};

/// A server-sent event received by TestEventStream::next_message.
struct TestSseMessage {
    /// The event's name (`event:`), if it has one.
    std::optional<std::string> event;
    /// Its ID (`id:`), if it has one.
    std::optional<std::string> id;
    /// Its data, as JSON (a string if it isn't JSON).
    json::Value data;
};

namespace detail {
class ChunkQueue;
}

/// A server-sent events stream opened by TestClient::events. Destroying it
/// is the client going away.
class TestEventStream {
  public:
    TestEventStream(TestEventStream&&) noexcept;
    TestEventStream& operator=(TestEventStream&&) noexcept;
    ~TestEventStream();

    int status = 0;
    http::Headers headers;

    std::optional<std::string> header(std::string_view name) const;
    /// The next event's data, as JSON (a string if it isn't JSON), or
    /// `nullopt` when the stream ends. Comments (keep-alives) are skipped.
    /// Throws std::runtime_error after receive_timeout.
    std::optional<json::Value> next();
    /// The next event, with its name and ID: as `next`.
    std::optional<TestSseMessage> next_message();
    /// The next event's data if one arrives within `timeout`.
    std::optional<json::Value> next_within(std::chrono::nanoseconds timeout);

  private:
    friend class TestClient;
    TestEventStream() = default;
    // The next event, or nullopt at the end; throws on a timeout if `strict`.
    std::optional<TestSseMessage> receive(std::chrono::nanoseconds timeout, bool strict);

    std::shared_ptr<detail::ChunkQueue> chunks_;
    std::string buffer_;
};

/// Sends requests to a ThingServer in-process.
class TestClient {
  public:
    /// Starts the server's Things and returns a client for it. Throws
    /// StartupError.
    static TestClient start(const ThingServer& server);

    /// This client with another `Host` header (and so another base URL in
    /// answers).
    TestClient with_host(std::string host) const;
    /// This client, sending a header with every request (`Authorization`).
    TestClient with_header(std::string name, std::string value) const;

    /// The runtime behind the server.
    const std::shared_ptr<Runtime>& runtime() const { return binding_->runtime(); }

    /// Sends a request with optional headers and body. `path` may have a
    /// query, or be an absolute URL. A streamed answer is read to its end.
    TestResponse request(std::string method, std::string_view path,
                         const std::vector<std::pair<std::string, std::string>>& headers = {},
                         std::optional<std::string> body = std::nullopt) const;
    /// `GET path`.
    TestResponse get(std::string_view path) const;
    /// `DELETE path`.
    TestResponse del(std::string_view path) const;
    /// `PUT path` with a JSON body.
    TestResponse put_json(std::string_view path, const json::Value& body) const;
    /// `POST path` with a JSON body, or none.
    TestResponse post_json(std::string_view path,
                           const std::optional<json::Value>& body = std::nullopt) const;
    /// Opens a server-sent events stream: `GET path` with `Accept:
    /// text/event-stream`.
    TestEventStream events(std::string_view path) const;

    /// Cancels running invocations and stops the Things.
    void stop() const;

  private:
    explicit TestClient(std::shared_ptr<http::HttpBinding> binding) : binding_(std::move(binding)) {}
    http::Request make(std::string method, std::string_view path) const;
    http::Response send(http::Request request) const;

    std::shared_ptr<http::HttpBinding> binding_;
    std::string host_ = "testserver";
    std::vector<std::pair<std::string, std::string>> headers_;
};

namespace detail {

/// A temporary folder, removed with the last copy of its pointer.
class TemporaryFolder {
  public:
    /// Makes a new, empty folder. Throws std::filesystem::filesystem_error.
    TemporaryFolder();
    TemporaryFolder(const TemporaryFolder&) = delete;
    TemporaryFolder& operator=(const TemporaryFolder&) = delete;
    ~TemporaryFolder();
    const std::filesystem::path& path() const { return path_; }

  private:
    std::filesystem::path path_;
};

}  // namespace detail

/// Testing one Thing (LabThings' `create_thing_without_server`): it runs on a
/// small server with a temporary settings folder, and its slots are filled by
/// stand-in Things (fakes that provide the same interface, or real Things
/// with test configurations) added with `Builder::thing`.
///
///     auto harness = tw::testing::Harness<Autofocus>::builder("autofocus")
///                        .thing("stage", std::make_shared<FakeStage>())
///                        .thing<FakeCamera>("camera")
///                        .start();
///     harness.thing().call(&Autofocus::focus);           // a ThingRef<Autofocus>
///     harness.client().post_json("/autofocus/focus");
template <ThingType T>
class Harness {
  public:
    /// Builds a Harness.
    class Builder {
      public:
        /// Adds another Thing, for example a stand-in for a slot.
        template <ThingType U>
        Builder& thing(std::string name, std::shared_ptr<U> thing) {
            builder_.thing(std::move(name), std::move(thing));
            return *this;
        }
        template <ThingType U>
            requires std::default_initializable<U>
        Builder& thing(std::string name) {
            return thing(std::move(name), std::make_shared<U>());
        }
        /// Registers a service.
        template <typename S>
        Builder& service(std::shared_ptr<S> service) {
            builder_.service(std::move(service));
            return *this;
        }
        /// Configures the slots of the Thing under test.
        Builder& slots(std::vector<std::pair<std::string, SlotSelection>> slots) {
            builder_.thing_slots(name_, std::move(slots));
            return *this;
        }
        /// Sets the application configuration.
        Builder& application_config(json::Value config) {
            builder_.application_config(std::move(config));
            return *this;
        }
        /// Enables the global lock.
        Builder& global_lock(bool enabled) {
            builder_.global_lock(enabled);
            return *this;
        }
        /// Uses an existing settings folder instead of a temporary one, for
        /// example to test loading a prepared settings file.
        Builder& settings_folder(std::filesystem::path folder) {
            builder_.settings_folder(std::move(folder));
            temporary_ = false;
            return *this;
        }

        /// Builds the server and starts the Things. Throws BuildError,
        /// http::RouteError or StartupError.
        Harness start() {
            std::shared_ptr<detail::TemporaryFolder> folder;
            if (temporary_) {
                folder = std::make_shared<detail::TemporaryFolder>();
                builder_.settings_folder(folder->path());
            }
            const auto server = builder_.build();
            auto thing = server.runtime()->template thing_ref<T>(name_);
            return Harness(TestClient::start(server), std::move(*thing), std::move(folder));
        }

      private:
        friend class Harness;
        Builder(std::string name, std::shared_ptr<T> thing) : name_(name) {
            builder_.thing(std::move(name), std::move(thing));
        }
        ThingServerBuilder builder_;
        std::string name_;
        bool temporary_ = true;
    };

    /// A harness for `thing`, served as `name`.
    static Builder builder(std::string name, std::shared_ptr<T> thing) {
        return Builder(std::move(name), std::move(thing));
    }
    /// A harness for a default-constructed Thing.
    static Builder builder(std::string name)
        requires std::default_initializable<T>
    {
        return builder(std::move(name), std::make_shared<T>());
    }

    /// The Thing under test.
    const ThingRef<T>& thing() const { return thing_; }
    /// A client for the server.
    const TestClient& client() const { return client_; }
    /// The temporary settings folder, if the harness has one. It is removed
    /// with the harness.
    std::optional<std::filesystem::path> settings_folder() const {
        if (folder_) {
            return folder_->path();
        }
        return std::nullopt;
    }
    /// Stops the Things.
    void stop() const { client_.stop(); }

  private:
    Harness(TestClient client, ThingRef<T> thing, std::shared_ptr<detail::TemporaryFolder> folder)
        : client_(std::move(client)), thing_(std::move(thing)), folder_(std::move(folder)) {}

    TestClient client_;
    ThingRef<T> thing_;
    std::shared_ptr<detail::TemporaryFolder> folder_;
};

}  // namespace teta::wot::testing
