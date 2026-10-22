// What the C++ binding adds to teta-wot-http's tests: invocation IDs read as
// the `uuid` crate reads them (tests/http/wire_corpus.jsonl), custom
// endpoints, server-sent events, and Thing code running on workers.

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include "http/path_uuid.hpp"
#include "teta/wot.hpp"

namespace {

namespace tw = teta::wot;
namespace http = teta::wot::http;
using tw::json::Value;
using tw::testing::TestClient;
using namespace std::chrono_literals;

// An event's data as text, or "end" at the end of the stream.
std::string text(const std::optional<Value>& data) {
    return data ? tw::json::dump(*data) : "end";
}

TEST(PathUuid, ReadsIdsAsTheUuidCrate) {
    std::ifstream file(TETA_WOT_WIRE_CORPUS, std::ios::binary);
    ASSERT_TRUE(file);
    std::string line;
    int cases = 0;
    while (std::getline(file, line)) {
        const auto entry = Value::parse(line);
        if (!entry.contains("uuid")) {
            continue;
        }
        const auto parsed = http::detail::parse_path_uuid(entry["uuid"].get<std::string>());
        if (entry.contains("value")) {
            ASSERT_TRUE(std::holds_alternative<tw::Uuid>(parsed)) << line;
            EXPECT_EQ(std::get<tw::Uuid>(parsed).to_string(), entry["value"].get<std::string>()) << line;
        } else {
            ASSERT_TRUE(std::holds_alternative<std::string>(parsed)) << line;
            EXPECT_EQ(std::get<std::string>(parsed), entry["error"].get<std::string>()) << line;
        }
        ++cases;
    }
    EXPECT_GT(cases, 30);
}

// ---- Custom endpoints (teta-wot's `custom-endpoint` example) ------------------------------------

class Logger {
  public:
    std::vector<std::pair<std::uint32_t, double>> readings = {{0, 20.5}, {60, 21.0}, {120, 21.75}};
    std::vector<std::string> notes;

    std::int64_t count() const { return static_cast<std::int64_t>(readings.size()); }

    http::Response readings_csv() const {
        std::string csv = "seconds,celsius\n";
        for (const auto& [seconds, celsius] : readings) {
            csv += std::format("{},{}\n", seconds, celsius);
        }
        auto response = http::Response::bytes(std::move(csv), "text/csv");
        response.headers.set("content-disposition", "attachment; filename=\"readings.csv\"");
        return response;
    }
    http::Response summary(const http::Request& request) const {
        double total = 0;
        for (const auto& reading : readings) {
            total += reading.second;
        }
        const double mean = total / static_cast<double>(readings.size());
        if (request.query == "unit=F") {
            return http::Response::text(
                std::format("{} readings, mean {:.1f} °F", readings.size(), mean * 1.8 + 32));
        }
        return http::Response::text(std::format("{} readings, mean {:.1f} °C", readings.size(), mean));
    }
    http::Response add_note(const http::Request& request) {
        const auto author = request.headers.get("x-author").value_or("anonymous");
        notes.push_back(std::string(author) + ": " + request.body);
        return http::Response::empty(204);
    }
    http::Response fail() { throw std::runtime_error("the logger is unplugged"); }

