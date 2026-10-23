#include "teta/wot/thing_server.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <format>
#include <mutex>
#include <system_error>
#include <thread>

#include "teta/wot/drogon.hpp"
#ifdef TETA_WOT_MDNS
#include "mdns.hpp"
#endif
#include "teta/wot/log.hpp"
#include "teta/wot/settings.hpp"
#include "teta/wot/systemd.hpp"
#include "teta/wot/uuid.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
// After winsock2.h.
#include <windows.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
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

// ---- Listening ------------------------------------------------------------------------

#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket no_socket = INVALID_SOCKET;
int socket_error() {
    return WSAGetLastError();
}
void close_socket(Socket s) {
    closesocket(s);
}
#else
using Socket = int;
constexpr Socket no_socket = -1;
int socket_error() {
    return errno;
}
void close_socket(Socket s) {
    close(s);
}
#endif

// An OS error as the Rust standard library describes it.
std::string os_error(int code) {
    std::string text = std::system_category().message(code);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) {
        text.pop_back();
    }
    return text + " (os error " + std::to_string(code) + ")";
}

// The text of a socket address, as Rust's `SocketAddr` writes it.
std::string address_text(const std::string& ip, std::uint16_t port) {
    return (ip.find(':') != std::string::npos ? "[" + ip + "]" : ip) + ":" + std::to_string(port);
}

// Claims Drogon's application, which runs once per process. Throws
// ServeError if it has run.
void claim_drogon() {
    static std::atomic<bool> claimed = false;
    if (claimed.exchange(true)) {
        throw ServeError("Drogon's application has already run in this process: a process can serve once");
    }
}

// Runs Drogon with `handler` (already mounted) on `address` until `stop` is
// requested. Once it is, `drain` runs (while new connections are refused),
// then Drogon quits. `listening` runs once Drogon listens, with the port.
// Returns false if Drogon stopped by itself.
bool run_drogon(const detail::ListenAddress& address, std::stop_token stop,
                const std::function<void(std::uint16_t)>& listening, const std::function<void()>& drain) {
    auto& app = drogon::app();
    auto draining = std::make_shared<std::atomic<bool>>(false);
    const auto threads = std::max(2u, std::thread::hardware_concurrency());
    app.setLogLevel(trantor::Logger::kWarn)
        .addListener(address.ip, address.port)
        .setThreadNum(threads)
        .disableSigtermHandling()
        // While draining, new connections are closed at once.
        .registerNewConnectionAdvice([draining](const trantor::InetAddress&, const trantor::InetAddress&) {
            return !draining->load();
        })
        .registerBeginningAdvice([address, listening] {
            const auto listeners = drogon::app().getListeners();
            const std::uint16_t bound = listeners.empty() ? address.port : listeners.front().toPort();
            if (listening) {
                listening(bound);
            }
        });

    std::atomic<bool> stopped = false;
    std::jthread stopper([&, draining](std::stop_token own) {
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
        stopped = true;
        draining->store(true);
        if (drain) {
            drain();
        }
        drogon::app().getLoop()->queueInLoop([] { drogon::app().quit(); });
    });

    // Drogon makes 256 directories for uploads when it starts, under
    // `./uploads` by default: in the working directory, which for a Windows
    // service is System32. The binding reads bodies itself, so they go to a
    // temporary folder, removed afterwards.
    std::error_code error;
    auto uploads = std::filesystem::temp_directory_path(error);
    uploads /= "teta-wot-drogon-" + Uuid::v4().to_string();
    app.setUploadPath(detail::path_text(uploads));

    app.run();

    stopper.request_stop();
    stopper.join();
    std::filesystem::remove_all(uploads, error);
    return stopped;
}

}  // namespace

