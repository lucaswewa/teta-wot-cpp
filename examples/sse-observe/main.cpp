// Observing a property in the browser with `EventSource`: a thermometer's
// reading, pushed as server-sent events. The page finds the stream in the
// Thing Description (the form with `"subprotocol": "sse"`), as any TD
// consumer would.
//
//     example-sse-observe                         # an in-process demo
//     example-sse-observe --serve                 # serve on http://127.0.0.1:5000
//     example-sse-observe --serve --port 0        # any free port
//
// While it serves, open http://127.0.0.1:5000/thermometer/index.html, or
// watch the stream with
// `curl -N -H "Accept: text/event-stream" http://127.0.0.1:5000/thermometer/temperature`.
// The C++ port of teta-wot's `sse-observe`.

#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <teta/wot.hpp>
#include <thread>

#include "index_html.hpp"

namespace tw = teta::wot;
using tw::json::Value;
using namespace std::chrono_literals;

/// How often the simulated sensor is read.
constexpr auto period = 250ms;

/// A thermometer that reads a simulated sensor four times a second.
class Thermometer {
  public:
    /// The latest reading.
    tw::Prop<double> temperature{21.0};

    /// Starts reading. Every change of the property reaches its observers.
    void start_reading() {
        const auto started = std::chrono::steady_clock::now();
        reader_ = std::jthread([this, started](std::stop_token stop) {
            auto next = std::chrono::steady_clock::now();
            std::mutex mutex;
            std::condition_variable_any wake;
            while (!stop.stop_requested()) {
                const double t =
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
                const double reading = 21.0 + 0.8 * std::sin(t / 3.0) + 0.05 * std::sin(t * 7.0);
                temperature.set(std::round(reading * 100.0) / 100.0);
                next += period;
                std::unique_lock lock(mutex);
                wake.wait_until(lock, stop, next, [] { return false; });
            }
        });
    }

    /// Stops reading.
    void stop_reading() { reader_ = {}; }

    tw::http::Response page() { return tw::http::Response::bytes(index_html, "text/html; charset=utf-8"); }

    static void define(tw::ThingSpec<Thermometer>& t) {
        t.doc("A thermometer that reads a simulated sensor four times a second.");
        t.property("temperature", &Thermometer::temperature)
            .doc("The latest reading.")
            .read_only()
            .unit("degree Celsius");
        t.on_start(&Thermometer::start_reading);
        t.on_stop(&Thermometer::stop_reading);
        t.endpoint("GET", "index.html", &Thermometer::page)
            .doc("A page that plots the reading as it arrives.");
    }

  private:
    std::jthread reader_;
};

tw::ThingServer server() {
    return tw::ThingServer::builder().thing<Thermometer>("thermometer").build();
}

void demo() {
    // What the page does, without a browser.
    const auto client = tw::testing::TestClient::start(server());
    const auto td = client.get("/thermometer/").json();
    Value form;
    for (const auto& candidate : td["properties"]["temperature"]["forms"]) {
        if (candidate.contains("subprotocol") && candidate["subprotocol"] == "sse") {
            form = candidate;
        }
    }
    if (form.is_null()) {
        throw std::runtime_error("no SSE form in the TD");
    }
    std::cout << "The TD's SSE form: " << tw::json::dump(form) << "\n";
    const auto href = form["href"].get<std::string>();

    auto stream = client.events(href);
    std::cout << "GET " << href << " (Accept: text/event-stream) → " << stream.status << " "
              << tw::http::reason_phrase(stream.status) << " " << stream.header("content-type").value_or("-")
              << "\n";
    for (int i = 0; i < 4; ++i) {
        const auto reading = stream.next();
        if (!reading) {
            throw std::runtime_error("the stream ended");
        }
        std::cout << "data: " << tw::json::dump(*reading) << "\n";
        if (!reading->is_number_float()) {
            throw std::runtime_error("a reading should be a number");
        }
    }

    const auto page = client.get("/thermometer/index.html");
    std::cout << "GET /thermometer/index.html → " << page.status << " "
              << tw::http::reason_phrase(page.status) << "\n";
    if (page.text().find("EventSource") == std::string::npos) {
        throw std::runtime_error("the page should use EventSource");
    }
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
                          << "open http://127.0.0.1:" << bound << "/thermometer/index.html in a browser"
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
