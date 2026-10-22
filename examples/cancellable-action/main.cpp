// Cooperative cancellation.
//
// Three long actions on a simulated scanner, each cancelled from another
// thread the way `DELETE /action_invocations/{id}` will cancel them:
//
// 1. `scan` checks for cancellation while it works and stops (`cancelled`);
// 2. `scan_or_save` handles the cancellation, saves what it has and finishes
//    normally (`completed`);
// 3. `batch` runs a child invocation; cancelling the parent cancels the
//    child, and the parent ends `cancelled` once the child has stopped.
//
// The C++ port of teta-wot's `cancellable-action` example: it prints the same
// (the number of lines saved depends on timing).

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <teta/wot.hpp>
#include <thread>

namespace tw = teta::wot;
using tw::json::Value;
using namespace std::chrono_literals;

class Scanner {
  public:
    tw::Prop<std::uint32_t> lines{0};

    /// Scan some lines, stopping if cancelled.
    std::uint32_t scan(tw::ActionCtx& ctx, std::uint32_t count) {
        for (std::uint32_t line = 0; line < count; ++line) {
            // Throws Cancelled, which ends the invocation as `cancelled`.
            ctx.sleep(10ms);
            lines.set(line + 1);
        }
        return count;
    }

    /// Scan some lines, saving the partial scan if cancelled.
    Value scan_or_save(tw::ActionCtx& ctx, std::uint32_t count) {
        for (std::uint32_t line = 0; line < count; ++line) {
            try {
                ctx.sleep(10ms);
            } catch (const tw::Cancelled&) {
                // The cancellation was consumed: carry on, and finish.
                tw::log::info("cancelled after {} lines; saving them", line);
                return {{"saved", line}, {"complete", false}};
            }
            lines.set(line + 1);
        }
        return {{"saved", count}, {"complete", true}};
    }

    /// Run a long child invocation.
    void batch(tw::ActionCtx& ctx) {
        auto child = ctx.spawn_child([](tw::ActionCtx& child_ctx) {
            for (int i = 0; i < 1000; ++i) {
                child_ctx.sleep(10ms);
            }
        });
        tw::log::info("waiting for child invocation {}", child.id().to_string());
        child.join();
    }

    static void define(tw::ThingSpec<Scanner>& t) {
        t.property("lines", &Scanner::lines).read_only();
        t.action<"lines">("scan", &Scanner::scan).doc("Scan some lines, stopping if cancelled.");
        t.action<"lines">("scan_or_save", &Scanner::scan_or_save)
            .doc("Scan some lines, saving the partial scan if cancelled.");
        t.action("batch", &Scanner::batch).doc("Run a long child invocation.");
    }
};

void run() {
    tw::log::init(false);
    auto runtime = tw::Runtime::builder().thing<Scanner>("scanner").build();
    runtime.start();
    const auto* scanner = runtime.thing("scanner");
    const std::tuple<const char*, Value, tw::InvocationStatus> cases[] = {
        {"scan", {{"lines", 1000}}, tw::InvocationStatus::cancelled},
        {"scan_or_save", {{"lines", 1000}}, tw::InvocationStatus::completed},
        {"batch", nullptr, tw::InvocationStatus::cancelled},
    };
    for (const auto& [action, input, expected] : cases) {
        const auto invocation = scanner->action(action)->invoke(input);
        std::this_thread::sleep_for(55ms);
        // What `DELETE /action_invocations/{id}` does:
        runtime.invocations().cancel(invocation->id());
        const auto status = invocation->wait();
        const auto error = invocation->error();
        std::cout << action << ": " << tw::to_string(status) << ", output "
                  << tw::json::dump(invocation->output().value_or(nullptr)) << ", error "
                  << (error && error->title ? *error->title : "") << "\n";
        if (status != expected) {
            throw std::runtime_error(std::string(action) + " ended " + std::string(tw::to_string(status)));
        }
        // A finished invocation can't be cancelled again.
        try {
            runtime.invocations().cancel(invocation->id());
        } catch (const tw::CancelError& refused) {
            std::cout << "  cancelling again: " << refused.what() << "\n";
        }
    }
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
