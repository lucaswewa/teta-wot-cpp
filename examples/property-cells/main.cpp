// `Prop<T>`: the typed cells that hold a Thing's data properties.
//
// Shows constraints and the pydantic-shaped errors they produce, change
// observers (a C++ watcher and the message broker a client would use), lax
// coercion of client values, and what `read_only` does and doesn't restrict.
// The C++ port of teta-wot's `property-cells` example: it prints the same.

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <teta/wot.hpp>

namespace tw = teta::wot;
using tw::json::Value;

class Heater {
  public:
    /// Set point in °C, 20 to 90.
    tw::Prop<double> target = tw::Prop<double>(37.0).with_constraints(tw::td::Constraints{}.ge(20).le(90));
    /// A label, lower-case letters only.
    tw::Prop<std::string> label =
        tw::Prop<std::string>(std::string("incubator"))
            .with_constraints(tw::td::Constraints{}.min_length(1).max_length(16).pattern("^[a-z]+$"));
    /// Firmware version: clients may read it, only the Thing sets it.
    tw::Prop<std::string> firmware{std::string("1.0.0")};

    static void define(tw::ThingSpec<Heater>& t) {
        t.property("target", &Heater::target).doc("Set point.").unit("degree Celsius");
        t.property("label", &Heater::label).doc("A short label.");
        t.property("firmware", &Heater::firmware).read_only().doc("Firmware version.");
    }
};

void check(bool condition, const char* what) {
    if (!condition) {
        throw std::runtime_error(what);
    }
}

std::string detail(const tw::ValidationError& error) {
    Value issues = Value::array();
    for (const auto& issue : error.issues()) {
        issues.push_back(tw::to_json(issue));
    }
    return tw::json::dump(issues);
}

void run() {
    auto runtime = tw::Runtime::builder().thing<Heater>("heater").build();
    const auto* handle = runtime.thing("heater");
    const auto heater = handle->instance<Heater>();

    // 1. C++ code sets values; constraints are checked, types by the compiler.
    auto watcher = heater->target.subscribe();
    heater->target.set(42.5);
    check(watcher.has_changed(), "the watcher sees the change");
    std::cout << "watcher saw target = " << tw::rust_display(watcher.get()) << "\n";

    try {
        heater->target.set(120.0);
        throw std::runtime_error("120 is above the maximum");
    } catch (const tw::PropertyError& error) {
        std::cout << "set(120.0) failed: " << error.what() << "\n";
    }
    check(heater->target.get() == 42.5, "an invalid value is never stored");

    // 2. Clients write JSON through the registry, and get pydantic's errors.
    const auto& target = *handle->property("target");
    auto broker = runtime.broker()->subscribe("heater", "target");
    target.write("55");  // "55" is coerced to 55.0, as pydantic does
    std::cout << "broker saw target = " << tw::json::dump(broker.recv()->payload) << "\n";

    for (const Value& bad : {Value(10), Value("hot"), Value(nullptr)}) {
        try {
            target.write(bad);
            throw std::runtime_error(tw::json::dump(bad) + " should have been refused");
        } catch (const tw::PropertyError& error) {
            check(error.kind() == tw::PropertyError::Kind::invalid, "a validation error");
            std::cout << "PUT " << tw::json::dump(bad) << " → 422 {\"detail\":" << detail(*error.validation())
                      << "}\n";
        }
    }

    try {
        handle->property("label")->write("Lab 3");
        throw std::runtime_error("an upper-case label should be refused");
    } catch (const tw::PropertyError& error) {
        std::cout << "PUT \"Lab 3\" → 422 " << detail(*error.validation()) << "\n";
    }

    // 3. Read-only restricts clients, not the Thing.
    const auto& firmware = *handle->property("firmware");
    try {
        firmware.write("2.0.0");
        throw std::runtime_error("firmware is read-only");
    } catch (const tw::PropertyError& error) {
        check(error.kind() == tw::PropertyError::Kind::read_only, "refused as read-only");
    }
    check(!firmware.is_resettable(), "clients can't reset it either");
    heater->firmware.set("1.1.0");
    std::cout << "firmware is now " << tw::json::dump(firmware.read()) << "\n";

    // 4. Reset goes back to the default.
    target.reset();
    check(target.read() == Value(37.0), "reset to the default");
    std::cout << "target reset to " << tw::json::dump(target.read()) << "\n";
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