namespace detail {

ListenAddress check_listen(const std::string& host, std::uint16_t port) {
#ifdef _WIN32
    WSADATA data;
    if (const int failed = WSAStartup(MAKEWORD(2, 2), &data); failed != 0) {
        throw std::runtime_error("can't listen on " + host + ":" + std::to_string(port) + ": " +
                                 os_error(failed));
    }
    struct Cleanup {
        ~Cleanup() { WSACleanup(); }
    } cleanup;
#endif
    // The host's address: an IP literal as it is, or the first a name
    // resolves to.
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    const std::string service = std::to_string(port);
    if (const int failed = getaddrinfo(host.c_str(), service.c_str(), &hints, &found);
        failed != 0 || !found) {
#ifdef _WIN32
        const std::string reason = os_error(failed);
#else
        const std::string reason = gai_strerror(failed);
#endif
        throw std::runtime_error("can't resolve " + host + ":" + service + ": " + reason);
    }
    char ip[INET6_ADDRSTRLEN] = {};
    const void* raw =
        found->ai_family == AF_INET6
            ? static_cast<const void*>(&reinterpret_cast<sockaddr_in6*>(found->ai_addr)->sin6_addr)
            : static_cast<const void*>(&reinterpret_cast<sockaddr_in*>(found->ai_addr)->sin_addr);
    inet_ntop(found->ai_family, raw, ip, sizeof ip);
    ListenAddress address{ip, port, address_text(ip, port)};

    // Port 0 is any free port: there is nothing to check.
    std::string problem;
    if (port != 0) {
        const Socket s = socket(found->ai_family, SOCK_STREAM, IPPROTO_TCP);
        if (s == no_socket) {
            problem = os_error(socket_error());
        } else {
#ifndef _WIN32
            // As tokio and trantor listen: a port in TIME_WAIT can be reused.
            const int reuse = 1;
            setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);
#endif
            if (bind(s, found->ai_addr, static_cast<int>(found->ai_addrlen)) != 0) {
                problem = os_error(socket_error());
            }
            close_socket(s);
        }
    }
    freeaddrinfo(found);
    if (!problem.empty()) {
        throw std::runtime_error("can't listen on " + address.text + ": " + problem);
    }
    return address;
}

}  // namespace detail

std::stop_token shutdown_signal() {
    static std::once_flag installed;
    std::call_once(installed, install_handlers);
    return shutdown_source().get_token();
}

ThingServerBuilder ThingServer::from_config(const ServerConfig& config, const ThingRegistry& registry) {
    for (const auto& key : config.ignored_keys) {
        log::warn("the configuration key `{}` isn't used, and is ignored", key);
    }
    ThingServerBuilder builder;
    builder.api_prefix(config.api_prefix)
        .global_lock(config.enable_global_lock)
        .global_lock_log_level(config.global_lock_log_level)
        .settings_folder(detail::path_from_text(config.settings_folder_or_default()));
    if (config.application_config) {
        builder.application_config(*config.application_config);
    }
    if (config.server_id) {
        builder.server_id(*config.server_id);
    }
    if (config.wire_profile) {
        builder.wire_profile(*config.wire_profile);
    }
    if (config.security) {
        const auto& security = *config.security;
        builder.security(security.scheme == SecurityConfig::Scheme::basic
                             ? http::Security::basic(security.username, security.secret())
                             : http::Security::bearer(security.secret()));
    }
    builder.mdns(config.mdns);
    std::vector<std::pair<std::string, std::string>> errors;
    for (const auto& [name, thing] : config.things) {
        if (!registry.add_thing(builder.runtime(), name, thing.cls, thing.args, thing.kwargs)) {
            std::string registered;
            for (const auto& known : registry.names()) {
                registered += (registered.empty() ? "" : ", ") + known;
            }
            errors.emplace_back("things." + name + ".cls", "No Thing type is registered as '" + thing.cls +
                                                               "'. Registered: " + registered);
            continue;
        }
        if (!thing.thing_slots.empty()) {
            builder.thing_slots(name, thing.thing_slots);
        }
    }
    if (!errors.empty()) {
        throw ConfigError(std::move(errors));
    }
    return builder;
}

ThingServer ThingServerBuilder::build() {
    auto runtime = std::make_shared<Runtime>(runtime_.build());
    auto binding = std::make_shared<http::HttpBinding>(runtime, http_);
    std::optional<std::string> advertise;
    if (mdns_) {
        advertise = http_.server_id;
    }
    return ThingServer(std::move(runtime), std::move(binding), grace_, std::move(advertise));
}

