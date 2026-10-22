// A Thing defined with the definition API, driven through the registry.
//
// - a read-only data property (`position`) and a constrained, writable one
//   (`settle_ms`);
// - a functional property (`filter`) computed by a getter;
// - an action with typed input and output (`select`), and one without input
//   (`home`);
// - start and stop hooks.
//
// It then drives the Thing the way the HTTP binding will: through the
// registry, with JSON values. The C++ port of teta-wot's `builder_thing`
// example: it prints the same (but for the IDs and times).

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <teta/wot.hpp>
#include <teta/wot/td/validation.hpp>

namespace tw = teta::wot;
using tw::json::Value;

/// The filters in the wheel, by position.
constexpr std::array<const char*, 6> filters = {"open", "red", "green", "blue", "ND 1", "ND 2"};

struct Selected {
    std::uint8_t position = 0;
    std::string filter;
};

class FilterWheel {
  public:
    tw::Prop<std::uint8_t> position{0};
    tw::Prop<std::uint64_t> settle_ms =
        tw::Prop<std::uint64_t>(10).with_constraints(tw::td::Constraints{}.le(1000));

    std::string filter() const { return filters[position.get()]; }

    Selected select(tw::ActionCtx& ctx, std::uint8_t to) {
        if (to >= filters.size()) {
            throw tw::ActionError::handled("There is no position " + std::to_string(to) + ".");
        }
        ctx.sleep(std::chrono::milliseconds(settle_ms.get()));
        position.set(to);
        return {to, filters[to]};
    }

    void home() { position.set(0); }

    static void define(tw::ThingSpec<FilterWheel>& t) {
        t.title("FilterWheel").description("A six-position filter wheel.");
        t.property("position", &FilterWheel::position).read_only().doc("The current position.");
        t.property("settle_ms", &FilterWheel::settle_ms)
            .doc("Settling time.\n\nHow long to wait after each move.")
            .unit("ms");
        t.property("filter", &FilterWheel::filter).doc("The filter in the beam.");
        t.action<"position">("select", &FilterWheel::select)
            .doc("Move to a position.\n\nReturns the position and the filter there.")
            .param("position")
            .description("The position to move to, from 0 to 5.");
        t.action("home", &FilterWheel::home).doc("Move to position 0.");
        t.on_start([](FilterWheel&, tw::ThingCtx& ctx) { std::cerr << "[" << ctx.name() << "] starting\n"; });
        t.on_stop([](FilterWheel&, tw::ThingCtx& ctx) { std::cerr << "[" << ctx.name() << "] stopping\n"; });
    }
};

void check(bool condition, const char* what) {
    if (!condition) {
        throw std::runtime_error(what);
    }
}

void run() {
    auto runtime = tw::Runtime::builder().thing<FilterWheel>("wheel").build();
    runtime.start();
    const auto* wheel = runtime.thing("wheel");

    // Properties, as a client sees them.
    const auto& settle = *wheel->property("settle_ms");
    settle.write("20");  // lax coercion: "20" is accepted
    try {
        settle.write(5000);
        throw std::runtime_error("5000 is above the maximum");
    } catch (const tw::PropertyError& error) {
        std::cerr << "writing 5000 to settle_ms: " << error.what() << "\n";
    }

    // Actions: invoke, then wait for the invocation.
    const auto invocation = wheel->action("select")->invoke(Value{{"position", 2}});
    check(invocation->wait() == tw::InvocationStatus::completed, "select completes");
    std::cout << tw::json::dump_pretty(tw::to_json(invocation->record())) << "\n";
    check(wheel->property("filter")->read() == Value("green"), "the green filter");

    const auto refused = wheel->action("select")->invoke(Value{{"position", 9}});
    check(refused->wait() == tw::InvocationStatus::error, "position 9 is refused");
    std::cerr << "select 9: " << refused->error()->detail.value_or("") << "\n";

    const auto home = wheel->action("home")->invoke(nullptr);
    check(home->wait() == tw::InvocationStatus::completed, "home completes");

    // The Thing Description comes from the same definition.
    tw::td::TdOptions options;
    options.path = "/wheel/";
    const auto td = wheel->thing_description(options);
    tw::td::validation::validate_schema(td);
    std::cout << tw::json::dump_pretty(tw::td::to_json(td)) << "\n";
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
