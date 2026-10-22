// End-to-end tests of the HTTP binding, in-process: a port of
// teta-wot-http's `tests/http.rs`.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <mutex>
#include <optional>
#include <regex>
#include <string>

#include "teta/wot.hpp"

namespace {

namespace tw = teta::wot;
using tw::json::Value;
using tw::testing::TestClient;
using tw::testing::TestResponse;
using namespace std::chrono_literals;

#define EXPECT_SAME(a, b) EXPECT_EQ(tw::json::dump(a), tw::json::dump(b))

class Counter {
  public:
    tw::Prop<std::int64_t> count{2};
    tw::Prop<std::int64_t> readonly{5};
    tw::Prop<std::optional<std::int64_t>> optional{std::optional<std::int64_t>(1)};

    std::int64_t computed() const { return count.get(); }
    void set_computed(std::int64_t value) { count.set(value); }
    std::int64_t broken() const { throw std::runtime_error("read failed"); }

    std::int64_t increment(std::int64_t input) {
        count.update([&](std::int64_t& n) { n += input; });
        tw::log::info("incremented counter");
        return count.get();
    }
    Value echo(Value input) { return input; }
    void noop() {}
    void wait(tw::ActionCtx& ctx) {
        {
            std::lock_guard lock(mutex_);
            started_ = true;
        }
        started_condition_.notify_all();
        throw ctx.cancelled();
    }
    void failed() { throw tw::HandledError("action failed"); }

    void wait_started() {
        std::unique_lock lock(mutex_);
        started_condition_.wait(lock, [&] { return started_; });
    }

    static void define(tw::ThingSpec<Counter>& t) {
        t.title("Counter").description("Counts through HTTP.");
        t.property("count", &Counter::count).unit("items").ge(0).le(10);
        t.property("readonly", &Counter::readonly).read_only();
        t.property("optional", &Counter::optional);
        t.property("computed", &Counter::computed).setter(&Counter::set_computed);
        t.property("broken", &Counter::broken);
        t.property("unlocked", &Counter::computed).setter(&Counter::set_computed).global_lock(false);
        t.action("increment", &Counter::increment, tw::whole_input).retention(5s);
        t.action("echo", &Counter::echo, tw::whole_input);
        t.action("noop", &Counter::noop);
        t.action("wait", &Counter::wait);
        t.action("failed", &Counter::failed);
    }

  private:
    std::mutex mutex_;
    std::condition_variable started_condition_;
    bool started_ = false;
};

/// A clock that tests move forward.
struct FakeClock {
    std::shared_ptr<std::atomic<std::int64_t>> offset = std::make_shared<std::atomic<std::int64_t>>(0);
    tw::Clock clock() const {
        return [offset = offset] {
            return std::chrono::steady_clock::now() + std::chrono::seconds(offset->load());
        };
    }
    void advance(std::chrono::seconds by) const { *offset += by.count(); }
};

struct Fixture {
    explicit Fixture(std::string prefix = "", bool locked = false) : prefix(std::move(prefix)) {
        auto builder = tw::ThingServer::builder();
        builder.thing("counter", thing).api_prefix(this->prefix).server_id("test-server").global_lock(locked);
        builder.runtime().global_lock_timeout(10ms).clock(clock.clock());
        server.emplace(builder.build());
        client.emplace(TestClient::start(*server));
    }
    ~Fixture() { client->stop(); }

    TestResponse send(const std::string& method, const std::string& path,
                      const std::optional<Value>& body = std::nullopt) const {
        if (body) {
            return client->request(method, prefix + path, {}, tw::json::dump(*body));
        }
        return client->request(method, prefix + path);
    }

    // A request exactly as given: no `Host` unless in `headers`.
    TestResponse raw(const std::string& method, const std::string& target,
                     const std::vector<std::pair<std::string, std::string>>& headers = {},
                     std::string body = {}) const {
        auto request = tw::http::Request::make(method, target);
        for (const auto& [name, value] : headers) {
            request.headers.add(name, value);
        }
        request.body = std::move(body);
        std::promise<tw::http::Response> answer;
        server->binding()->handle(
            std::move(request), [&](tw::http::Response response) { answer.set_value(std::move(response)); });
        auto response = answer.get_future().get();
        return {response.status, std::move(response.headers), std::move(response.body)};
    }

    std::pair<TestResponse, std::shared_ptr<tw::Invocation>> invoke(
        const std::string& action, const std::optional<Value>& input = {}) const {
        auto response = send("POST", "/counter/" + action, input);
        EXPECT_EQ(response.status, 201) << response.body;
        const auto id = tw::Uuid::parse(response.json()["id"].get<std::string>());
        return {response, server->runtime()->invocations().get(*id)};
    }

