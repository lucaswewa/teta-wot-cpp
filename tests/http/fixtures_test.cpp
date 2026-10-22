// The reference's HTTP fixtures (conformance/fixtures/http/), replayed
// in-process through TestClient on the C++ twins: every step's answer, after
// conformance/run.py's normalisation, must equal the reference's, parsed and
// as exact text. run.py replays the same fixtures over real sockets
// (the `conformance.http` test). `tds` and `tds-wot` need Things of later
// phases (`blob_probe`, `ndarray_probe`); their TDs are compared in
// tests/td/twin_tds_test.cpp.

#include <gtest/gtest.h>

#include <chrono>
#include <fstream>
#include <map>
#include <regex>
#include <string>
#include <thread>
#include <vector>

#include "../temp_folder.hpp"
#include "teta/wot.hpp"
#include "twins.hpp"

namespace {

namespace tw = teta::wot;
using tw::json::Value;

const char* const nil = "00000000-0000-0000-0000-000000000000";
const std::vector<std::string> kept_headers = {"content-type",
                                               "location",
                                               "allow",
                                               "access-control-allow-origin",
                                               "access-control-allow-credentials",
                                               "access-control-allow-methods",
                                               "access-control-allow-headers",
                                               "access-control-max-age",
                                               "access-control-expose-headers",
                                               "vary",
                                               "cache-control",
                                               "www-authenticate",
                                               "link",
                                               "etag"};
const std::vector<std::string> run_state = {"status", "timeStarted", "timeCompleted", "timeEnded",
                                            "output", "log",         "error"};

const std::string naive = R"(\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d(?:\.\d{1,6})?)";
const std::string utc = naive + R"((?:Z|\+00:00))";

// run.py's `Normaliser`: masks what differs from run to run, consistently
// within a scenario (UUIDs numbered by first appearance).
class Normaliser {
  public:
    std::string uuids(const std::string& text) {
        static const std::regex uuid("[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}");
        std::string out;
        std::size_t last = 0;
        for (auto it = std::sregex_iterator(text.begin(), text.end(), uuid); it != std::sregex_iterator();
             ++it) {
            out += text.substr(last, static_cast<std::size_t>(it->position()) - last);
            const std::string found = it->str();
            if (found == nil) {
                out += "<nil-uuid>";
            } else {
                if (!numbers_.contains(found)) {
                    numbers_.emplace(found, numbers_.size());
                }
                out += "<uuid:" + std::to_string(numbers_.at(found)) + ">";
            }
            last = static_cast<std::size_t>(it->position() + it->length());
        }
        return out + text.substr(last);
    }

    std::string string(const std::string& text) {
        static const std::regex utc_time(utc);
        static const std::regex naive_time(naive);
        if (std::regex_match(text, utc_time)) {
            return "<utc-datetime>";
        }
        if (std::regex_match(text, naive_time)) {
            return "<naive-datetime>";
        }
        return uuids(text);
    }

