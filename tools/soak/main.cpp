// The soak test: drives the simulated microscope with every kind of client
// for as long as it is given, and watches its memory. A port of teta-wot's
// `tools/soak`.
//
//     soak --server build/<preset>/examples/simulated-microscope/example-simulated-microscope --duration 24h
//     --report soak.csv
//
// The server runs as its own process, so that its memory is measured alone.
// The clients, all at once, on real sockets, one thread each:
//
// - property reads and writes, including the camera's rendered sharpness;
// - stage moves, waited for, and long moves cancelled;
// - autofocus runs;
// - image captures, whose Blobs are downloaded;
// - two MJPEG viewers, two server-sent events streams and a WebSocket, which
//   must never end.
//
// Every minute it prints (and with `--report` appends to a CSV file) the
// requests, errors, latency, frames, events, WebSocket messages, the
// server's working set, private bytes and handle count, and the invocations
// it keeps. It exits with a failure if any request failed, a stream ended,
// or the private bytes grew after the warm-up: the median of the last
// quarter of the run against the first quarter's, beyond 25 % plus 16 MB.
//
// Options: `--duration` (such as `90s`, `10m`, `24h`; 5 minutes by default),
// `--profile tetathing|wot`, `--warmup` (by default a fifth of the run, at
// most 10 minutes), `--report FILE`, and `--stop-under-load`: the
// shutdown-under-load test, which at the end stops the server as Ctrl-C does
// while every client is still at work, and fails unless it exits with 0
// within its grace period and 10 seconds, its long move cancelled.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "net.hpp"
#include "process.hpp"
#include "teta/wot/json.hpp"
#include "teta/wot/uuid.hpp"

namespace json = teta::wot::json;
using json::Value;
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

namespace {

// ---- Options -----------------------------------------------------------------

struct Options {
    std::filesystem::path server = "example-simulated-microscope";
    std::chrono::seconds duration = 300s;
    std::optional<std::chrono::seconds> warmup;
    std::string profile = "tetathing";
    std::optional<std::filesystem::path> report;
    bool stop_under_load = false;
};

std::chrono::seconds parse_duration(const std::string& text) {
    const auto digits = text.find_first_not_of("0123456789");
    const auto number = text.substr(0, digits);
    const auto unit = digits == std::string::npos ? std::string() : text.substr(digits);
    if (number.empty()) {
        throw std::runtime_error("a duration, not `" + text + "`");
    }
    const auto n = std::stoull(number);
    if (unit.empty() || unit == "s") {
        return std::chrono::seconds(n);
    }
    if (unit == "m") {
        return std::chrono::seconds(n * 60);
    }
    if (unit == "h") {
        return std::chrono::seconds(n * 3600);
    }
    throw std::runtime_error("unknown unit `" + unit + "` in `" + text + "`: use s, m or h");
}

Options options(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto value = [&]() -> std::string {
            if (i + 1 >= argc) {
                throw std::runtime_error(arg + " needs a value");
            }
            return argv[++i];
        };
        if (arg == "--server") {
            options.server = value();
        } else if (arg == "--duration") {
            options.duration = parse_duration(value());
        } else if (arg == "--warmup") {
            options.warmup = parse_duration(value());
        } else if (arg == "--profile") {
            options.profile = value();
        } else if (arg == "--report") {
            options.report = value();
        } else if (arg == "--stop-under-load") {
            options.stop_under_load = true;
        } else {
            throw std::runtime_error("unknown option `" + arg + "`");
        }
    }
    return options;
}

// ---- Statistics ----------------------------------------------------------------

struct Stats {
    std::atomic<std::uint64_t> requests = 0;
    std::atomic<std::uint64_t> errors = 0;
    std::atomic<std::uint64_t> frames = 0;
    std::atomic<std::uint64_t> events = 0;
    std::atomic<std::uint64_t> messages = 0;
    std::atomic<std::uint64_t> stream_ends = 0;
    std::mutex mutex;
    std::vector<double> latencies_ms;
    std::size_t shown_errors = 0;
    /// Set when the server is being stopped under load: what fails after
    /// that is expected.
    std::atomic<bool> stopping = false;

