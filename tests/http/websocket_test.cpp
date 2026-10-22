// A Thing's WebSocket: teta-wot-http's `observe.rs` tests (the relayed
// messages, the error responses, and its fuzzing of the dialect), and the
// socket through TestClient: observations, refusals, bad messages, the close
// at shutdown, and backpressure.

#include "http/websocket.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "../log_capture.hpp"
#include "teta/wot.hpp"

namespace tw = teta::wot;
namespace http = teta::wot::http;
using tw::json::Value;
using tw::testing::TestClient;
using namespace std::chrono_literals;

namespace websocket_test {

struct Probe {
    tw::Prop<std::int64_t> data{0};
    tw::Event<std::string> happened;

    std::int64_t computed() const { return 1; }
    void act() {}

    static void define(tw::ThingSpec<Probe>& t) {
        t.property("data", &Probe::data);
        t.property("computed", &Probe::computed);
        t.action("act", &Probe::act);
        t.event("happened", &Probe::happened);
    }
};

struct Fixture {
    Fixture()
        : server(tw::ThingServer::builder().thing("probe", probe).build()),
          client(TestClient::start(server)) {}
    ~Fixture() { client.stop(); }
    std::shared_ptr<Probe> probe = std::make_shared<Probe>();
    tw::ThingServer server;
    TestClient client;
};

Value observe(std::string_view message_type, std::string_view name) {
    return Value{{"messageType", message_type}, {"data", {{name, true}}}};
}

// A peer that records what is sent, and keeps up only when told to.
class SlowPeer final : public http::WebSocketPeer {
  public:
    bool send_text(std::string_view text) override {
        sent.emplace_back(text);
        return true;
    }
    void close(std::uint16_t code, std::string_view) override { closed = code; }
    bool ready() const override { return keeping_up; }
    void set_resume(std::function<void()> callback) override { resume = std::move(callback); }

    std::vector<std::string> sent;
    std::uint16_t closed = 0;
    bool keeping_up = false;
    std::function<void()> resume;
};

}  // namespace websocket_test

using namespace websocket_test;

TEST(WebSocket, RelayedMessages) {
    const auto message = [](tw::MessageKind kind, Value payload) {
        return tw::Message::now("thing", "name", kind, std::move(payload));
    };
    EXPECT_EQ(tw::json::dump(http::detail::relay(message(tw::MessageKind::property, 3))),
              R"({"messageType":"propertyStatus","data":{"name":3}})");
    EXPECT_EQ(tw::json::dump(http::detail::relay(message(tw::MessageKind::action, "running"))),
              R"({"messageType":"actionStatus","data":{"action name":"name","status":"running"}})");
    const auto event = http::detail::relay(message(tw::MessageKind::event, Value{{"a", 1}}));
    EXPECT_EQ(event["messageType"], "event");
    EXPECT_EQ(tw::json::dump(event["data"]["name"]["data"]), R"({"a":1})");
    const auto timestamp = event["data"]["name"]["timestamp"].get<std::string>();
    EXPECT_TRUE(timestamp.ends_with('Z'));
    EXPECT_EQ(timestamp.size(), std::string("2026-10-22T17:05:06.462336Z").size()) << "microseconds";
}

TEST(WebSocket, ErrorResponses) {
    EXPECT_EQ(
        tw::json::dump(http::detail::error_response("funcprop", http::detail::Target::property,
                                                    http::detail::Refusal::not_observable)),
        R"({"messageType":"response","operation":"observeproperty","name":"funcprop","error":{"status":"403",)"
        R"("type":"https://w3c.github.io/web-thing-protocol/errors#not-observable","title":"Not Observable",)"
        R"("detail":"Property 'funcprop' is not observable."}})");
    EXPECT_EQ(http::detail::error_response("missing", http::detail::Target::action,
                                           http::detail::Refusal::not_found)["error"]["detail"],
              "No action found with the name 'missing'.");
    EXPECT_EQ(http::detail::error_response("x", http::detail::Target::event,
                                           http::detail::Refusal::not_found)["operation"],
              "subscribeevent");
}