    static void define(tw::ThingSpec<Logger>& t) {
        t.doc("A data logger.");
        t.property("count", &Logger::count).doc("How many readings there are.");
        t.endpoint("get", "readings.csv", &Logger::readings_csv)
            .doc("The readings as a CSV file.")
            .link("alternate", "text/csv");
        t.endpoint("GET", "summary.txt", &Logger::summary);
        t.endpoint("POST", "notes", &Logger::add_note);
        t.endpoint("GET", "files/broken", &Logger::fail);
    }
};

TEST(Endpoints, AnswerTheirMethodAndPathOutsideTheTd) {
    const auto server = tw::ThingServer::builder().thing<Logger>("logger").build();
    const auto client = TestClient::start(server);
    const auto csv = client.get("/logger/readings.csv");
    EXPECT_EQ(csv.status, 200);
    EXPECT_EQ(csv.header("content-type"), "text/csv");
    EXPECT_EQ(csv.header("content-disposition"), "attachment; filename=\"readings.csv\"");
    EXPECT_EQ(csv.header("vary"), "Origin");
    EXPECT_EQ(csv.body, "seconds,celsius\n0,20.5\n60,21\n120,21.75\n");
    EXPECT_EQ(client.get("/logger/summary.txt").body, "3 readings, mean 21.1 °C");
    EXPECT_EQ(client.get("/logger/summary.txt?unit=F").body, "3 readings, mean 69.9 °F");
    const auto posted =
        client.request("POST", "/logger/notes", {{"x-author", "Ada"}}, "Calibrated the probe.");
    EXPECT_EQ(posted.status, 204);
    EXPECT_FALSE(posted.header("content-type"));
    EXPECT_EQ(client.runtime()->thing_ref<Logger>("logger")->thing().notes,
              std::vector<std::string>{"Ada: Calibrated the probe."});
    const auto wrong = client.get("/logger/notes");
    EXPECT_EQ(wrong.status, 405);
    EXPECT_EQ(wrong.header("allow"), "POST");

    const auto td = client.get("/logger/").json();
    EXPECT_EQ(td["properties"].size(), 1u);
    bool linked = false;
    for (const auto& link : td["links"]) {
        linked = linked || (link["href"] == "/logger/readings.csv" && link["rel"] == "alternate" &&
                            link["type"] == "text/csv");
    }
    EXPECT_TRUE(linked) << td["links"].dump();
}

TEST(Endpoints, AFailingEndpointIsA500) {
    const auto server = tw::ThingServer::builder().thing<Logger>("logger").build();
    const auto response = TestClient::start(server).get("/logger/files/broken");
    EXPECT_EQ(response.status, 500);
    EXPECT_EQ(response.header("content-type"), "application/problem+json");
    EXPECT_EQ(response.json()["detail"], "the logger is unplugged");
}

struct Clash {
    tw::Prop<std::int64_t> count{0};
    http::Response answer() { return http::Response::text("taken"); }
    static void define(tw::ThingSpec<Clash>& t) {
        t.property("count", &Clash::count);
        t.endpoint("GET", "count", &Clash::answer);
    }
};

TEST(Endpoints, ARouteThatIsTakenIsRefused) {
    try {
        (void)tw::ThingServer::builder().thing<Clash>("clash").build();
        FAIL();
    } catch (const http::RouteError& error) {
        EXPECT_EQ(error.kind(), http::RouteError::Kind::endpoint_conflict);
        EXPECT_STREQ(error.what(),
                     "an endpoint of Thing `clash` answers `GET /clash/count`, which is already taken");
    }
}

template <const char* Path>
struct BadPath {
    http::Response answer() { return http::Response::text(""); }
    static void define(tw::ThingSpec<BadPath>& t) { t.endpoint("GET", Path, &BadPath::answer); }
};
constexpr char absolute[] = "/x";
constexpr char empty_segment[] = "a//b";
constexpr char parent[] = "a/../b";

TEST(Endpoints, PathsAreRelativeWithoutEmptyOrDotSegments) {
    EXPECT_THROW(tw::Runtime::builder().thing<BadPath<absolute>>("t").build(), tw::BuildError);
    EXPECT_THROW(tw::Runtime::builder().thing<BadPath<empty_segment>>("t").build(), tw::BuildError);
    EXPECT_THROW(tw::Runtime::builder().thing<BadPath<parent>>("t").build(), tw::BuildError);
}

// ---- Server-sent events --------------------------------------------------------------------

struct Sensor {
    tw::Prop<std::int64_t> level{0};
    tw::Prop<std::string> label{"a"};
    tw::Event<std::int64_t> alarm;

    std::int64_t doubled() const { return level.get() * 2; }