    void error(const std::string& what) {
        if (stopping) {
            return;
        }
        ++errors;
        std::lock_guard lock(mutex);
        if (shown_errors < 20) {
            ++shown_errors;
            std::cerr << "error: " << what << std::endl;
        }
    }

    /// Times a request, counting it and any failure: its body if it
    /// succeeded (a 2xx).
    std::optional<std::string> time(const std::string& what, const std::function<soak::Answer()>& request) {
        const auto started = Clock::now();
        std::optional<std::string> body;
        try {
            auto answer = request();
            if (answer.status >= 200 && answer.status < 300) {
                body = std::move(answer.body);
            } else {
                error(what + ": status " + std::to_string(answer.status));
            }
        } catch (const std::exception& failure) {
            error(what + ": " + failure.what());
        }
        const std::chrono::duration<double, std::milli> took = Clock::now() - started;
        ++requests;
        std::lock_guard lock(mutex);
        latencies_ms.push_back(took.count());
        return body;
    }
};

/// Whether the clients should go on, and a sleep that ends when they shouldn't.
class Running {
  public:
    explicit operator bool() const { return running_; }
    void stop() {
        {
            std::lock_guard lock(mutex_);
            running_ = false;
        }
        wake_.notify_all();
    }
    void pause(std::chrono::milliseconds duration) {
        std::unique_lock lock(mutex_);
        wake_.wait_for(lock, duration, [&] { return !running_.load(); });
    }

  private:
    std::atomic<bool> running_ = true;
    std::mutex mutex_;
    std::condition_variable wake_;
};

Value parse_or_null(const std::optional<std::string>& body) {
    if (!body) {
        return nullptr;
    }
    try {
        return json::parse(*body);
    } catch (const json::ParseError&) {
        return nullptr;
    }
}

/// The path of an absolute URL on the server.
std::string local(const std::string& href) {
    const auto scheme = href.find("://");
    if (scheme == std::string::npos) {
        return href;
    }
    const auto path = href.find('/', scheme + 3);
    return path == std::string::npos ? href : href.substr(path);
}

// ---- The clients -------------------------------------------------------------

/// Invokes an action and waits for it to finish: its final status.
std::optional<Value> invoke(soak::Http& http, Stats& stats, Running& running, const std::string& path,
                            const Value& input) {
    const auto body = input.is_null() ? std::string() : json::dump(input);
    const auto started = parse_or_null(stats.time(path, [&] { return http.request("POST", path, body); }));
    if (!started.is_object() || !started["href"].is_string()) {
        return std::nullopt;
    }
    const auto status_path = local(started["href"].get<std::string>());
    for (;;) {
        const auto status =
            parse_or_null(stats.time(status_path, [&] { return http.request("GET", status_path); }));
        if (!status.is_object()) {
            return std::nullopt;
        }
        if (status["status"] != "pending" && status["status"] != "running") {
            return status;
        }
        running.pause(25ms);
    }
}

void properties(std::uint16_t port, Stats& stats, Running& running) {
    soak::Http http(port);
    for (std::uint64_t n = 0; running; ++n) {
        stats.time("GET /stage/position", [&] { return http.request("GET", "/stage/position"); });
        stats.time("GET /autofocus/last_focus", [&] { return http.request("GET", "/autofocus/last_focus"); });
        const auto fps = json::dump(Value(10.0 + static_cast<double>(n % 3)));
        stats.time("PUT /camera/fps", [&] { return http.request("PUT", "/camera/fps", fps); });
        if (n % 10 == 0) {
            stats.time("GET /camera/sharpness", [&] { return http.request("GET", "/camera/sharpness"); });
        }
        running.pause(20ms);
    }
}

