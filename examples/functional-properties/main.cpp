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
// The C++ port of teta-wot's `functional-properties` example, in-process: the
// HTTP requests become calls on the property registry (HTTP comes in Phase 4).

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <format>
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

void step(const std::string& label, const std::string& outcome, const std::string& expected) {
    std::cout << std::format("{:<38} → {}\n", label, outcome);
    if (outcome != expected) {
        throw std::runtime_error("expected " + expected);
    }
}

// What a client's write gets: written, or why not.
std::string outcome(const std::function<void()>& request) {
    try {
        request();
        return "done";
    } catch (const tw::PropertyError& error) {
        switch (error.kind()) {
            case tw::PropertyError::Kind::invalid:
                return "invalid";
            case tw::PropertyError::Kind::read_only:
                return "read-only";
            default:
                return error.what();
        }
    }
}

void run() {
    auto runtime = tw::Runtime::builder().thing<Controller>("controller").build();
    const auto* controller = runtime.thing("controller");

    tw::td::TdOptions options;
    options.path = "/controller/";
    const auto td = tw::td::to_json(controller->thing_description(options));
    for (const char* name : {"setpoint", "temperature", "heating", "power_limit"}) {
        const auto& p = td["properties"][name];
        std::cout << std::format("{:<12} readOnly {:<5} default {:<5} unit {}\n", name,
                                 tw::json::dump(p["readOnly"]),
                                 p.contains("default") ? tw::json::dump(p["default"]) : "null",
                                 p.contains("unit") ? tw::json::dump(p["unit"]) : "null");
    }

    const auto& setpoint = *controller->property("setpoint");
    step("write setpoint 25 (setter runs)", outcome([&] { setpoint.write(25); }), "done");
    std::cout << "read setpoint                          → " << tw::json::dump(setpoint.read()) << "\n";
    step("write setpoint 40 (above le = 30)", outcome([&] { setpoint.write(40); }), "invalid");
    step("reset setpoint (resetter runs)", outcome([&] { setpoint.reset(); }), "done");
    if (setpoint.read() != Value(20.0)) {
        throw std::runtime_error("the resetter didn't run");
    }

    std::cout << "read temperature (a slow getter)       → "
              << tw::json::dump(controller->property("temperature")->read()) << "\n";
    step("write heating (no setter)", outcome([&] { controller->property("heating")->write(true); }),
         "read-only");
    step("write power_limit (read-only)", outcome([&] { controller->property("power_limit")->write(50); }),
         "read-only");

    // The Thing's own code may still use the setter.
    controller->instance<Controller>()->set_power_limit(50);
    const auto limit = controller->property("power_limit")->read();
    std::cout << "set_power_limit(50) from C++, then read power_limit → " << tw::json::dump(limit) << "\n";
    if (limit != Value(50)) {
        throw std::runtime_error("the setter didn't run");
    }
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
