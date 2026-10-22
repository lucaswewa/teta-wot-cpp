// Invocation logs.
//
// Every invocation runs in its scope; what code logs there (with `tw::log`,
// or with spdlog through the spdlog sink) goes into the invocation's log,
// which clients read when they poll the invocation. Events from a device call
// count too, because the device thread enters the caller's scope.
//
// The C++ port of teta-wot's `invocation-logs` example: it prints the same
// (but for the times and line numbers). The pump driver logs with spdlog, as
// a driver library might, when spdlog is available.

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <teta/wot.hpp>

#ifdef TETA_WOT_EXAMPLE_SPDLOG
#include <spdlog/spdlog.h>

#include <teta/wot/log_spdlog.hpp>
#define PUMP_INFO(...) SPDLOG_INFO(__VA_ARGS__)
#define PUMP_DEBUG(...) SPDLOG_DEBUG(__VA_ARGS__)
#else
#define PUMP_INFO(...) teta::wot::log::info(__VA_ARGS__)
#define PUMP_DEBUG(...) teta::wot::log::debug(__VA_ARGS__)
#endif

namespace tw = teta::wot;
using tw::json::Value;

/// A pump driver: synchronous, on its own thread.
class Pump {
  public:
    std::uint32_t prime() {
        PUMP_INFO("priming the pump");
        PUMP_DEBUG("valve states: open, closed, open");  // below INFO: not captured
        return 3;
    }
};

class Dispenser {
  public:
    tw::Device<Pump> pump;

    std::uint32_t dispense() {
        tw::log::info("dispensing");
        const auto strokes = pump.call([](Pump& p) { return p.prime(); });
        tw::log::warn("priming took more than one stroke strokes={}", strokes);
        return strokes;
    }

    void jam() {
        tw::log::info("dispensing");
        try {
            throw std::runtime_error("pressure 3.2 bar above the limit");
        } catch (...) {
            std::throw_with_nested(std::runtime_error("the nozzle is jammed"));
        }
    }

    static void define(tw::ThingSpec<Dispenser>& t) {
        t.device("pump", &Dispenser::pump);
        t.action("dispense", &Dispenser::dispense);
        t.action("jam", &Dispenser::jam);
    }
};

void check(bool condition, const char* what) {
    if (!condition) {
        throw std::runtime_error(what);
    }
}

void run() {
    // The console, and capture at INFO.
    tw::log::init(false);
#ifdef TETA_WOT_EXAMPLE_SPDLOG
    spdlog::default_logger()->sinks().push_back(std::make_shared<tw::log::SpdlogSink>());
    spdlog::set_level(spdlog::level::debug);
#endif
    auto runtime = tw::Runtime::builder().thing<Dispenser>("dispenser").build();
    runtime.start();
    const auto* thing = runtime.thing("dispenser");

    const auto ok = thing->action("dispense")->invoke(nullptr);
    ok->wait();
    const auto log = ok->logs();
    Value records = Value::array();
    for (const auto& record : log) {
        records.push_back(tw::to_json(record));
    }
    std::cout << "dispense log:\n" << tw::json::dump_pretty(records) << "\n";
    check(log.size() == 3, "three records: DEBUG isn't captured");
    check(log[0].message == "dispensing", "the action's own event");
    check(log[1].message == "priming the pump", "the device thread's event");
    check(log[2].message == "priming took more than one stroke strokes=3", "the warning");
    check(log[2].levelname == "WARNING", "a warning");

    const auto failed = thing->action("jam")->invoke(nullptr);
    failed->wait();
    const auto last = failed->logs().back();
    std::cout << "jam: [" << last.exception_type.value_or("-") << "] " << last.message << "\ntraceback:\n"
              << last.traceback.value_or("-") << "\n";
    check(last.levelname == "ERROR", "an error");
    check(last.traceback.has_value(), "errors are logged with their cause chain");
    runtime.stop();
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