void moves(std::uint16_t port, Stats& stats, Running& running, const std::string& profile) {
    soak::Http http(port);
    for (std::int64_t n = 1; running; ++n) {
        if (n % 10 == 0) {
            // A long move, cancelled.
            const auto started = parse_or_null(stats.time("POST /stage/move_to", [&] {
                return http.request("POST", "/stage/move_to", R"({"x": 100000, "y": 0, "z": 0})");
            }));
            if (!started.is_object() || !started["href"].is_string()) {
                continue;
            }
            const auto path = local(started["href"].get<std::string>());
            running.pause(100ms);
            stats.time("DELETE invocation", [&] { return http.request("DELETE", path); });
            const std::string expected = profile == "wot" ? "failed" : "cancelled";
            for (;;) {
                const auto status =
                    parse_or_null(stats.time("GET invocation", [&] { return http.request("GET", path); }));
                if (!status.is_object()) {
                    break;
                }
                if (status["status"] == "pending" || status["status"] == "running") {
                    running.pause(25ms);
                    continue;
                }
                if (status["status"] != expected) {
                    stats.error("a cancelled move ended " + json::dump(status["status"]));
                }
                break;
            }
            invoke(http, stats, running, "/stage/home", nullptr);
        } else {
            const auto status =
                invoke(http, stats, running, "/stage/move_by", Value{{"dz", n % 2 == 0 ? 20 : -20}});
            if (status && (*status)["status"] != "completed") {
                stats.error("a move ended " + json::dump((*status)["status"]));
            }
        }
    }
}

void autofocus(std::uint16_t port, Stats& stats, Running& running) {
    soak::Http http(port);
    while (running) {
        const auto status =
            invoke(http, stats, running, "/autofocus/run", Value{{"range", 200}, {"steps", 5}});
        if (status && (*status)["status"] != "completed") {
            stats.error("an autofocus ended " + json::dump((*status)["status"]));
        }
        running.pause(20s);
    }
}

void captures(std::uint16_t port, Stats& stats, Running& running) {
    soak::Http http(port);
    while (running) {
        if (const auto status = invoke(http, stats, running, "/camera/capture", nullptr)) {
            const auto& output = (*status)["output"];
            if (output.is_object() && output["href"].is_string()) {
                const auto path = local(output["href"].get<std::string>());
                const auto jpeg = stats.time("GET a Blob", [&] { return http.request("GET", path); });
                if (jpeg && !jpeg->starts_with("\xff\xd8")) {
                    stats.error("a Blob that isn't a JPEG");
                }
            } else {
                stats.error("a capture ended " + json::dump((*status)["status"]));
            }
        }
        running.pause(2s);
    }
}

/// A long answer (MJPEG or SSE), counting `marker`s into `counter`, and
/// reopened if the server ends it.
void stream(std::uint16_t port, const std::string& path, const std::string& accept, const std::string& marker,
            Stats& stats, Running& running, std::atomic<std::uint64_t>& counter) {
    while (running) {
        soak::Connection connection;
        try {
            connection = soak::Connection(port);
            connection.send_all("GET " + path + " HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) +
                                "\r\nAccept: " + accept + "\r\n\r\n");
        } catch (const std::exception&) {
            stats.error("GET " + path + ": can't connect");
            running.pause(1s);
            continue;
        }
        while (running) {
            const auto read = connection.fill(1000ms);
            if (!read) {
                break;
            }
            auto& buffer = connection.buffer();
            for (auto at = buffer.find(marker); at != std::string::npos;
                 at = buffer.find(marker, at + marker.size())) {
                ++counter;
            }
            // Keep a tail that may hold the start of a marker.
            buffer.erase(0, buffer.size() > marker.size() - 1 ? buffer.size() - (marker.size() - 1) : 0);
        }
        if (running && !stats.stopping) {
            ++stats.stream_ends;
            stats.error("GET " + path + " ended");
        }
    }
}

void websocket(std::uint16_t port, Stats& stats, Running& running) {
    while (running) {
        std::optional<soak::WebSocket> socket;
        try {
            socket.emplace(port, "/stage/ws");
            socket->send_text(R"({"messageType": "addPropertyObservation", "data": {"position": true}})");
        } catch (const std::exception&) {
            stats.error("the WebSocket can't open");
            running.pause(1s);
            continue;
        }
        while (running) {
            const auto received = socket->receive(1000ms);
            if (!received) {
                break;
            }
            stats.messages += *received;
        }
        if (running && !stats.stopping) {
            ++stats.stream_ends;
            stats.error("the WebSocket closed");
        }
    }
}

