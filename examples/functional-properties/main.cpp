// Functional properties: properties whose value a member function computes.
//
// A temperature controller shows each kind:
//
// - `setpoint`: a getter, a setter and a resetter, a default and constraints,
//   which are checked before the setter runs;
// - `temperature`: a slow getter (every getter runs on the calling worker
//   thread, so a slow synchronous read is fine: `blocking` isn't needed);
// - `heating`: a getter only, so read-only;
// - `power_limit`: `read_only` with a setter, which C++ code may call but
//   clients may not.
//
// The C++ port of teta-wot's `functional-properties` example: the same
// requests, through TestClient.

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <functional>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <teta/wot.hpp>
#include <thread>

namespace tw = teta::wot;
using tw::json::Value;

/// A temperature controller with a simulated heater.
class Controller {
  public:
    double setpoint() const {
        std::lock_guard lock(mutex_);
        return setpoint_;
    }
    void set_setpoint(double value) {
        std::lock_guard lock(mutex_);
        setpoint_ = value;
    }
    void reset_setpoint() { set_setpoint(20.0); }

    double temperature() const {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));  // a slow sensor
        return 18.5;
    }

    bool heating() const { return 18.5 < setpoint(); }

    std::uint8_t power_limit() const {
        std::lock_guard lock(mutex_);
        return power_limit_;
    }
    void set_power_limit(std::uint8_t value) {
        std::lock_guard lock(mutex_);
        power_limit_ = value;
    }

    static void define(tw::ThingSpec<Controller>& t) {
        t.doc("A temperature controller with a simulated heater.");
        t.property("setpoint", &Controller::setpoint)
            .setter(&Controller::set_setpoint)
            .resetter(&Controller::reset_setpoint)
            .default_value(20.0)
            .doc("The target temperature.")
            .ge(5)
            .le(30)
            .unit("degree Celsius");
        t.property("temperature", &Controller::temperature)
            .doc("The measured temperature.\n\nThe sensor is read synchronously and slowly.")
            .unit("degree Celsius");
        t.property("heating", &Controller::heating).doc("Whether the heater is on.");
        t.property("power_limit", &Controller::power_limit)
            .setter(&Controller::set_power_limit)
            .default_value(100)
            .read_only()
            .doc("The heater's power limit, in percent. Only the Thing's code sets it.");
    }

  private:
    mutable std::mutex mutex_;
    double setpoint_ = 20.0;
    std::uint8_t power_limit_ = 100;
};

void step(const std::string& label, int status, int expected) {
    std::cout << std::format("{:<38} → {}\n", label, status);
    if (status != expected) {
        throw std::runtime_error("expected " + std::to_string(expected));
    }
}

void run() {
    const auto server = tw::ThingServer::builder().thing<Controller>("controller").build();
    const auto client = tw::testing::TestClient::start(server);

    const auto td = client.get("/controller/").json();
    for (const char* name : {"setpoint", "temperature", "heating", "power_limit"}) {
        const auto& p = td["properties"][name];
        std::cout << std::format("{:<12} readOnly {:<5} default {:<5} unit {}\n", name,
                                 tw::json::dump(p["readOnly"]),
                                 p.contains("default") ? tw::json::dump(p["default"]) : "null",
                                 p.contains("unit") ? tw::json::dump(p["unit"]) : "null");
    }

    step("PUT setpoint 25 (setter runs)", client.put_json("/controller/setpoint", 25).status, 201);
    const auto read = client.get("/controller/setpoint").json();
    std::cout << "GET setpoint                           → " << tw::json::dump(read) << "\n";
    if (read != Value(25.0)) {
        throw std::runtime_error("the setter didn't run");
    }
    step("PUT setpoint 40 (above le = 30)", client.put_json("/controller/setpoint", 40).status, 422);
    step("POST setpoint/reset (resetter runs)", client.post_json("/controller/setpoint/reset").status, 200);
    if (client.get("/controller/setpoint").json() != Value(20.0)) {
        throw std::runtime_error("the resetter didn't run");
    }

    std::cout << "GET temperature (a slow getter)        → " << client.get("/controller/temperature").text()
              << "\n";
    step("PUT heating (no setter)", client.put_json("/controller/heating", true).status, 405);
    step("PUT power_limit (readonly)", client.put_json("/controller/power_limit", 50).status, 405);

    // The Thing's own code may still use the setter.
    client.runtime()->thing_ref<Controller>("controller")->thing().set_power_limit(50);
    const auto limit = client.get("/controller/power_limit").json();
    std::cout << "set_power_limit(50) from C++, then GET power_limit → " << tw::json::dump(limit) << "\n";
    if (limit != Value(50)) {
        throw std::runtime_error("the setter didn't run");
    }
    client.stop();
}

int main() {
    try {
        run();
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
