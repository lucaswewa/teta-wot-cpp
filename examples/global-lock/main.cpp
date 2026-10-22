// The optional global lock.
//
// With the lock enabled, actions and client property writes take it, so they
// happen one at a time; anything that can't get it within 50 ms is refused.
// The lock belongs to an invocation, not a thread, so an action can take it
// again. Actions can opt out.
//
// The C++ port of teta-wot's `global-lock` example: it prints the same.

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <teta/wot.hpp>
#include <thread>

namespace tw = teta::wot;
using tw::json::Value;
using namespace std::chrono_literals;

class Microscope {
  public:
    tw::Prop<double> exposure{10.0};

    std::string acquire(tw::ActionCtx& ctx) {
        // The invocation already holds the lock; taking it again (as a nested
        // call would) doesn't block.
        const auto again = ctx.hold_global_lock();
        ctx.sleep(300ms);
        return "image acquired";
    }
    void autofocus(tw::ActionCtx& ctx) { ctx.sleep(10ms); }
    std::string status() const { return "exposure " + tw::rust_display(exposure.get()) + " ms"; }

    static void define(tw::ThingSpec<Microscope>& t) {
        t.property("exposure", &Microscope::exposure);
        t.action("acquire", &Microscope::acquire);
        t.action("autofocus", &Microscope::autofocus);
        // Harmless to run at any time: opt out of the lock.
        t.action("status", &Microscope::status).global_lock(false);
    }
};

void check(bool condition, const char* what) {
    if (!condition) {
        throw std::runtime_error(what);
    }
}

void run() {
    tw::log::init(false);
    auto runtime = tw::Runtime::builder().global_lock(true).thing<Microscope>("scope").build();
    runtime.start();
    const auto* scope = runtime.thing("scope");
    const auto acquire = scope->action("acquire")->invoke(nullptr);
    std::this_thread::sleep_for(50ms);

    // While `acquire` holds the lock, another action can't start...
    const auto focus = scope->action("autofocus")->invoke(nullptr);
    check(focus->wait() == tw::InvocationStatus::error, "autofocus is refused");
    const auto problem = *focus->error();
    const auto logs = focus->logs();
    std::cout << "autofocus: " << problem.title.value_or("") << " (" << problem.status.value_or(0)
              << "), log: " << (logs.empty() ? "None" : "Some(" + tw::json::dump(logs.back().message) + ")")
              << "\n";

    // ...and a client can't write a property (HTTP 409)...
    try {
        scope->property("exposure")->write(20.0);
        throw std::runtime_error("the write should be refused");
    } catch (const tw::PropertyError& error) {
        std::cout << "PUT exposure: " << error.what() << " (status Some("
                  << error.problem().status.value_or(0) << "))\n";
    }

    // ...but an action that opted out runs.
    const auto status = scope->action("status")->invoke(nullptr);
    check(status->wait() == tw::InvocationStatus::completed, "status runs");
    std::cout << "status: " << tw::json::dump(status->output().value_or(nullptr)) << "\n";

    // `acquire` took the lock twice (reentrant) and finishes normally.
    check(acquire->wait() == tw::InvocationStatus::completed, "acquire completes");
    std::cout << "acquire: " << tw::json::dump(acquire->output().value_or(nullptr)) << "\n";

    // Once it is released, everything works again.
    scope->property("exposure")->write(20.0);
    const auto again = scope->action("autofocus")->invoke(nullptr);
    check(again->wait() == tw::InvocationStatus::completed, "autofocus runs");
    std::cout << "after release: exposure set, autofocus " << tw::to_string(again->status()) << "\n";
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