TEST(WebSocket, PropertiesActionsAndEventsAreObserved) {
    Fixture f;
    auto ws = f.client.websocket("/probe/ws");
    ws.send_json(observe("addPropertyObservation", "data"));
    ws.send_json(observe("addActionObservation", "act"));
    ws.send_json(observe("addEventSubscription", "happened"));
    f.probe->data.set(5);
    EXPECT_EQ(tw::json::dump(ws.receive_json()), R"({"messageType":"propertyStatus","data":{"data":5}})");
    f.probe->happened.emit("now");
    const auto event = ws.receive_json();
    EXPECT_EQ(event["data"]["happened"]["data"], "now");
    f.client.post_json("/probe/act");
    std::vector<std::string> statuses;
    while (statuses.empty() || statuses.back() != "completed") {
        const auto message = ws.receive_json();
        ASSERT_EQ(message["messageType"], "actionStatus");
        EXPECT_EQ(message["data"]["action name"], "act");
        statuses.push_back(message["data"]["status"].get<std::string>());
    }
    EXPECT_EQ(statuses, (std::vector<std::string>{"pending", "running", "completed"}));
}

TEST(WebSocket, NamesThatCantBeObservedAreRefusedAndTheRestOfTheMessageSkipped) {
    Fixture f;
    auto ws = f.client.websocket("/probe/ws");
    ws.send_json(
        Value{{"messageType", "addPropertyObservation"}, {"data", {{"computed", true}, {"data", true}}}});
    const auto refusal = ws.receive_json();
    EXPECT_EQ(refusal["error"]["status"], "403");
    EXPECT_EQ(refusal["name"], "computed");
    ws.send_json(observe("addEventSubscription", "nope"));
    EXPECT_EQ(ws.receive_json()["error"]["detail"], "No event found with the name 'nope'.");
    // `data`, after the refused name, wasn't observed.
    f.probe->data.set(1);
    EXPECT_FALSE(ws.receive_json_within(100ms));
}

TEST(WebSocket, BadMessagesAreLoggedAndIgnored) {
    Fixture f;
    auto ws = f.client.websocket("/probe/ws");
    testing_support::LogCapture log;
    ws.send_text("not json");
    ws.send_text("[1]");
    ws.send_json(Value{{"messageType", "addPropertyObservation"}, {"data", 5}});
    ws.send_json(Value{{"messageType", "setProperty"}, {"data", {{"data", 1}}}});
    EXPECT_FALSE(ws.receive_json_within(100ms));
    const auto messages = log.messages();
    ASSERT_EQ(messages.size(), 3u);
    EXPECT_EQ(
        messages[0],
        "Got a bad websocket message: not json, caused a JSON error: expected ident at line 1 column 2.");
    EXPECT_EQ(messages[1], "Got a bad websocket message: [1], caused a missing messageType.");
    EXPECT_EQ(
        messages[2],
        R"(Got a bad websocket message: {"messageType":"addPropertyObservation","data":5}, caused data )"
        R"(that isn't an object.)");
}

TEST(WebSocket, OnlyThingsHaveSockets) {
    Fixture f;
    EXPECT_THROW((void)f.client.websocket("/nope/ws"), std::runtime_error);
    EXPECT_THROW((void)f.client.websocket("/probe/data"), std::runtime_error);
    // Without the upgrade, the path is an ordinary one.
    EXPECT_EQ(f.client.get("/probe/ws").status, 404);
}

TEST(WebSocket, ShutdownClosesTheSocket) {
    Fixture f;
    auto ws = f.client.websocket("/probe/ws");
    ws.send_json(observe("addPropertyObservation", "data"));
    f.probe->data.set(2);
    f.client.stop();
    EXPECT_EQ(ws.receive_json()["data"]["data"], 2) << "what was buffered is sent first";
    EXPECT_EQ(ws.closed(), (tw::testing::TestClose{1001, "the server is shutting down"}));
}

