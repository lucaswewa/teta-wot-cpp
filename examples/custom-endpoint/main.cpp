// Custom endpoints: HTTP routes of a Thing that aren't affordances, written
// as member functions taking the request and returning a response. They
// aren't in the Thing Description, but can be the target of its links.
//
// A data logger serves its readings as a CSV download, a plain-text summary
// that takes a query parameter, and accepts notes posted as text. The C++
// port of teta-wot's `custom-endpoint`: it prints the same.

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <teta/wot.hpp>
#include <utility>
#include <vector>

namespace tw = teta::wot;
namespace http = teta::wot::http;

// A query parameter's value (`unit` in `unit=F&x=1`), if it is there.
std::optional<std::string> query_parameter(const http::Request& request, std::string_view name) {
    const std::string text = request.query.value_or("");
    std::string_view query = text;
    while (!query.empty()) {
        const auto end = query.find('&');
        const auto pair = query.substr(0, end);
        if (const auto equals = pair.find('='); pair.substr(0, equals) == name) {
            return std::string(equals == std::string_view::npos ? "" : pair.substr(equals + 1));
        }
        query = end == std::string_view::npos ? "" : query.substr(end + 1);
    }
    return std::nullopt;
}

/// A data logger.
class Logger {
  public:
    std::size_t count() const {
        std::lock_guard lock(mutex_);
        return readings_.size();
    }

    http::Response readings_csv() const {
        std::string csv = "seconds,celsius\n";
        std::lock_guard lock(mutex_);
        for (const auto& [seconds, celsius] : readings_) {
            csv += std::format("{},{}\n", seconds, celsius);
        }
        auto response = http::Response::bytes(std::move(csv), "text/csv");
        response.headers.set("content-disposition", "attachment; filename=\"readings.csv\"");
        return response;
    }

    http::Response summary(const http::Request& request) const {
        std::lock_guard lock(mutex_);
        double total = 0;
        for (const auto& reading : readings_) {
            total += reading.second;
        }
        const double mean = total / static_cast<double>(readings_.size());
        if (query_parameter(request, "unit") == "F") {
            return http::Response::text(
                std::format("{} readings, mean {:.1f} °F", readings_.size(), mean * 1.8 + 32.0));
        }
        return http::Response::text(std::format("{} readings, mean {:.1f} °C", readings_.size(), mean));
    }

    http::Response add_note(const http::Request& request) {
        const auto author = request.headers.get("x-author").value_or("anonymous");
        std::lock_guard lock(mutex_);
        notes_.push_back(std::string(author) + ": " + request.body);
        return http::Response::empty(204);
    }

    std::vector<std::string> notes() const {
        std::lock_guard lock(mutex_);
        return notes_;
    }

    static void define(tw::ThingSpec<Logger>& t) {
        t.doc("A data logger.");
        t.property("count", &Logger::count).doc("How many readings there are.");
        t.endpoint("get", "readings.csv", &Logger::readings_csv).doc("The readings as a CSV file.");
        t.endpoint("get", "summary.txt", &Logger::summary)
            .doc("A one-line summary, in Celsius or (with `?unit=F`) Fahrenheit.");
        t.endpoint("post", "notes", &Logger::add_note).doc("Adds a note, sent as plain text.");
    }

  private:
    mutable std::mutex mutex_;
    std::vector<std::pair<std::uint32_t, double>> readings_ = {{0, 20.5}, {60, 21.0}, {120, 21.75}};
    std::vector<std::string> notes_;
};

void check(bool condition, const char* what) {
    if (!condition) {
        throw std::runtime_error(what);
    }
}

std::string status_line(int status) {
    return std::format("{} {}", status, http::reason_phrase(status));
}

// A list of strings, as Rust's `{:?}` writes it.
std::string debug(const std::vector<std::string>& items) {
    std::string out = "[";
    for (const auto& item : items) {
        out += (out.size() > 1 ? ", " : "") + tw::json::dump(item);
    }
    return out + "]";
}

void run() {
    const auto server = tw::ThingServer::builder().thing<Logger>("logger").build();
    const auto client = tw::testing::TestClient::start(server);

    const auto csv = client.get("/logger/readings.csv");
    std::cout << "GET /logger/readings.csv → " << status_line(csv.status) << "\n";
    std::cout << "  content-type: " << csv.header("content-type").value_or("-") << "\n";
    std::cout << "  content-disposition: " << csv.header("content-disposition").value_or("-") << "\n";
    std::cout << csv.text();
    check(std::ranges::count(csv.text(), '\n') == 4, "the CSV should have four lines");

    for (const char* path : {"/logger/summary.txt", "/logger/summary.txt?unit=F"}) {
        const auto summary = client.get(path);
        std::cout << "GET " << path << " → " << status_line(summary.status) << ": " << summary.text() << "\n";
    }

    const auto posted =
        client.request("POST", "/logger/notes", {{"x-author", "Ada"}}, "Calibrated the probe.");
    std::cout << "POST /logger/notes → " << status_line(posted.status) << "\n";
    check(posted.status == 204, "the note should be accepted");
    const auto logger = *client.runtime()->thing_ref<Logger>("logger");
    std::cout << "  notes: " << debug(logger.thing().notes()) << "\n";

    const auto td = client.get("/logger/").json();
    std::vector<std::string> properties;
    for (const auto& [name, property] : td["properties"].items()) {
        properties.push_back(name);
    }
    std::cout << "The TD lists the property Some(" << debug(properties) << ") and no endpoints.\n";
    const auto wrong = client.get("/logger/notes");
    std::cout << "GET /logger/notes → " << status_line(wrong.status)
              << " (allow: " << wrong.header("allow").value_or("-") << ")\n";
    check(wrong.status == 405, "GET /logger/notes should be refused");
    client.stop();
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
