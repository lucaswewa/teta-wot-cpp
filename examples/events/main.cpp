// Events: a laser's safety interlock trips on its device thread, which emits
// an event straight from synchronous driver code. Clients receive it over
// server-sent events and over the WebSocket; C++ code can subscribe too.
//
//     example-events                         # an in-process demo
//     example-events --serve                 # serve on http://127.0.0.1:5000
//     example-events --serve --port 0        # any free port
//
// While it serves:
//
//     curl -N http://127.0.0.1:5000/laser/tripped              # waits for events
//     curl -X POST http://127.0.0.1:5000/laser/open_door       # in another terminal
//
// The C++ port of teta-wot's `events`.

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <teta/wot.hpp>

namespace tw = teta::wot;
using tw::json::Value;
using namespace std::chrono_literals;

namespace laser {

/// Why the interlock tripped.
struct Trip {
    /// The input that opened the interlock circuit.
    std::string input;
    /// The emission's state before it was cut.
    bool was_emitting = false;

    static void describe(tw::StructInfo<Trip>& s) {
        s.description("Why the interlock tripped.");
        s.field("input").description("The input that opened the interlock circuit.");
        s.field("was_emitting").description("The emission's state before it was cut.");
    }
};

/// A simulated interlock circuit, read and driven on the device's thread.
struct Interlock {
    bool door_open = false;
    bool emitting = false;
    /// Where trips are reported: the Thing's event, given at start.
    std::optional<tw::Event<Trip>> on_trip;

    void set_door(bool open) {
        if (open && !door_open) {
            const Trip trip{"enclosure door", emitting};
            emitting = false;
            std::cout << "[" << tw::device_thread_name() << "] the interlock tripped: Trip { input: \""
                      << trip.input << "\", was_emitting: " << (trip.was_emitting ? "true" : "false") << " }"
                      << std::endl;
            // Synchronous code, on the device thread: `emit` never waits.
            if (on_trip) {
                on_trip->emit(trip);
            }
        }
        door_open = open;
    }
};

/// A laser in an enclosure with a safety interlock.
class Laser {
  public:
    tw::Device<Interlock> interlock;
    tw::Event<Trip> tripped;

    /// Gives the driver the event to emit (an Event is a cheap handle).
    void connect() {
        interlock.call([tripped = tripped](Interlock& driver) { driver.on_trip = tripped; });
    }

    bool emitting() {
        return interlock.call([](Interlock& driver) { return driver.emitting; });
    }

    bool start_emission() {
        return interlock.call([](Interlock& driver) {
            driver.emitting = !driver.door_open;
            return driver.emitting;
        });
    }

    void open_door() {
        interlock.call([](Interlock& driver) { driver.set_door(true); });
    }

    void close_door() {
        interlock.call([](Interlock& driver) { driver.set_door(false); });
    }

    static void define(tw::ThingSpec<Laser>& t) {
        t.doc("A laser in an enclosure with a safety interlock.");
        t.device("interlock", &Laser::interlock);
        t.event("tripped", &Laser::tripped)
            .doc(
                "The interlock tripped, and emission stopped.\n\nSent when the enclosure is opened while the "
                "interlock is armed.");
        t.on_start(&Laser::connect);
        t.property("emitting", &Laser::emitting).doc("Whether the laser is emitting.");
        t.action("start_emission", &Laser::start_emission).doc("Start emitting, if the door is closed.");
        t.action("open_door", &Laser::open_door)
            .doc("Open the enclosure door (in the simulation), which trips the interlock.");
        t.action("close_door", &Laser::close_door).doc("Close the enclosure door.");
    }
};

}  // namespace laser

using laser::Laser;
using laser::Trip;

tw::ThingServer server() {
    return tw::ThingServer::builder().thing<Laser>("laser").build();
}

void demo() {
    // Three subscribers, one trip.
    const auto client = tw::testing::TestClient::start(server());
    const auto laser = *client.runtime()->thing_ref<Laser>("laser");
    auto in_cpp = laser.thing().tripped.subscribe();

    auto sse = client.events("/laser/tripped");
    std::cout << "GET /laser/tripped → " << sse.status << " " << tw::http::reason_phrase(sse.status) << " "
              << sse.header("content-type").value_or("-") << "\n";

    auto ws = client.websocket("/laser/ws");
    ws.send_json(Value{{"messageType", "addEventSubscription"}, {"data", {{"tripped", true}}}});
    // Refused (no such event); as messages are handled in order, the answer
    // also means the subscription above is in place.
    ws.send_json(Value{{"messageType", "addEventSubscription"}, {"data", {{"exploded", true}}}});
    std::cout << "WebSocket ← " << tw::json::dump(ws.receive_json()) << "\n";

    // In-process calls wait for the action; then the trip, over HTTP.
    laser.call(&Laser::start_emission);
    client.post_json("/laser/open_door");

    const auto data = sse.next();
    std::cout << "SSE data: " << (data ? tw::json::dump(*data) : "null") << "\n";
    if (!data || *data != Value{{"input", "enclosure door"}, {"was_emitting", true}}) {
        throw std::runtime_error("the trip should reach the SSE stream");
    }
    const auto message = ws.receive_json();
    std::cout << "WebSocket ← " << tw::json::dump(message) << "\n";
    if (message["data"]["tripped"]["data"] != *data) {
        throw std::runtime_error("the trip should reach the WebSocket");
    }
    const auto trip = in_cpp.recv_for(10s);
    if (!trip) {
        throw std::runtime_error("the trip should reach the C++ subscriber");
    }
    std::cout << "C++ subscriber: Trip { input: \"" << trip->input
              << "\", was_emitting: " << (trip->was_emitting ? "true" : "false") << " }\n";

    const auto td = client.get("/laser/").json();
    std::cout << "The TD's event: " << tw::json::dump_pretty(td["events"]) << "\n";
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
                          << "  curl -N http://127.0.0.1:" << bound << "/laser/tripped\n"
                          << "  curl -X POST http://127.0.0.1:" << bound << "/laser/open_door" << std::endl;
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
