// Every route of the HTTP binding, over a real socket.
//
//     example-http-walkthrough            # serve on a free port and walk through it
//     example-http-walkthrough --serve    # serve on 127.0.0.1:5000 for walkthrough.sh/.ps1
//
// The walkthrough sends the same requests as the curl scripts, with a
// deliberately minimal HTTP/1.1 client, and prints each exchange as `curl -i`
// would. The C++ port of teta-wot's `http-walkthrough`; this one also checks
// each status, and exits with a non-zero status if one is wrong.

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

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <iostream>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <teta/wot.hpp>
#include <thread>
#include <utility>
#include <vector>

namespace tw = teta::wot;
using tw::json::Value;
using namespace std::chrono_literals;

struct FadeInput {
    std::uint8_t to = 0;
    double step_seconds = 0.1;

    static void describe(tw::StructInfo<FadeInput>& s) {
        s.deny_unknown_fields();
        s.field("to").description("Target brightness.");
        s.field("step_seconds").description("Seconds per step.").defaulted();
    }
};

/// A dimmable lamp.
class Lamp {
  public:
    tw::Prop<std::uint8_t> brightness =
        tw::Prop<std::uint8_t>(50).with_constraints(tw::td::Constraints{}.le(100));
    tw::Prop<bool> is_on{false};

    bool toggle() {
        is_on.update([](bool& on) { on = !on; });
        return is_on.get();
    }

    std::uint8_t fade(tw::ActionCtx& ctx, FadeInput input) {
        while (brightness.get() != input.to) {
            ctx.sleep(std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::duration<double>(input.step_seconds)));
            brightness.update([&](std::uint8_t& b) { b = b < input.to ? b + 1 : b - 1; });
        }
        return brightness.get();
    }

    static void define(tw::ThingSpec<Lamp>& t) {
        t.title("Lamp").description("A dimmable lamp.");
        t.property("brightness", &Lamp::brightness).doc("Brightness, 0 to 100.").unit("percent");
        t.property("is_on", &Lamp::is_on).read_only().doc("Whether the lamp is on.");
        t.action("toggle", &Lamp::toggle).doc("Switch the lamp on or off.\n\nReturns whether it is now on.");
        t.action("fade", &Lamp::fade, tw::whole_input).doc("Fade to a brightness, one step at a time.");
    }
};