    static void define(tw::ThingSpec<Sensor>& t) {
        t.property("level", &Sensor::level);
        t.property("label", &Sensor::label);
        t.property("doubled", &Sensor::doubled);
        t.event("alarm", &Sensor::alarm);
    }
};

struct SensorFixture {
    explicit SensorFixture(http::WireProfile profile = http::WireProfile::tetathing)
        : server(tw::ThingServer::builder().thing("sensor", sensor).wire_profile(profile).build()),
          client(TestClient::start(server)) {}
    std::shared_ptr<Sensor> sensor = std::make_shared<Sensor>();
    tw::ThingServer server;
    TestClient client;
};

TEST(Sse, APropertyStreamSendsEachValue) {
    SensorFixture fixture;
    auto events = fixture.client.events("/sensor/level");
    EXPECT_EQ(events.status, 200);
    EXPECT_EQ(events.header("content-type"), "text/event-stream");
    EXPECT_EQ(events.header("cache-control"), "no-cache");
    EXPECT_EQ(events.header("vary"), "Origin");
    fixture.sensor->level.set(3);
    fixture.sensor->level.set(4);
    const auto first = events.next_message();
    ASSERT_TRUE(first);
    EXPECT_EQ(first->data, 3);
    EXPECT_FALSE(first->event) << "unnamed in the tetathing profile";
    EXPECT_EQ(text(events.next()), "4");
    fixture.client.stop();
    EXPECT_EQ(text(events.next()), "end") << "closing the broker ends the stream";
}

TEST(Sse, AnEventStreamSendsTheEventsData) {
    SensorFixture fixture;
    auto events = fixture.client.events("/sensor/alarm");
    fixture.sensor->alarm.emit(7);
    EXPECT_EQ(text(events.next()), "7");
}

TEST(Sse, StreamsOfAllPropertiesOrEventsNameTheirEvents) {
    SensorFixture fixture;
    auto properties = fixture.client.events("/sensor/properties");
    auto all_events = fixture.client.events("/sensor/events");
    fixture.sensor->label.set("b");
    fixture.sensor->level.set(1);
    fixture.sensor->alarm.emit(2);
    auto message = properties.next_message();
    ASSERT_TRUE(message);
    EXPECT_EQ(message->event, "label");
    EXPECT_EQ(message->data, "b");
    ASSERT_TRUE(message->id);
    EXPECT_TRUE(message->id->ends_with("Z"));
    EXPECT_EQ(message->id->size(), std::string("2026-10-22T06:34:26.123456789Z").size());
    message = properties.next_message();
    ASSERT_TRUE(message);
    EXPECT_EQ(message->event, "level");
    message = all_events.next_message();
    ASSERT_TRUE(message);
    EXPECT_EQ(message->event, "alarm");
    EXPECT_EQ(message->data, 2);
}

TEST(Sse, StreamsAreNamedInTheWotProfile) {
    SensorFixture fixture(http::WireProfile::wot);
    auto events = fixture.client.events("/sensor/level");
    fixture.sensor->level.set(5);
    const auto message = events.next_message();
    ASSERT_TRUE(message);
    EXPECT_EQ(message->event, "level");
    EXPECT_TRUE(message->id);
}

TEST(Sse, FunctionalPropertiesAreReadNotStreamed) {
    SensorFixture fixture;
    const auto response = fixture.client.request("GET", "/sensor/doubled", {{"accept", "text/event-stream"}});
    EXPECT_EQ(response.status, 200);
    EXPECT_EQ(response.header("content-type"), "application/json");
    EXPECT_EQ(response.body, "0");
}

TEST(Sse, AClientThatGoesAwayIsForgotten) {
    SensorFixture fixture;
    {
        auto events = fixture.client.events("/sensor/level");
        fixture.sensor->level.set(1);
        EXPECT_EQ(text(events.next()), "1");
    }
    // The stream's next send fails, and it unsubscribes.
    fixture.sensor->level.set(2);
    EXPECT_FALSE(fixture.server.runtime()->broker()->has_subscribers("sensor", "level"));
}

// ---- Threads ---------------------------------------------------------------------------------

struct Where {
    std::int64_t on_caller() const { return std::this_thread::get_id() == caller ? 1 : 0; }
    std::thread::id caller = std::this_thread::get_id();
    static void define(tw::ThingSpec<Where>& t) { t.property("on_caller", &Where::on_caller); }
};

TEST(Threads, GettersRunOnWorkersNotTheTransportsThread) {
    const auto server = tw::ThingServer::builder().thing<Where>("where").build();
    EXPECT_EQ(TestClient::start(server).get("/where/on_caller").body, "0");
}

}  // namespace
