// Observing a Thing over its WebSocket, `/{thing}/ws`: a property's changes
// and an action's status, pushed to the client instead of polled.
//
//     example-observe-websocket                         # an in-process demo
//     example-observe-websocket --serve                 # serve on http://127.0.0.1:5000
//     example-observe-websocket --serve --port 0        # any free port
//
// While it serves, open http://127.0.0.1:5000/oven/monitor.html in a
// browser. The C++ port of teta-wot's `observe-websocket`.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <teta/wot.hpp>
#include <vector>

#include "monitor_html.hpp"

namespace tw = teta::wot;
using tw::json::Value;
using namespace std::chrono_literals;

/// A small oven, heated in steps.
class Oven {
  public:
    /// The temperature inside the oven.
    tw::Prop<double> temperature{20.0};

    double heat_to(tw::ActionCtx& ctx, double target) {
        for (;;) {
            const double now = temperature.get();
            if (std::abs(target - now) < 1e-9) {
                return now;
            }
            ctx.sleep(200ms);
            temperature.set(now + std::clamp(target - now, -10.0, 10.0));
        }
    }

    bool safe_to_open() const { return temperature.get() < 50.0; }

    tw::http::Response monitor() {
        return tw::http::Response::bytes(monitor_html, "text/html; charset=utf-8");
    }

    static void define(tw::ThingSpec<Oven>& t) {
        t.doc("A small oven, heated in steps.");
        t.property("temperature", &Oven::temperature)
            .doc("The temperature inside the oven.")
            .read_only()
            .unit("degree Celsius");
        t.action<"target">("heat_to", &Oven::heat_to)
            .doc("Heat or cool to a temperature, at most 10 degrees per step.");
        t.property("safe_to_open", &Oven::safe_to_open)
            .doc("Whether the oven is cool enough to open. Computed when read, so it\ncan't be observed.");
        t.endpoint("GET", "monitor.html", &Oven::monitor)
            .doc("A page that observes the oven over its WebSocket.");
    }
};

tw::ThingServer server() {
    return tw::ThingServer::builder().thing<Oven>("oven").build();
}

void demo() {
    const auto client = tw::testing::TestClient::start(server());
    auto ws = client.websocket("/oven/ws");
    for (const Value& message : {
             Value{{"messageType", "addPropertyObservation"}, {"data", {{"temperature", true}}}},
             Value{{"messageType", "addActionObservation"}, {"data", {{"heat_to", true}}}},
             // Refused: a functional property isn't observable.
             Value{{"messageType", "addPropertyObservation"}, {"data", {{"safe_to_open", true}}}},
         }) {
        std::cout << "→ " << tw::json::dump(message) << "\n";
        ws.send_json(message);
    }
    // The dialect acknowledges nothing, but answers a bad request. As a
    // connection's messages are handled in order, this answer also means the
    // two subscriptions before it are in place.
    const auto refusal = ws.receive_json();
    std::cout << "← " << tw::json::dump(refusal) << "\n";
    if (refusal["error"]["status"] != "403") {
        throw std::runtime_error("safe_to_open should be refused");
    }

    const auto invocation = client.post_json("/oven/heat_to", Value{{"target", 50}});
    std::cout << "POST /oven/heat_to {\"target\": 50} → " << invocation.status << " "
              << tw::http::reason_phrase(invocation.status) << "\n";

    std::vector<Value> temperatures;
    std::vector<std::string> statuses;
    while (statuses.empty() || statuses.back() != "completed") {
        const auto message = ws.receive_json();
        std::cout << "← " << tw::json::dump(message) << "\n";
        if (message["messageType"] == "propertyStatus") {
            temperatures.push_back(message["data"]["temperature"]);
        } else if (message["messageType"] == "actionStatus") {
            statuses.push_back(message["data"]["status"].get<std::string>());
        } else {
            throw std::runtime_error("unexpected message " + tw::json::dump(message));
        }
    }
    if (temperatures != std::vector<Value>{30.0, 40.0, 50.0} ||
        statuses != std::vector<std::string>{"pending", "running", "completed"}) {
        throw std::runtime_error("the oven should heat in three steps");
    }

    const auto page = client.get("/oven/monitor.html");
    std::cout << "GET /oven/monitor.html → " << page.status << " " << tw::http::reason_phrase(page.status)
              << " (" << page.body.size() << " bytes of HTML)\n";
    const auto td = client.get("/oven/").json();
    std::cout << "The TD's forms for temperature: "
              << tw::json::dump(td["properties"]["temperature"]["forms"]) << "\n";
    ws.close();
    client.stop();
}

int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string_view(argv[1]) == "--serve") {
            std::uint16_t port = 5000;
            if (argc > 3 && std::string_view(argv[2]) == "--port") {
                port = static_cast<std::uint16_t>(std::stoul(argv[3]));
            }
            tw::log::init(false);
            server().serve("127.0.0.1", port, tw::shutdown_signal(), [](std::uint16_t bound) {
                std::cout << "listening on http://127.0.0.1:" << bound << "\n"
                          << "open http://127.0.0.1:" << bound << "/oven/monitor.html in a browser"
                          << std::endl;
            });
            return EXIT_SUCCESS;
        }
        demo();
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
