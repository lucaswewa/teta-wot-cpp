// Dependency injection: a shared service, registered once with the server,
// is injected into actions as a `Dep<S>` parameter. A `Server` parameter
// gives the action the server: here, to embed every Thing's state
// (`thing_states()`) and the application configuration in each record.
//
// The C++ port of teta-wot's `dependency-injection`.

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <teta/wot.hpp>
#include <thread>
#include <vector>

namespace tw = teta::wot;
using tw::json::Value;
using namespace std::chrono_literals;

/// A record store shared by every Thing.
class DataStore {
  public:
    /// Adds a record; returns how many there are.
    std::size_t add(Value record) {
        std::lock_guard lock(mutex_);
        records_.push_back(std::move(record));
        return records_.size();
    }
    std::vector<Value> records() const {
        std::lock_guard lock(mutex_);
        return records_;
    }
    std::size_t size() const {
        std::lock_guard lock(mutex_);
        return records_.size();
    }

  private:
    mutable std::mutex mutex_;
    std::vector<Value> records_;
};

/// A thermometer.
class Thermometer {
  public:
    /// The temperature, in degrees Celsius.
    tw::Prop<double> temperature{21.5};

    std::size_t log(tw::Dep<DataStore> store) {
        return store->add(Value{{"temperature", temperature.get()}});
    }

    /// What other Things' records should say about this one.
    Value state() const { return Value{{"temperature", temperature.get()}}; }

    static void define(tw::ThingSpec<Thermometer>& t) {
        t.doc("A thermometer.");
        t.property("temperature", &Thermometer::temperature)
            .doc("The temperature, in degrees Celsius.")
            .read_only();
        t.action("log", &Thermometer::log).doc("Record the temperature.");
        t.thing_state(&Thermometer::state);
    }
};

/// A camera.
class Camera {
  public:
    /// The exposure time, in milliseconds.
    tw::Prop<double> exposure{10.0};

    std::size_t capture(tw::Dep<DataStore> store, tw::Server server) {
        Value metadata = Value::object();
        for (const auto& [name, state] : server.thing_states()) {
            metadata[name] = state;
        }
        return store->add(Value{{"image", "frame-" + std::to_string(store->size()) + ".jpg"},
                                {"metadata", metadata},
                                {"lab", server.application_config().value_or(nullptr)}});
    }

    Value state() const { return Value{{"exposure", exposure.get()}}; }

    static void define(tw::ThingSpec<Camera>& t) {
        t.doc("A camera.");
        t.property("exposure", &Camera::exposure).doc("The exposure time, in milliseconds.");
        t.action("capture", &Camera::capture)
            .doc("Capture an image, and record it with the state of every Thing.");
        t.thing_state(&Camera::state);
    }
};

void demo() {
    const auto store = std::make_shared<DataStore>();
    const auto server = tw::ThingServer::builder()
                            .thing<Thermometer>("thermometer")
                            .thing<Camera>("camera")
                            .service(store)
                            .application_config(Value{{"lab", "B12"}})
                            .build();
    const auto client = tw::testing::TestClient::start(server);

    // Over HTTP, `Dep<DataStore>` and `Server` aren't part of the input.
    const auto td = client.get("/camera/").json();
    std::cout << "capture's input schema: " << tw::json::dump(td["actions"]["capture"]["input"]["oneOf"][0])
              << "\n";

    for (const std::string_view path : {"/thermometer/log", "/camera/capture"}) {
        std::string href = client.post_json(path).json()["href"].get<std::string>();
        href.erase(0, std::string_view("http://testserver").size());
        for (;;) {
            const auto status = client.get(href).json()["status"];
            if (status != "pending" && status != "running") {
                break;
            }
            std::this_thread::sleep_for(5ms);
        }
    }
    // In-process calls get the same injection.
    const auto camera = *client.runtime()->thing_ref<Camera>("camera");
    camera.call(&Camera::capture);

    const auto records = store->records();
    std::cout << "\nThe store holds " << records.size() << " records:\n";
    for (const auto& record : records) {
        std::cout << "  " << tw::json::dump(record) << "\n";
    }
    if (records.size() != 3 || records[1]["metadata"]["thermometer"]["temperature"] != 21.5) {
        throw std::runtime_error("the store should hold three records, the second with the temperature");
    }
    client.stop();

    // A Thing that needs a service can't be built without it.
    try {
        (void)tw::ThingServer::builder().thing<Camera>("camera").build();
        throw std::runtime_error("the camera was built without its service");
    } catch (const tw::BuildError& error) {
        std::cout << "\nWithout the service: " << error.what() << "\n";
    }
}

int main() {
    try {
        demo();
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