TEST(WebSocket, ASlowClientMissesWhatDoesntFitInItsBuffer) {
    Fixture f;
    auto peer = std::make_shared<SlowPeer>();
    http::Request request = http::Request::make("GET", "/probe/ws");
    request.headers.add("upgrade", "websocket");
    const auto session = f.server.binding()->open_websocket(request, peer);
    session->on_text(tw::json::dump(observe("addPropertyObservation", "data")));
    testing_support::LogCapture log;
    for (int i = 1; i <= 8; ++i) {
        f.probe->data.set(i);
    }
    EXPECT_TRUE(peer->sent.empty()) << "nothing is taken while the client doesn't keep up";
    EXPECT_EQ(log.messages().size(), 3u) << "the broker drops what doesn't fit, with a warning";
    peer->keeping_up = true;
    peer->resume();
    ASSERT_EQ(peer->sent.size(), tw::subscriber_buffer);
    EXPECT_EQ(peer->sent.front(), R"({"messageType":"propertyStatus","data":{"data":1}})");
    session->on_closed();
}

// Fuzzing the dialect's parser (`observe.rs`' proptest): whatever a client
// sends, the session answers only error responses, 404 or 403, for a name
// the message has, and 403 only for the functional property.
TEST(WebSocket, AnyTextGetsOnlyErrorResponses) {
    Fixture f;
    std::mt19937 random(12345);
    const std::vector<std::string> names = {"data", "computed", "act", "happened", "ws", "", "nope"};
    const std::vector<std::string> types = {"addPropertyObservation", "addActionObservation",
                                            "addEventSubscription",   "setProperty",
                                            "requestAction",          ""};
    auto pick = [&](const auto& list) { return list[random() % list.size()]; };
    std::function<Value(int)> json = [&](int depth) -> Value {
        switch (random() % (depth > 2 ? 4 : 6)) {
            case 0:
                return nullptr;
            case 1:
                return random() % 2 == 0;
            case 2:
                return static_cast<std::int64_t>(random()) - 1'000'000;
            case 3:
                return pick(names);
            case 4: {
                Value list = Value::array();
                for (auto n = random() % 4; n > 0; --n) {
                    list.push_back(json(depth + 1));
                }
                return list;
            }
            default: {
                Value object = Value::object();
                for (auto n = random() % 4; n > 0; --n) {
                    object[pick(names)] = json(depth + 1);
                }
                return object;
            }
        }
    };
    auto ws = f.client.websocket("/probe/ws");
    for (int i = 0; i < 2000; ++i) {
        std::string text;
        Value request = Value::object();
        switch (i % 3) {
            case 0: {
                if (random() % 4 != 0) {
                    request["messageType"] = random() % 5 == 0 ? json(0) : Value(pick(types));
                }
                Value data = Value::object();
                for (auto n = random() % 5; n > 0; --n) {
                    data[pick(names)] = json(1);
                }
                request["data"] = random() % 5 == 0 ? json(0) : data;
                request["extra"] = json(1);
                text = tw::json::dump(request);
                break;
            }
            case 1:
                text = tw::json::dump(json(0));
                break;
            default:
                for (auto n = random() % 64; n > 0; --n) {
                    text += static_cast<char>(' ' + random() % 95);
                }
        }
        ws.send_text(text);
        while (const auto reply = ws.receive_json_within(0ms)) {
            ASSERT_EQ((*reply)["messageType"], "response") << text;
            const auto status = (*reply)["error"]["status"].get<std::string>();
            ASSERT_TRUE(status == "404" || status == "403") << tw::json::dump(*reply);
            request = tw::json::parse(text);
            ASSERT_TRUE(request["data"].contains((*reply)["name"].get<std::string>())) << text;
            if (status == "403") {
                ASSERT_EQ((*reply)["name"], "computed");
            }
        }
    }
}
