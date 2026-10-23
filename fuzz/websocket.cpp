// A Thing's WebSocket, the dialect's parser (teta-wot fuzzes it with
// proptest, `observe.rs`): whatever text a client sends, the session answers
// only error responses, 404 or 403, for a name the message has, and 403 only
// for the functional property, which can't be observed.

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "fuzz.hpp"
#include "teta/wot.hpp"

namespace tw = teta::wot;
using namespace std::chrono_literals;

namespace {

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

// One server and socket for every input.
struct Fixture {
    Fixture()
        : server((tw::log::set_sink(nullptr), tw::ThingServer::builder().thing<Probe>("probe").build())),
          client(tw::testing::TestClient::start(server)),
          socket(client.websocket("/probe/ws")) {}
    ~Fixture() { client.stop(); }

    tw::ThingServer server;
    tw::testing::TestClient client;
    tw::testing::TestWebSocket socket;
};

Fixture& fixture() {
    static Fixture instance;
    return instance;
}

}  // namespace

void fuzz::one_input(std::string_view input) {
    auto& f = fixture();
    const std::string text(input);
    f.socket.send_text(text);
    while (const auto reply = f.socket.receive_json_within(0ms)) {
        const auto shown = tw::json::dump(*reply) + " for " + text;
        fuzz::check((*reply)["messageType"] == "response", "not an error response: " + shown);
        const auto& error = (*reply)["error"];
        fuzz::check(error.is_object() && error["status"].is_string(), "no error status: " + shown);
        const auto status = error["status"].get<std::string>();
        fuzz::check(status == "404" || status == "403", "neither 404 nor 403: " + shown);
        // A reply answers a JSON object whose data names what it refuses.
        const auto request = tw::json::parse(text);
        const auto& name = (*reply)["name"];
        fuzz::check(name.is_string() && request["data"].contains(name.get<std::string>()),
                    "a name the message hasn't: " + shown);
        fuzz::check(status != "403" || name == "computed", "403 for an observable name: " + shown);
    }
}