tw::ThingServer server() {
    return tw::ThingServer::builder().thing<Lamp>("lamp").build();
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

// Sends `request` and reads the answer until the server closes the
// connection (the request says `Connection: close`).
std::string round_trip(std::uint16_t port, const std::string& request) {
    const Socket s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    if (connect(s, reinterpret_cast<const sockaddr*>(&address), sizeof address) != 0) {
        close_socket(s);
        throw std::runtime_error("can't connect");
    }
    for (std::size_t sent = 0; sent < request.size();) {
        const auto n = send(s, request.data() + sent, static_cast<int>(request.size() - sent), 0);
        if (n <= 0) {
            close_socket(s);
            throw std::runtime_error("can't send");
        }
        sent += static_cast<std::size_t>(n);
    }
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
    return response;
}

struct Walkthrough {
    std::uint16_t port;
    int failures = 0;

    static void section(std::string_view title) { std::cout << "\n## " << title << "\n\n"; }

    // Sends one request, prints it and the response `curl -i` style, and
    // checks its status. Returns the JSON body (`null` if there is none).
    Value http(std::string_view method, const std::string& path, std::optional<std::string> body,
               int expected, const std::vector<std::pair<std::string, std::string>>& headers = {}) {
        std::string request =
            std::format("{} {} HTTP/1.1\r\nHost: 127.0.0.1:{}\r\nConnection: close\r\n", method, path, port);
        for (const auto& [name, value] : headers) {
            request += name + ": " + value + "\r\n";
        }
        if (body) {
            request += std::format("Content-Type: application/json\r\nContent-Length: {}\r\n", body->size());
        }
        request += "\r\n" + body.value_or("");
        std::cout << "$ " << method << " " << path << (body ? "  " + *body : "") << "\n";

        const std::string response = round_trip(port, request);
        const auto split = response.find("\r\n\r\n");
        const std::string head = response.substr(0, split);
        const std::string content = split == std::string::npos ? "" : response.substr(split + 4);
        int status = 0;
        for (std::size_t start = 0; start < head.size();) {
            const auto end = std::min(head.find("\r\n", start), head.size());
            const std::string line = head.substr(start, end - start);
            if (start == 0 && line.size() > 12) {
                status = std::stoi(line.substr(9, 3));
            }
            if (!line.starts_with("date:") && !line.starts_with("content-length:")) {
                std::cout << "< " << line << "\n";
            }
            start = end + 2;
        }
        if (!content.empty()) {
            std::cout << content.substr(0, 300) << (content.size() > 300 ? " …" : "") << "\n";
        }
        std::cout << "\n";
        if (status != expected) {
            std::cerr << "expected " << expected << ", got " << status << "\n";
            ++failures;
        }
        try {
            return tw::json::parse(content);
        } catch (const tw::json::ParseError&) {
            return nullptr;
        }
    }

    // The same requests as walkthrough.sh and walkthrough.ps1.
    void run() {
        const std::pair<std::string, std::string> origin{"Origin", "http://example.com"};
        section("Discovery: the Thing list and the Thing Description");
        http("GET", "/things/", {}, 200);
        http("GET", "/lamp/", {}, 200);

        section("Properties: read, write (with lax coercion), invalid values, reset");
        http("GET", "/lamp/brightness", {}, 200);
        http("PUT", "/lamp/brightness", "\"80\"", 201);
        http("PUT", "/lamp/brightness", "150", 422);
        http("PUT", "/lamp/is_on", "true", 405);
        http("POST", "/lamp/brightness/reset", {}, 200);

        section("Actions: invoke, poll, output, list");
        auto invocation = http("POST", "/lamp/toggle", {}, 201);
        std::string id = invocation.value("id", std::string());
        std::this_thread::sleep_for(50ms);
        http("GET", "/action_invocations/" + id, {}, 200);
        http("GET", "/action_invocations/" + id + "/output", {}, 200);
        http("GET", "/lamp/toggle", {}, 200);
        http("POST", "/lamp/fade", R"({"to": "high"})", 422);

        section("Cancelling a long invocation");
        const auto fade = http("POST", "/lamp/fade", R"({"to": 100})", 201);
        id = fade.value("id", std::string());
        std::this_thread::sleep_for(250ms);
        http("DELETE", "/action_invocations/" + id, {}, 200);
        std::this_thread::sleep_for(50ms);
        http("GET", "/action_invocations/" + id, {}, 200);
        http("DELETE", "/action_invocations/" + id, {}, 503);

        section("Errors and routing: 404, 405, 307, 422");
        http("GET", "/action_invocations/00000000-0000-0000-0000-000000000000", {}, 404);
        http("GET", "/action_invocations/not-a-uuid", {}, 422);
        http("DELETE", "/lamp/brightness", {}, 405);
        http("GET", "/lamp", {}, 307);

        section("CORS: a preflight, and a request with an Origin");
        http("OPTIONS", "/lamp/brightness", {}, 200, {origin, {"Access-Control-Request-Method", "PUT"}});
        http("GET", "/lamp/is_on", {}, 200, {origin});
    }
};

int main(int argc, char** argv) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    try {
        if (argc > 1 && std::string_view(argv[1]) == "--serve") {
            tw::log::init(false);
            server().serve("127.0.0.1", 5000, tw::shutdown_signal(), [](std::uint16_t port) {
                std::cout << "listening on http://127.0.0.1:" << port << std::endl;
            });
            return EXIT_SUCCESS;
        }
        // Serve on a free port; walk through it from another thread, then stop.
        std::stop_source stop;
        std::jthread walker;
        int failures = 0;
        server().serve("127.0.0.1", 0, stop.get_token(), [&](std::uint16_t port) {
            walker = std::jthread([&, port] {
                Walkthrough walkthrough{port};
                try {
                    walkthrough.run();
                } catch (const std::exception& error) {
                    std::cerr << "error: " << error.what() << "\n";
                    ++walkthrough.failures;
                }
                failures = walkthrough.failures;
                stop.request_stop();
            });
        });
        walker.join();
        return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