    std::string text(std::string raw) {
        static const std::regex utc_string("\"" + utc + "\"");
        static const std::regex naive_string("\"" + naive + "\"");
        static const std::regex lineno(R"("lineno":\d+)");
        static const std::regex filename(R"("filename":"(?:[^"\\]|\\.)*")");
        static const std::regex traceback(R"("traceback":"(?:[^"\\]|\\.)*")");
        raw = std::regex_replace(raw, utc_string, "\"<utc-datetime>\"");
        raw = std::regex_replace(raw, naive_string, "\"<naive-datetime>\"");
        raw = std::regex_replace(raw, lineno, "\"lineno\":\"<lineno>\"");
        raw = std::regex_replace(raw, filename, "\"filename\":\"<filename>\"");
        raw = std::regex_replace(raw, traceback, "\"traceback\":\"<traceback>\"");
        return uuids(raw);
    }

    Value value(const Value& value) {
        if (value.is_string()) {
            return string(value.get<std::string>());
        }
        if (value.is_array()) {
            Value out = Value::array();
            for (const auto& item : value) {
                out.push_back(this->value(item));
            }
            return out;
        }
        if (value.is_object()) {
            Value out = Value::object();
            for (const auto& [key, item] : value.items()) {
                out[key] = this->value(item);
            }
            if (out.contains("levelname") && out.contains("levelno") && out.contains("lineno")) {
                out["lineno"] = "<lineno>";
                out["filename"] = "<filename>";
                if (out.contains("traceback") && !out["traceback"].is_null()) {
                    out["traceback"] = "<traceback>";
                }
            }
            return out;
        }
        return value;
    }

  private:
    std::map<std::string, std::size_t> numbers_;
};

std::string hex(const std::string& bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    for (const char c : bytes) {
        const auto b = static_cast<unsigned char>(c);
        out += digits[b >> 4];
        out += digits[b & 15];
    }
    return out;
}

// run.py's `record`.
Value record(const tw::testing::TestResponse& response, Normaliser& normaliser, bool race = false) {
    Value headers = Value::object();
    for (const auto& name : kept_headers) {
        if (auto value = response.header(name)) {
            if (name == "allow") {
                std::vector<std::string> methods;
                for (std::size_t start = 0;;) {
                    const auto end = value->find(", ", start);
                    methods.push_back(value->substr(start, end - start));
                    if (end == std::string::npos) {
                        break;
                    }
                    start = end + 2;
                }
                std::ranges::sort(methods);
                std::string sorted;
                for (const auto& method : methods) {
                    sorted += (sorted.empty() ? "" : ", ") + method;
                }
                value = sorted;
            }
            headers[name] = *value;
        }
    }
    Value entry = {{"status", response.status}, {"headers", normaliser.value(headers)}};
    const std::string content_type = response.header("content-type").value_or("");
    if (response.body.empty()) {
        entry["body"] = nullptr;
    } else if (content_type.find("json") != std::string::npos) {
        Value body = normaliser.value(Value::parse(response.body));
        if (race && body.is_object() && body.contains("status")) {
            for (const auto& key : run_state) {
                if (body.contains(key)) {
                    body[key] = "<race>";
                }
            }
            entry["body"] = body;
        } else {
            entry["body"] = body;
            entry["text"] = normaliser.text(response.body);
        }
    } else if (content_type.starts_with("text/")) {
        entry["body"] = normaliser.text(response.body);
    } else {
        entry["body"] = {{"hex", hex(response.body)}};
    }
    return entry;
}

std::string replace_all(std::string text, const std::string& from, const std::string& to) {
    for (auto at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) {
        text.replace(at, from.size(), to);
    }
    return text;
}

// run.py's `run`, through TestClient, with a temporary settings folder.
Value run(const Value& scenario) {
    const testing_support::TemporaryFolder folder;
    Value config = scenario["config"];
    config["settings_folder"] = folder.text();
    const auto server =
        tw::ThingServer::from_config(tw::ServerConfig::from_value(config), twins::registry()).build();
    const auto base = tw::testing::TestClient::start(server);
    const std::string prefix = scenario["config"].value("api_prefix", "");
    Normaliser normaliser;
    std::vector<std::string> ids;
    auto resolve = [&](const Value& value, auto&& self) -> Value {
        if (value.is_array()) {
            Value out = Value::array();
            for (const auto& item : value) {
                out.push_back(self(item, self));
            }
            return out;
        }
        if (value.is_object()) {
            Value out = Value::object();
            for (const auto& [key, item] : value.items()) {
                out[key] = self(item, self);
            }
            return out;
        }
        if (!value.is_string()) {
            return value;
        }
        std::string text = replace_all(value.get<std::string>(), "{last}", ids.empty() ? nil : ids.back());
        static const std::regex inv(R"(\{inv:(\d+)\})");
        std::smatch match;
        while (std::regex_search(text, match, inv)) {
            text = match.prefix().str() + ids.at(std::stoul(match[1].str())) + match.suffix().str();
        }
        return text;
    };

    Value steps = Value::array();
    for (const auto& step : scenario["steps"]) {
        auto client = base;
        std::vector<std::pair<std::string, std::string>> headers;
        const auto step_headers = step.value("headers", Value::object());
        for (const auto& [name, value] : step_headers.items()) {
            if (tw::http::equals_ignoring_case(name, "host")) {
                client = client.with_host(value.get<std::string>());
            } else {
                headers.emplace_back(name, value.get<std::string>());
            }
        }
        std::optional<std::string> body;
        if (step.contains("json")) {
            body = tw::json::dump(resolve(step["json"], resolve));
            headers.emplace_back("Content-Type", "application/json");
        } else if (step.contains("body")) {
            body = step["body"].get<std::string>();
        } else if (step.contains("body_repeat")) {
            const auto text = step["body_repeat"][0].get<std::string>();
            const auto count = step["body_repeat"][1].get<std::size_t>();
            std::string repeated;
            repeated.reserve(text.size() * count);
            for (std::size_t i = 0; i < count; ++i) {
                repeated.append(text);
            }
            body = std::move(repeated);
        }
        const auto method = step["method"].get<std::string>();
        const auto path = resolve(step["path"], resolve).get<std::string>();
        auto response = client.request(method, path, headers, body);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        auto polling = [&] {
            const auto status = response.json().value("status", Value(nullptr));
            if (step.contains("until")) {
                return std::ranges::find(step["until"], status) == step["until"].end();
            }
            return status == "pending" || status == "running";
        };
        while (step.value("poll", false) && polling()) {
            EXPECT_LT(std::chrono::steady_clock::now(), deadline) << step["name"];
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            response = client.request(method, path, headers, body);
        }
        std::optional<std::string> created;
        if (response.status == 201 &&
            response.header("content-type").value_or("").find("json") != std::string::npos) {
            const auto parsed = response.json();
            if (parsed.is_object() && parsed.contains("id")) {
                created = parsed["id"].get<std::string>();
            } else if (parsed.is_object() && parsed.contains("status")) {
                static const std::regex uuid("[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}");
                const std::string location = response.header("location").value_or("");
                if (std::smatch match; std::regex_search(location, match, uuid)) {
                    created = match.str();
                }
            }
            if (created) {
                ids.push_back(*created);
            }
        }
        Value entry = {{"step", step}, {"response", record(response, normaliser, created.has_value())}};
        if (step.value("wait", false) && created) {
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(15);
            for (;;) {
                auto final = base.get(prefix + "/action_invocations/" + *created);
                const auto status = final.json()["status"];
                if (status != "pending" && status != "running") {
                    entry["final"] = record(final, normaliser);
                    break;
                }
                if (std::chrono::steady_clock::now() > until) {
                    ADD_FAILURE() << "invocation " << *created << " didn't finish";
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        }
        steps.push_back(std::move(entry));
    }
    base.stop();
    return steps;
}

class Fixtures : public testing::TestWithParam<const char*> {};

TEST_P(Fixtures, ReplayThroughTestClientWithNoDeltas) {
    const std::string name = GetParam();
    std::ifstream fixture_file(std::string(TETA_WOT_FIXTURES) + "/http/" + name + ".json", std::ios::binary);
    std::ifstream scenario_file(std::string(TETA_WOT_SCENARIOS) + "/http/" + name + ".json",
                                std::ios::binary);
    ASSERT_TRUE(fixture_file && scenario_file) << name;
    const auto fixture = Value::parse(fixture_file);
    const auto steps = run(Value::parse(scenario_file));
    ASSERT_EQ(steps.size(), fixture["steps"].size());
    for (std::size_t i = 0; i < steps.size(); ++i) {
        EXPECT_EQ(steps[i].dump(2), fixture["steps"][i].dump(2))
            << name << ": " << fixture["steps"][i]["step"]["name"];
    }
}

INSTANTIATE_TEST_SUITE_P(Http, Fixtures,
                         testing::Values("counter", "validation", "default", "locked", "prefixed", "wot"),
                         [](const auto& info) { return std::string(info.param); });

}  // namespace
