// An application binary: it registers its Thing types, and serves them from
// configuration files.
//
//     example-config-and-cli                                          # a demonstration, then exit
//     example-config-and-cli -c examples/config-and-cli/config.json   # serve until Ctrl-C
//     example-config-and-cli -c examples/config-and-cli/failing.json --fallback
//
// The C++ port of teta-wot's `config-and-cli`. `tw::cli::serve_from_cli` is
// teta-wot's command line: `-c`, `-j`, `--host`, `--port`, `--fallback`,
// `--debug`, and its exit codes.

// The socket API includes <windows.h>: no `min` and `max` macros.
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <teta/wot.hpp>
#include <thread>
#include <utility>
#include <vector>

namespace tw = teta::wot;
using tw::json::Value;
using namespace std::chrono_literals;

/// The counter's configuration: its `kwargs`.
struct CounterConfig {
    std::int64_t start = 0;

    static void describe(tw::StructInfo<CounterConfig>& s) {
        s.deny_unknown_fields();
        s.field("start").defaulted();
    }
};

/// A counter.
class Counter {
  public:
    using Config = CounterConfig;

    explicit Counter(CounterConfig config) : count(config.start) {}

    /// The count.
    tw::Prop<std::int64_t> count;

    std::int64_t increment() {
        count.update([](std::int64_t& c) { ++c; });
        return count.get();
    }

    static void define(tw::ThingSpec<Counter>& t) {
        t.doc("A counter.");
        t.property("count", &Counter::count).doc("The count.").read_only();
        t.action("increment", &Counter::increment).doc("Add one.");
    }
};

/// A stage whose hardware isn't connected: it can't start.
class DisconnectedStage {
  public:
    void connect() { throw std::runtime_error("no stage on COM3"); }

    static void define(tw::ThingSpec<DisconnectedStage>& t) { t.on_start(&DisconnectedStage::connect); }
};

/// The Thing types this application serves, by the names its configuration
/// files use.
tw::ThingRegistry registry() {
    tw::ThingRegistry registry;
    registry.add<Counter>("my_lab.counters:Counter").add<DisconnectedStage>("my_lab.stages:Stage");
    return registry;
}

// ---- A minimal HTTP/1.1 client ------------------------------------------------------------

#ifdef _WIN32
using Socket = SOCKET;
void close_socket(Socket s) {
    closesocket(s);
}
#else
using Socket = int;
void close_socket(Socket s) {
    close(s);
}
#endif

sockaddr_in loopback(std::uint16_t port) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    return address;
}

// A port nothing listens on.
std::uint16_t free_port() {
    const Socket s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address = loopback(0);
    bind(s, reinterpret_cast<const sockaddr*>(&address), sizeof address);
    socklen_t length = sizeof address;
    getsockname(s, reinterpret_cast<sockaddr*>(&address), &length);
    close_socket(s);
    return ntohs(address.sin_port);
}

bool answers(std::uint16_t port) {
    const Socket s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    const sockaddr_in address = loopback(port);
    const bool connected = connect(s, reinterpret_cast<const sockaddr*>(&address), sizeof address) == 0;
    close_socket(s);
    return connected;
}

// A GET: the status code and the body.
std::pair<int, std::string> get(std::uint16_t port, const std::string& path) {
    const Socket s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    const sockaddr_in address = loopback(port);
    if (connect(s, reinterpret_cast<const sockaddr*>(&address), sizeof address) != 0) {
        close_socket(s);
        throw std::runtime_error("can't connect");
    }
    const std::string request = "GET " + path + " HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) +
                                "\r\nConnection: close\r\n\r\n";
    send(s, request.data(), static_cast<int>(request.size()), 0);
    std::string response;
    char buffer[4096];
    for (;;) {
        const auto n = recv(s, buffer, sizeof buffer, 0);
        if (n <= 0) {
            break;
        }
        response.append(buffer, static_cast<std::size_t>(n));
    }
    close_socket(s);
    const auto space = response.find(' ');
    const int status = space == std::string::npos ? 0 : std::stoi(response.substr(space + 1, 3));
    const auto body = response.find("\r\n\r\n");
    return {status, body == std::string::npos ? "" : response.substr(body + 4)};
}

// ---- The demonstration --------------------------------------------------------------------

