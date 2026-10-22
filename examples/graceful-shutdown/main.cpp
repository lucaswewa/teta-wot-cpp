// Graceful shutdown.
//
// Two Things, each with a device. One runs a long action. When the server is
// asked to stop, it:
//
// 1. stops accepting connections, and lets requests in progress finish;
// 2. cancels unfinished invocations and waits (up to the grace period) for
//    them to end;
// 3. stops the Things in reverse order, each before its devices close.
//
//     example-graceful-shutdown              # stops itself after a moment
//     example-graceful-shutdown --wait       # waits for Ctrl-C, Ctrl-Break or closing the console
//
// The C++ port of teta-wot's `graceful-shutdown`.

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <teta/wot.hpp>
#include <thread>

namespace tw = teta::wot;
using namespace std::chrono_literals;

const auto started = std::chrono::steady_clock::now();
std::mutex say_mutex;

void say(const std::string& line) {
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
    std::lock_guard lock(say_mutex);
    std::cout << std::format("[{:>5} ms] {}", elapsed.count(), line) << std::endl;
}

/// A shutter driver: closing it is what must not be skipped.
class Shutter {
  public:
    explicit Shutter(std::string name) : name_(std::move(name)) {}
    void open() { say(name_ + ": shutter opened (on \"" + tw::device_thread_name() + "\")"); }
    void close() { say(name_ + ": shutter closed"); }

  private:
    std::string name_;
};

class Camera {
  public:
    explicit Camera(const std::string& name) : name_(name), shutter([name] { return Shutter(name); }) {}

    void time_lapse(tw::ActionCtx& ctx) {
        int frames = 0;
        for (;;) {
            try {
                ctx.sleep(100ms);
            } catch (const tw::Cancelled&) {
                say(std::format("{}: time lapse cancelled after {} frames; finishing the file", name_,
                                frames));
                throw;
            }
            ++frames;
        }
    }

    void start(tw::ThingCtx& ctx) { say(ctx.name() + ": started"); }
    void stop(tw::ThingCtx& ctx) { say(ctx.name() + ": stopping"); }

    static void define(tw::ThingSpec<Camera>& t) {
        t.title("Camera");
        t.device("shutter", &Camera::shutter);
        t.action("time_lapse", &Camera::time_lapse);
        t.on_start(&Camera::start);
        t.on_stop(&Camera::stop);
    }

  private:
    std::string name_;

  public:
    tw::Device<Shutter> shutter;
};

int run(bool wait_for_signal) {
    say("building the server");
    auto server = tw::ThingServer::builder()
                      .thing("left", std::make_shared<Camera>("left"))
                      .thing("right", std::make_shared<Camera>("right"))
                      .shutdown_grace(3s)
                      .build();
    const auto runtime = server.runtime();

    // The stop request: the process's (Ctrl-C and the like), or a timer.
    std::stop_source timer;
    const std::stop_token stop = wait_for_signal ? tw::shutdown_signal() : timer.get_token();
    std::stop_callback requested(stop, [] { say("shutdown requested"); });

    std::promise<std::uint16_t> listening;
    std::thread serving([&] {
        try {
            server.serve("127.0.0.1", 0, stop, [&](std::uint16_t port) { listening.set_value(port); });
        } catch (...) {
            try {
                listening.set_exception(std::current_exception());
            } catch (const std::future_error&) {
                // It was listening: the error is lost.
            }
        }
    });
    std::uint16_t port = 0;
    try {
        port = listening.get_future().get();
    } catch (...) {
        serving.join();
        throw;
    }
    say(std::format("serving on http://127.0.0.1:{}", port));

    const auto invocation = runtime->thing("left")->action("time_lapse")->invoke(nullptr);
    say("left: time lapse started");

    std::jthread stopper;
    if (wait_for_signal) {
        say("press Ctrl-C (or Ctrl-Break, or close the console) to stop");
    } else {
        stopper = std::jthread([&timer] {
            std::this_thread::sleep_for(500ms);
            timer.request_stop();
        });
    }
    serving.join();
    say(std::format("left's time lapse ended as `{}`", tw::to_string(invocation->status())));
    if (invocation->status() != tw::InvocationStatus::cancelled) {
        throw std::runtime_error("the invocation should be cancelled");
    }
    return EXIT_SUCCESS;
}

int main(int argc, char** argv) {
    try {
        return run(argc > 1 && std::string_view(argv[1]) == "--wait");
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