    FakeClock clock;
    std::shared_ptr<Counter> thing = std::make_shared<Counter>();
    std::string prefix;
    std::optional<tw::ThingServer> server;
    std::optional<TestClient> client;
};

void assert_json(const TestResponse& response, int status, const Value& body) {
    EXPECT_EQ(response.status, status) << response.body;
    EXPECT_EQ(response.header("content-type"), "application/json");
    EXPECT_SAME(response.json(), body);
}

TEST(Http, PropertyReadsWritesCoercionAndResetShareTheRuntimeCell) {
    Fixture fixture;
    assert_json(fixture.send("GET", "/counter/count"), 200, 2);
    assert_json(fixture.send("PUT", "/counter/count", "6"), 201, nullptr);
    EXPECT_EQ(fixture.thing->count.get(), 6);
    assert_json(fixture.send("GET", "/counter/count"), 200, 6);
    assert_json(fixture.send("POST", "/counter/count/reset"), 200, nullptr);
    EXPECT_EQ(fixture.thing->count.get(), 2);
    assert_json(fixture.send("PUT", "/counter/computed", 4), 201, nullptr);
    assert_json(fixture.send("GET", "/counter/computed"), 200, 4);
}

TEST(Http, ReadonlyAndNonresettablePropertiesHaveNoMutatingRoutes) {
    Fixture fixture;
    assert_json(fixture.send("GET", "/counter/readonly"), 200, 5);
    const auto response = fixture.send("PUT", "/counter/readonly", 9);
    assert_json(response, 405, {{"detail", "Method Not Allowed"}});
    EXPECT_EQ(response.header("allow"), "GET");
    for (const char* path : {"/counter/readonly/reset", "/counter/computed/reset"}) {
        assert_json(fixture.send("POST", path), 404, {{"detail", "Not Found"}});
    }
    EXPECT_EQ(fixture.thing->readonly.get(), 5);
}

TEST(Http, InvalidPropertyValuesReturn422WithoutChangingTheCell) {
    Fixture fixture;
    for (const auto& [input, kind] : std::vector<std::pair<Value, std::string>>{
             {11, "less_than_equal"}, {"bad", "int_parsing"}, {nullptr, "missing"}}) {
        const auto response = fixture.send("PUT", "/counter/count", input);
        EXPECT_EQ(response.status, 422);
        const auto body = response.json();
        EXPECT_EQ(body["detail"].size(), 1u);
        EXPECT_EQ(body["detail"][0]["type"], kind);
        EXPECT_SAME(body["detail"][0]["loc"], Value::array({"body"}));
        EXPECT_SAME(body["detail"][0]["input"], input);
        EXPECT_EQ(fixture.thing->count.get(), 2);
    }
    const auto response = fixture.send("PUT", "/counter/optional", Value(nullptr));
    EXPECT_EQ(response.status, 422);
    EXPECT_EQ(response.json()["detail"][0]["type"], "missing");
}

TEST(Http, MissingAndMalformedBodiesHaveDistinctValidationErrors) {
    Fixture fixture;
    const auto missing = fixture.send("PUT", "/counter/count");
    EXPECT_EQ(missing.status, 422);
    EXPECT_EQ(missing.json()["detail"][0]["type"], "missing");
    const auto malformed = fixture.raw("PUT", "/counter/count", {}, "{\n  \"x\":\n}");
    EXPECT_EQ(malformed.status, 422);
    const auto body = malformed.json();
    EXPECT_EQ(body["detail"][0]["type"], "json_invalid");
    EXPECT_SAME(body["detail"][0]["loc"], Value::parse(R"(["body", 9])"));
    EXPECT_EQ(body["detail"][0]["msg"], "JSON decode error");
    EXPECT_SAME(body["detail"][0]["input"], Value::object());
    EXPECT_NE(body["detail"][0]["ctx"]["error"].get<std::string>().find("expected value"), std::string::npos);
    EXPECT_EQ(fixture.thing->count.get(), 2);
}

TEST(Http, OversizedRequestBodiesReturn413BeforeMutating) {
    Fixture fixture;
    const auto response = fixture.raw("PUT", "/counter/count", {}, std::string(64 * 1024 * 1024 + 1, ' '));
    EXPECT_EQ(response.status, 413);
    EXPECT_TRUE(response.json()["detail"].is_string());
    EXPECT_EQ(fixture.thing->count.get(), 2);
}

TEST(Http, FailedPropertyGettersReturnProblemDetails) {
    Fixture fixture;
    assert_json(fixture.send("GET", "/counter/broken"), 500,
                {{"detail", "read failed"},
                 {"type", nullptr},
                 {"status", 500},
                 {"title", "PropertyError"},
                 {"instance", nullptr}});
}

TEST(Http, BusyGlobalLockReturns409ForWritesAndResetsButNotReadsOrOptOut) {
    Fixture fixture("", true);
    std::optional<tw::GlobalLock::Guard> guard =
        fixture.server->runtime()->global_lock()->try_acquire(tw::Uuid::v4());
    for (const auto& [method, path] : std::vector<std::pair<std::string, std::string>>{
             {"PUT", "/counter/count"}, {"POST", "/counter/count/reset"}}) {
        const auto response = fixture.send(method, path, 4);
        EXPECT_EQ(response.status, 409);
        EXPECT_EQ(response.json()["title"], "GlobalLockBusyError");
        EXPECT_EQ(response.json()["type"], tw::teta_wot_exception_type("GlobalLockBusyError"));
    }
    assert_json(fixture.send("GET", "/counter/count"), 200, 2);
    assert_json(fixture.send("PUT", "/counter/unlocked", 4), 201, nullptr);
    guard.reset();
    assert_json(fixture.send("PUT", "/counter/count", 6), 201, nullptr);
}

TEST(Http, UnknownPathsWrongMethodsAndHeadRequestsReturnExpectedResponses) {
    Fixture fixture;
    for (const char* path :
         {"/", "/missing", "/counter/missing", "/action_invocations//output", "/counter/count/extra"}) {
        assert_json(fixture.send("GET", path), 404, {{"detail", "Not Found"}});
    }
    for (const auto& [method, path, allow] : std::vector<std::tuple<std::string, std::string, std::string>>{
             {"POST", "/counter/count", "PUT"},
             {"PUT", "/counter/increment", "POST"},
             {"GET", "/counter/count/reset", "POST"}}) {
        const auto response = fixture.send(method, path);
        assert_json(response, 405, {{"detail", "Method Not Allowed"}});
        EXPECT_EQ(response.header("allow"), allow);
    }
    for (const auto& [path, status] :
         std::vector<std::pair<std::string, int>>{{"/counter/", 405}, {"/missing", 404}}) {
        const auto response = fixture.send("HEAD", path);
        EXPECT_EQ(response.status, status);
        EXPECT_TRUE(response.body.empty());
    }
}

TEST(Http, TrailingSlashRedirectsPreserveQueriesAndPrefixes) {
    Fixture fixture("/api/v1");
    for (const auto& [path, target] :
         std::vector<std::pair<std::string, std::string>>{{"/counter?x=1", "/counter/?x=1"},
                                                          {"/counter/count/?x=1", "/counter/count?x=1"},
                                                          {"/action_invocations/", "/action_invocations"}}) {
        const auto response = fixture.send("GET", path);
        EXPECT_EQ(response.status, 307);
        EXPECT_EQ(response.header("location"), "http://testserver/api/v1" + target);
        EXPECT_TRUE(response.body.empty());
    }
    assert_json(fixture.raw("GET", "/counter/count"), 404, {{"detail", "Not Found"}});
    assert_json(fixture.send("GET", "/counter/count"), 200, 2);
}

TEST(Http, ThingDescriptionsAndIndexesUseRequestUrlsAndStableIds) {
    Fixture fixture("/api");
    const auto response = fixture.send("GET", "/counter/");
    EXPECT_EQ(response.status, 200);
    const auto td = response.json();
    EXPECT_EQ(td["title"], "Counter");
    EXPECT_EQ(td["description"], "Counts through HTTP.");
    EXPECT_EQ(td["base"], "http://testserver/");
    EXPECT_EQ(td["id"], tw::td::thing_id("test-server", "counter"));
    EXPECT_EQ(td["properties"]["count"]["forms"][0]["href"], "/api/counter/count");
    EXPECT_EQ(td["actions"]["increment"]["forms"][0]["href"], "/api/counter/increment");
    assert_json(fixture.send("GET", "/things/"), 200, {{"counter", "http://testserver/api/counter/"}});
    assert_json(fixture.send("GET", "/thing_descriptions/"), 200, {{"counter", td}});
    const auto other =
        fixture.raw("GET", "https://uri.example/api/counter/", {{"host", "header.example:8443"}}).json();
    EXPECT_EQ(other["base"], "https://header.example:8443/");
    EXPECT_EQ(other["id"], td["id"]);
    assert_json(fixture.raw("GET", "https://uri.example/api/things/"), 200,
                {{"counter", "https://uri.example/api/counter/"}});
    assert_json(fixture.raw("GET", "/api/things/"), 200, {{"counter", "http://localhost/api/counter/"}});
}

TEST(Http, EmptyRuntimeIndexesReturnEmptyObjectsAndLists) {
    const auto server = tw::ThingServer::builder().build();
    const auto client = TestClient::start(server);
    for (const auto& [path, expected] :
         std::vector<std::pair<std::string, Value>>{{"/things/", Value::object()},
                                                    {"/thing_descriptions/", Value::object()},
                                                    {"/action_invocations", Value::array()}}) {
        const auto response = client.get(path);
        EXPECT_EQ(response.status, 200);
        EXPECT_SAME(response.json(), expected);
    }
}

TEST(Http, ActionCreationAndGetRenderFullRecordsAndLinks) {
    Fixture fixture("/api");
    const auto [created, invocation] = fixture.invoke("increment", "3");
    const std::string href = "http://testserver/api/action_invocations/" + invocation->id().to_string();
    EXPECT_EQ(created.header("location"), href);
    EXPECT_EQ(created.json()["href"], href);
    EXPECT_EQ(created.json()["action"], "/api/counter/increment");
    EXPECT_EQ(created.json()["input"], 3);
    EXPECT_EQ(created.json()["status"], "pending");
    EXPECT_TRUE(created.json()["output"].is_null());
    EXPECT_EQ(invocation->wait(), tw::InvocationStatus::completed);
    const auto response = fixture.send("GET", "/action_invocations/" + invocation->id().to_string());
    EXPECT_EQ(response.status, 200);
    const auto record = response.json();
    EXPECT_EQ(record["status"], "completed");
    EXPECT_EQ(record["output"], 5);
    EXPECT_TRUE(record["error"].is_null());
    const Value self = {{"href", href}, {"type", nullptr}, {"rel", "self"}, {"anchor", nullptr}};
    const Value output = {
        {"href", href + "/output"}, {"type", nullptr}, {"rel", "output"}, {"anchor", nullptr}};
    EXPECT_SAME(record["links"], Value::array({self, output}));
    bool logged = false;
    for (const auto& line : record["log"]) {
        logged = logged || line["message"] == "incremented counter";
    }
    EXPECT_TRUE(logged);
    static const std::regex naive(R"(\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d(\.\d+)?)");
    for (const char* key : {"timeRequested", "timeStarted", "timeCompleted"}) {
        EXPECT_TRUE(std::regex_match(record[key].get<std::string>(), naive)) << key;
    }
    const auto changed = fixture.raw("GET", "/api/action_invocations/" + invocation->id().to_string(),
                                     {{"host", "other.example"}});
    EXPECT_EQ(changed.json()["href"],
              "http://other.example/api/action_invocations/" + invocation->id().to_string());
}

TEST(Http, InvocationListsAreFilteredAndOmitFullRecordFields) {
    Fixture fixture;
    const auto first = fixture.invoke("increment", 1).second;
    first->wait();
    const auto second = fixture.invoke("noop").second;
    second->wait();
    const auto all = fixture.send("GET", "/action_invocations").json();
    ASSERT_EQ(all.size(), 2u);
    EXPECT_EQ(all[0]["id"], first->id().to_string());
    EXPECT_EQ(all[1]["id"], second->id().to_string());
    for (const auto& summary : all) {
        for (const char* field : {"input", "output", "log", "error"}) {
            EXPECT_FALSE(summary.contains(field));
        }
        EXPECT_EQ(summary["status"], "completed");
    }
    const auto filtered = fixture.send("GET", "/counter/increment").json();
    ASSERT_EQ(filtered.size(), 1u);
    EXPECT_EQ(filtered[0]["id"], first->id().to_string());
    assert_json(fixture.send("GET", "/counter/echo"), 200, Value::array());
}

TEST(Http, InvalidActionBodiesReturn422AndCreateNoInvocations) {
    Fixture fixture;
    for (const auto& input : std::vector<std::optional<Value>>{std::nullopt, Value(nullptr), Value("bad")}) {
        const std::string expected = input && input->is_string() ? "int_parsing" : "missing";
        const auto response = fixture.send("POST", "/counter/increment", input);
        EXPECT_EQ(response.status, 422);
        EXPECT_EQ(response.json()["detail"][0]["type"], expected);
        EXPECT_SAME(response.json()["detail"][0]["loc"], Value::array({"body"}));
        EXPECT_FALSE(response.header("location"));
    }
    const auto response = fixture.raw("POST", "/counter/increment", {}, "[broken");
    EXPECT_EQ(response.status, 422);
    EXPECT_EQ(response.json()["detail"][0]["type"], "json_invalid");
    EXPECT_TRUE(fixture.server->runtime()->invocations().list().empty());
    EXPECT_EQ(fixture.thing->count.get(), 2);
}

TEST(Http, InvocationOutputsPreserveFalsyValuesAndNullIsUnavailable) {
    Fixture fixture;
    for (const auto& value :
         {Value(0), Value(false), Value(""), Value::array(), Value::object(), Value(nullptr)}) {
        const auto invocation = fixture.invoke("echo", value).second;
        invocation->wait();
        const auto response =
            fixture.send("GET", "/action_invocations/" + invocation->id().to_string() + "/output");
        if (value.is_null()) {
            assert_json(response, 503, {{"detail", "No result is available for this invocation"}});
        } else {
            assert_json(response, 200, value);
        }
    }
}

TEST(Http, CancellationEndpointsCancelRunningActionsAndRejectFinishedOnes) {
    Fixture fixture;
    const auto invocation = fixture.invoke("wait").second;
    fixture.thing->wait_started();
    const std::string path = "/action_invocations/" + invocation->id().to_string();
    assert_json(fixture.send("GET", path + "/output"), 503,
                {{"detail", "No result is available for this invocation"}});
    assert_json(fixture.send("DELETE", path), 200, nullptr);
    EXPECT_EQ(invocation->wait(), tw::InvocationStatus::cancelled);
    const auto record = fixture.send("GET", path).json();
    EXPECT_EQ(record["status"], "cancelled");
    EXPECT_EQ(record["error"]["title"], "InvocationCancelledError");
    assert_json(fixture.send("DELETE", path), 503,
                {{"detail", "The invocation is cancelled and may not be cancelled."}});
    const auto completed = fixture.invoke("noop").second;
    completed->wait();
    assert_json(fixture.send("DELETE", "/action_invocations/" + completed->id().to_string()), 503,
                {{"detail", "The invocation is completed and may not be cancelled."}});
}

TEST(Http, MalformedAndMissingInvocationIdsReturn422And404) {
    Fixture fixture;
    const auto missing = tw::Uuid::v4().to_string();
    for (const auto& [method, suffix] :
         std::vector<std::pair<std::string, std::string>>{{"GET", ""}, {"DELETE", ""}, {"GET", "/output"}}) {
        const auto invalid = fixture.send(method, "/action_invocations/bad-id" + suffix);
        EXPECT_EQ(invalid.status, 422);
        EXPECT_EQ(invalid.json()["detail"][0]["type"], "uuid_parsing");
        EXPECT_SAME(invalid.json()["detail"][0]["loc"], Value::parse(R"(["path", "id"])"));
        EXPECT_EQ(invalid.json()["detail"][0]["input"], "bad-id");
        assert_json(fixture.send(method, "/action_invocations/" + missing + suffix), 404,
                    {{"detail", "No action invocation found with ID " + missing}});
    }
}

TEST(Http, ActionFailureIsReportedInTheRecordAndOutputRemainsUnavailable) {
    Fixture fixture;
    const auto invocation = fixture.invoke("failed").second;
    EXPECT_EQ(invocation->wait(), tw::InvocationStatus::error);
    const std::string path = "/action_invocations/" + invocation->id().to_string();
    const auto record = fixture.send("GET", path).json();
    EXPECT_EQ(record["status"], "error");
    EXPECT_EQ(record["error"]["detail"], "action failed");
    EXPECT_EQ(record["error"]["title"], "InvocationError");
    assert_json(fixture.send("GET", path + "/output"), 503,
                {{"detail", "No result is available for this invocation"}});
}

TEST(Http, ExpiredInvocationsAreRemovedOnTheNextActionRequest) {
    Fixture fixture;
    const auto first = fixture.invoke("increment", 1).second;
    first->wait();
    fixture.clock.advance(5s);
    const auto second = fixture.invoke("noop").second;
    second->wait();
    EXPECT_EQ(fixture.send("GET", "/action_invocations/" + first->id().to_string()).status, 404);
    const auto list = fixture.send("GET", "/action_invocations").json();
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(list[0]["id"], second->id().to_string());
}

}  // namespace