// Runs the command line in-process with a configuration, calls `visit` while
// it serves, stops it and returns its exit code.
int run_cli(const std::vector<std::string>& extra, const Value& config,
            const std::function<void(std::uint16_t)>& visit) {
    const std::uint16_t port = free_port();
    std::vector<std::string> args = {"config-and-cli", "-j", tw::json::dump(config), "--port",
                                     std::to_string(port)};
    args.insert(args.end(), extra.begin(), extra.end());
    std::stop_source stop;
    std::atomic<bool> finished = false;
    int code = -1;
    std::thread serving([&] {
        code = tw::cli::serve_until(args, registry(), {}, stop.get_token());
        finished = true;
    });
    // Wait until something answers, unless the command line has ended.
    for (int i = 0; i < 200 && !finished && !answers(port); ++i) {
        std::this_thread::sleep_for(20ms);
    }
    try {
        if (!finished) {
            visit(port);
        }
    } catch (...) {
        stop.request_stop();
        serving.join();
        throw;
    }
    stop.request_stop();
    serving.join();
    return code;
}

void demo(const std::string& folder) {
    std::cout << "1. A configuration:\n";
    const Value config{{"things",
                        {{"counter", {{"class", "my_lab.counters:Counter"}, {"kwargs", {{"start", 41}}}}},
                         {"spare", "my_lab.counters:Counter"}}},
                       {"api_prefix", "/api"},
                       {"settings_folder", folder}};
    std::cout << "   " << tw::json::dump(config) << "\n";
    int code = run_cli({}, config, [](std::uint16_t port) {
        std::cout << "   GET /api/things/ → " << get(port, "/api/things/").second << "\n";
        const auto count = get(port, "/api/counter/count").second;
        std::cout << "   GET /api/counter/count → " << count << "\n";
        if (count != "41") {
            throw std::runtime_error("the count should start at 41");
        }
    });
    std::cout << "   exit code: " << code << "\n\n";
    if (code != 0) {
        throw std::runtime_error("the server should have stopped normally");
    }

    // Drogon serves once per process, so the rest runs what doesn't serve.
    std::cout << "2. A Thing that can't start:\n";
    const Value failing{
        {"things", {{"counter", "my_lab.counters:Counter"}, {"stage", "my_lab.stages:Stage"}}},
        {"settings_folder", folder}};
    code = run_cli({}, failing, [](std::uint16_t) {});
    std::cout << "   exit code: " << code << "\n";
    if (code != 3) {
        throw std::runtime_error("a Thing that can't start should exit with 3");
    }
    // What `--fallback` would serve instead, at the server's address.
    tw::http::FallbackPage page;
    page.error_message = "Failed to enter 'stage' Thing: no stage on COM3";
    page.things = {"counter", "stage"};
    page.config = tw::json::dump_pretty(failing);
    page.traceback = "no stage on COM3";
    const auto html = page.html();
    std::cout << "   with --fallback:";
    for (const std::string path : {"/fallback", "/", "/stage/"}) {
        const auto answer = tw::http::fallback_answer(html, tw::http::Request::make("GET", path));
        std::cout << " GET " << path << " → " << answer.status
                  << (path == "/fallback" ? " " + answer.body : "") << ";";
    }
    std::cout << "\n\n";

    std::cout << "3. An unknown class is a configuration error:\n";
    code = run_cli({}, Value{{"things", {{"x", "my_lab.missing:Thing"}}}, {"settings_folder", folder}},
                   [](std::uint16_t) {});
    std::cout << "   exit code: " << code << "\n";
    if (code != 3) {
        throw std::runtime_error("an unknown class should exit with 3");
    }
}

int main(int argc, char** argv) {
    if (argc > 1) {
        // The real command line.
        return tw::cli::serve_from_cli(argc, argv, registry());
    }
    std::cout << std::unitbuf;  // each line as it is written, between the log's
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    const auto folder =
        std::filesystem::temp_directory_path() / ("config-and-cli-" + tw::Uuid::v4().to_string());
    int code = EXIT_SUCCESS;
    try {
        demo(tw::detail::path_text(folder));
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        code = EXIT_FAILURE;
    }
    std::filesystem::remove_all(folder);
    return code;
}