void ThingServer::serve(const std::string& host, std::uint16_t port) {
    serve(host, port, shutdown_signal());
}

bool mdns_supported() {
#ifdef TETA_WOT_MDNS
    return true;
#else
    return false;
#endif
}

void ThingServer::serve(const std::string& host, std::uint16_t port, std::stop_token stop,
                        std::function<void(std::uint16_t port)> listening) {
    detail::ListenAddress address;
    try {
        address = detail::check_listen(host, port);
    } catch (const std::runtime_error& error) {
        throw ServeError(error.what());
    }
    try {
        runtime_->start();
    } catch (const StartupError& failure) {
        log::error("{}", failure.what());
        throw;
    }
    try {
        claim_drogon();
    } catch (const ServeError&) {
        runtime_->stop();
        throw;
    }
#ifdef TETA_WOT_MDNS
    std::unique_ptr<detail::MdnsAdvertisement> advertisement;
#endif
    // Advertises the server with DNS-SD, if asked to, once it listens.
    // Failing to advertise is logged; the server serves anyway.
    const auto advertise = [&](std::uint16_t bound) {
        if (!advertise_) {
            return;
        }
#ifdef TETA_WOT_MDNS
        try {
            advertisement = detail::MdnsAdvertisement::start(*advertise_, bound);
            log::info("advertising `{}` as {} on port {}", *advertise_, directory_service_type, bound);
        } catch (const std::exception& error) {
            log::warn("the server can't be advertised with mDNS: {}", error.what());
        }
#else
        (void)bound;
        log::warn("`{}` can't be advertised with mDNS: the server was built without TETA_WOT_MDNS",
                  *advertise_);
#endif
    };

    const auto handler = drogon_adapter::mount(binding_, drogon::app());
    // The application is the server's alone: the binding answers every
    // request, before Drogon's routing (and its static files), but the
    // Things' WebSockets, which their controller serves.
    drogon_adapter::answer_all(
        handler, drogon::app(), [binding = binding_](const drogon::HttpRequest& request) {
            return http::equals_ignoring_case(request.getHeader("upgrade"), "websocket") &&
                   binding->is_websocket(drogon_adapter::to_request(request));
        });
    const bool stopped = run_drogon(
        address, stop,
        [&](std::uint16_t bound) {
            log::info("serving {} Things on http://{}", runtime_->things().size(),
                      address_text(address.ip, bound));
            advertise(bound);
            // A `Type=notify` systemd unit has started.
            systemd::notify_ready();
            if (listening) {
                listening(bound);
            }
        },
        [&] {
            log::info("shutting down");
            systemd::notify("STOPPING=1");
#ifdef TETA_WOT_MDNS
            if (advertisement) {
                advertisement->stop();
            }
#endif
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
        });
    if (!stopped) {
        runtime_->shutdown(grace_);
        mark_finished();
        throw ServeError("Drogon stopped");
    }
    runtime_->stop();
    log::info("stopped");
    mark_finished();
}

void serve_fallback(const http::FallbackPage& page, const std::string& host, std::uint16_t port,
                    std::stop_token stop, std::function<void(std::uint16_t port)> listening) {
    detail::ListenAddress address;
    try {
        address = detail::check_listen(host, port);
    } catch (const std::runtime_error& error) {
        throw ServeError(error.what());
    }
    claim_drogon();
    const auto handler = drogon_adapter::mount(
        drogon_adapter::Service(
            [html = page.html()](http::Request request, http::HttpBinding::Respond respond) {
                respond(http::fallback_answer(html, request));
            }),
        drogon::app());
    drogon_adapter::answer_all(handler, drogon::app());
    const bool stopped = run_drogon(
        address, stop,
        [&](std::uint16_t bound) {
            // Under systemd, the fallback page is the service, started.
            systemd::notify_ready();
            if (listening) {
                listening(bound);
            }
        },
        [] { systemd::notify("STOPPING=1"); });
    mark_finished();
    if (!stopped) {
        throw ServeError("Drogon stopped");
    }
}

}  // namespace teta::wot