// ---- The run -------------------------------------------------------------------

double percentile(std::vector<double>& sorted, double p) {
    if (sorted.empty()) {
        return 0.0;
    }
    return sorted[static_cast<std::size_t>(std::round(static_cast<double>(sorted.size() - 1) * p))];
}

double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values.empty() ? 0.0 : values[values.size() / 2];
}

std::string text_of(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

bool run(const Options& options) {
    const auto folder =
        std::filesystem::temp_directory_path() / ("teta-wot-soak-" + teta::wot::Uuid::v4().to_string());
    std::filesystem::create_directories(folder);
    const Value config = {{"things",
                           {{"stage", "microscope.stage:Stage"},
                            {"camera", "microscope.camera:Camera"},
                            {"autofocus", "microscope.autofocus:Autofocus"}}},
                          {"settings_folder", text_of(folder / "settings")},
                          {"wire_profile", options.profile}};
    const auto config_file = folder / "microscope.json";
    std::ofstream(config_file, std::ios::binary) << json::dump(config);
    const auto port = soak::free_port();
    const auto log = folder / "server.log";
    soak::Process server(std::filesystem::absolute(options.server),
                         {"-c", text_of(config_file), "--port", std::to_string(port)}, log);
    const auto server_log = [&] {
        std::ifstream file(log, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), {});
    };
    soak::Http http(port);
    const auto deadline = Clock::now() + 60s;
    for (;;) {
        try {
            http.request("GET", "/stage/");
            break;
        } catch (const std::exception&) {
            if (Clock::now() > deadline || server.exited()) {
                std::cerr << "the server didn't answer within a minute:\n" << server_log();
                return false;
            }
            std::this_thread::sleep_for(200ms);
        }
    }
    const auto warmup = options.warmup.value_or(std::min<std::chrono::seconds>(options.duration / 5, 600s));
    std::cout << "soaking " << text_of(options.server) << " (the " << options.profile << " profile) for "
              << options.duration.count() << "s, warm-up " << warmup.count() << "s, pid " << server.pid()
              << "\n";

    Stats stats;
    Running running;
    std::vector<std::jthread> clients;
    clients.emplace_back([&] { properties(port, stats, running); });
    clients.emplace_back([&] { properties(port, stats, running); });
    clients.emplace_back([&] { moves(port, stats, running, options.profile); });
    clients.emplace_back([&] { autofocus(port, stats, running); });
    clients.emplace_back([&] { captures(port, stats, running); });
    for (int i = 0; i < 2; ++i) {
        clients.emplace_back(
            [&] { stream(port, "/camera/preview", "*/*", "--frame", stats, running, stats.frames); });
    }
    clients.emplace_back(
        [&] { stream(port, "/stage/arrived", "text/event-stream", "data:", stats, running, stats.events); });
    clients.emplace_back([&] {
        stream(port, "/camera/captured", "text/event-stream", "data:", stats, running, stats.events);
    });
    clients.emplace_back([&] { websocket(port, stats, running); });

    std::optional<std::ofstream> report;
    if (options.report) {
        report.emplace(*options.report, std::ios::binary);
        *report << "minute,requests,errors,p50_ms,p99_ms,frames,events,ws_messages,working_set_mb,private_mb,"
                   "handles,"
                   "invocations\n";
    }
    std::cout << "minute  requests errors   p50 ms   p99 ms   frames  events  ws msgs  working MB  private "
                 "MB  handles "
                 " invocations\n";
    const auto started = Clock::now();
    std::vector<double> samples;
    for (int minute = 1; Clock::now() - started < options.duration; ++minute) {
        const auto remaining =
            options.duration - std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - started);
        std::this_thread::sleep_for(std::min<std::chrono::seconds>(remaining, 60s));
        std::vector<double> latencies;
        {
            std::lock_guard lock(stats.mutex);
            latencies.swap(stats.latencies_ms);
        }
        std::sort(latencies.begin(), latencies.end());
        const auto requests = stats.requests.exchange(0);
        const auto errors = stats.errors.load();
        const auto frames = stats.frames.exchange(0);
        const auto events = stats.events.exchange(0);
        const auto messages = stats.messages.exchange(0);
        const auto memory = server.memory().value_or(soak::Memory{});
        const auto working = static_cast<double>(memory.working_set) / 1048576.0;
        const auto private_mb = static_cast<double>(memory.private_bytes) / 1048576.0;
        std::size_t invocations = 0;
        try {
            const auto list = json::parse(http.request("GET", "/action_invocations").body);
            invocations = list.is_array() ? list.size() : 0;
        } catch (const std::exception&) {
        }
        if (Clock::now() - started >= warmup && memory.private_bytes > 0) {
            samples.push_back(private_mb);
        }
        const auto p50 = percentile(latencies, 0.5);
        const auto p99 = percentile(latencies, 0.99);
        std::cout
            << std::format(
                   "{:>6} {:>9} {:>6} {:>8.1f} {:>8.1f} {:>8} {:>7} {:>8} {:>11.1f} {:>11.1f} {:>8} {:>12}\n",
                   minute, requests, errors, p50, p99, frames, events, messages, working, private_mb,
                   memory.handles, invocations)
            << std::flush;
        if (report) {
            *report << std::format("{},{},{},{:.2f},{:.2f},{},{},{},{:.2f},{:.2f},{},{}\n", minute, requests,
                                   errors, p50, p99, frames, events, messages, working, private_mb,
                                   memory.handles, invocations)
                    << std::flush;
        }
        if (server.exited()) {
            stats.error("the server exited");
            break;
        }
    }

    bool passed = true;
    if (options.stop_under_load) {
        // The shutdown-under-load test: a long move in progress, every client
        // at work, and the server asked to stop as Ctrl-C does.
        const auto move = http.request("POST", "/stage/move_to", R"({"x": 100000, "y": 0, "z": 0})");
        std::this_thread::sleep_for(200ms);
        stats.stopping = true;
        const auto asked = Clock::now();
        server.interrupt();
        const auto code = server.wait(15s);
        const std::chrono::duration<double> took = Clock::now() - asked;
        const auto text = server_log();
        const bool cancelled = text.find("was cancelled") != std::string::npos;
        const bool clean = code == 0 && text.find("shutting down") != std::string::npos &&
                           text.find("stopped") != std::string::npos;
        std::cout << std::format(
            "\nstopped under load in {:.1f} s, with exit code {}; the long move ({}) {}\n", took.count(),
            code ? std::to_string(*code) : "none: still running", move.status,
            cancelled ? "cancelled" : "NOT cancelled");
        if (!clean || !cancelled) {
            std::cerr << "the server's log:\n" << text;
        }
        passed = clean && cancelled;
    }
    running.stop();
    clients.clear();  // joins them
    server.kill();
    (void)server.wait(10s);
    std::error_code ignored;
    std::filesystem::remove_all(folder, ignored);

    // The verdict.
    const auto errors = stats.errors.load();
    const auto stream_ends = stats.stream_ends.load();
    passed = passed && errors == 0 && stream_ends == 0;
    std::cout << "\n" << errors << " errors, " << stream_ends << " streams ended early\n";
    if (samples.size() >= 4) {
        const auto quarter = std::max<std::size_t>(samples.size() / 4, 1);
        const auto first = median({samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(quarter)});
        const auto last = median({samples.end() - static_cast<std::ptrdiff_t>(quarter), samples.end()});
        const auto limit = first * 1.25 + 16.0;
        const bool grew = last > limit;
        std::cout << std::format(
            "private bytes after the warm-up: {:.1f} MB at first, {:.1f} MB at the end (limit {:.1f} MB): "
            "{}\n",
            first, last, limit, grew ? "GREW" : "steady");
        passed = passed && !grew;
    } else {
        std::cout << "too short after the warm-up to judge memory (" << samples.size() << " samples)\n";
    }
    std::cout << (passed ? "PASSED" : "FAILED") << "\n";
    return passed;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(options(argc, argv)) ? EXIT_SUCCESS : EXIT_FAILURE;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
