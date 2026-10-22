#include "teta/wot/thing_server.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <format>
#include <mutex>
#include <thread>

#include "teta/wot/drogon.hpp"
#include "teta/wot/log.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>

#include <cerrno>
#include <csignal>
#endif

namespace teta::wot {

namespace {

std::stop_source& shutdown_source() {
    static std::stop_source source;
    return source;
}

// Whether a server has finished shutting down, for console events after
// which Windows ends the process as soon as their handler returns.
std::mutex finished_mutex;
std::condition_variable finished_condition;
bool finished = false;

void mark_finished() {
    {
        std::lock_guard lock(finished_mutex);
        finished = true;
    }
    finished_condition.notify_all();
}

#ifdef _WIN32

BOOL WINAPI on_console_event(DWORD event) {
    const char* what = nullptr;
    switch (event) {
        case CTRL_C_EVENT:
            what = "received Ctrl-C";
            break;
        case CTRL_BREAK_EVENT:
            what = "received Ctrl-Break";
            break;
        case CTRL_CLOSE_EVENT:
            what = "the console is closing";
            break;
        case CTRL_LOGOFF_EVENT:
            what = "the user is logging off";
            break;
        case CTRL_SHUTDOWN_EVENT:
            what = "the system is shutting down";
            break;
        default:
            return FALSE;
    }
    log::info("{}", what);
    shutdown_source().request_stop();
    if (event == CTRL_CLOSE_EVENT || event == CTRL_LOGOFF_EVENT || event == CTRL_SHUTDOWN_EVENT) {
        // The process ends when this returns: let the shutdown finish first.
        std::unique_lock lock(finished_mutex);
        finished_condition.wait_for(lock, std::chrono::seconds(20), [] { return finished; });
    }
    return TRUE;
}

void install_handlers() {
    SetConsoleCtrlHandler(on_console_event, TRUE);
}

#else

int signal_pipe[2] = {-1, -1};

void on_signal(int number) {
    const char c = number == SIGINT ? 'i' : 't';
    [[maybe_unused]] const auto written = write(signal_pipe[1], &c, 1);
}

void install_handlers() {
    if (pipe(signal_pipe) != 0) {
        log::warn("can't install signal handlers: no pipe");
        return;
    }
    struct sigaction action{};
    action.sa_handler = on_signal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
    std::thread([] {
        char c = 0;
        while (read(signal_pipe[0], &c, 1) < 0 && errno == EINTR) {
        }
        log::info("{}", c == 'i' ? "received Ctrl-C" : "received SIGTERM");
        shutdown_source().request_stop();
    }).detach();
}

#endif

}  // namespace

std::stop_token shutdown_signal() {
    static std::once_flag installed;
    std::call_once(installed, install_handlers);
    return shutdown_source().get_token();
}

ThingServer ThingServerBuilder::build() {
    auto runtime = std::make_shared<Runtime>(runtime_.build());
    auto binding = std::make_shared<http::HttpBinding>(runtime, http_);
    return ThingServer(std::move(runtime), std::move(binding), grace_);
}

void ThingServer::serve(const std::string& host, std::uint16_t port) {
    serve(host, port, shutdown_signal());
}

void ThingServer::serve(const std::string& host, std::uint16_t port, std::stop_token stop,
                        std::function<void(std::uint16_t port)> listening) {
    try {
        runtime_->start();
    } catch (const StartupError& failure) {
        log::error("{}", failure.what());
        throw;
    }

    auto& app = drogon::app();
    const auto handler = drogon_adapter::mount(binding_, app);
    auto draining = std::make_shared<std::atomic<bool>>(false);
    const auto threads = std::max(2u, std::thread::hardware_concurrency());
    app.setLogLevel(trantor::Logger::kWarn)
        .addListener(host, port)
        .setThreadNum(threads)
        .disableSigtermHandling()
        // While draining, new connections are closed at once.
        .registerNewConnectionAdvice([draining](const trantor::InetAddress&, const trantor::InetAddress&) {
            return !draining->load();
        })
        .registerBeginningAdvice([this, host, port, listening] {
            const auto listeners = drogon::app().getListeners();
            const std::uint16_t bound = listeners.empty() ? port : listeners.front().toPort();
            log::info("serving {} Things on http://{}:{}", runtime_->things().size(), host, bound);
            if (listening) {
                listening(bound);
            }
        });

    std::atomic<bool> shutting_down = false;
    // The shutdown sequence, once `stop` is requested.
    std::jthread stopper([&, draining, handler](std::stop_token own) {
        std::mutex mutex;
        std::condition_variable_any wake;
        std::stop_callback on_stop(stop, [&] {
            std::lock_guard lock(mutex);
            wake.notify_all();
        });
        {
            std::unique_lock lock(mutex);
            wake.wait(lock, own, [&] { return stop.stop_requested(); });
        }
        if (!stop.stop_requested()) {
            return;  // Drogon stopped by itself
        }
        shutting_down = true;
        log::info("shutting down");
        draining->store(true);
        // Ends observation streams, which would otherwise keep their
        // connections open for the whole grace period.
        runtime_->broker()->close();
        runtime_->invocations().cancel_all();
        const auto deadline = std::chrono::steady_clock::now() + grace_;
        while (handler->in_flight() > 0 && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        const auto left = std::max(std::chrono::steady_clock::duration::zero(),
                                   deadline - std::chrono::steady_clock::now());
        const bool idle = runtime_->invocations().wait_idle(left);
        if (handler->in_flight() > 0 || !idle) {
            log::warn("requests or invocations were still running after {}",
                      std::chrono::duration<double>(grace_));
        }
        drogon::app().getLoop()->queueInLoop([] { drogon::app().quit(); });
    });

    app.run();

    stopper.request_stop();
    stopper.join();
    if (!shutting_down) {
        // Drogon stopped by itself.
        runtime_->shutdown(grace_);
        mark_finished();
        throw ServeError("Drogon stopped");
    }
    runtime_->stop();
    log::info("stopped");
    mark_finished();
}

}  // namespace teta::wot
