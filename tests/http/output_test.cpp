// Blobs and MJPEG streams over HTTP: teta-wot-http's `output.rs` tests, and
// downloads, Blob outputs, absolute `href`s, the viewer page and the MJPEG
// feed (through a sink that keeps up only when told to).

#include "http/output.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "../log_capture.hpp"
#include "http/render.hpp"
#include "teta/wot.hpp"

namespace tw = teta::wot;
namespace http = teta::wot::http;
namespace blob = teta::wot::blob;
using tw::json::Value;
using tw::testing::TestClient;

namespace output_test {

using Text = tw::Blob<blob::TextPlain>;
using Jpeg = tw::Blob<blob::Jpeg>;

const std::string fake_jpeg = "\xff\xd8 frame \xff\xd9";

struct Probe {
    tw::MjpegStream stream;
    std::filesystem::path file;

    Text text() { return Text::from_bytes("some text"); }
    Jpeg remote() { return Jpeg::from_url("https://example.com/a.jpg"); }
    Text from_file() { return Text::from_file(file); }
    tw::OrderedMap<Text> two() {
        tw::OrderedMap<Text> out;
        out["a"] = Text::from_bytes("a");
        out["b"] = Text::from_bytes("b");
        return out;
    }

    static void define(tw::ThingSpec<Probe>& t) {
        t.stream("stream", &Probe::stream);
        t.action("text", &Probe::text);
        t.action("remote", &Probe::remote);
        t.action("from_file", &Probe::from_file);
        t.action("two", &Probe::two);
    }
};

struct Fixture {
    explicit Fixture(std::string prefix = "")
        : server(tw::ThingServer::builder().api_prefix(std::move(prefix)).thing("probe", probe).build()),
          client(TestClient::start(server)) {}
    ~Fixture() { client.stop(); }

    // Invokes an action, waits for it, and returns its invocation's URL.
    std::string invoke(const std::string& path) {
        const auto created = client.post_json(path);
        EXPECT_EQ(created.status, 201) << created.body;
        const auto location = *created.header("location");
        const auto invocation =
            client.runtime()->invocations().get(*tw::Uuid::parse(created.json()["id"].get<std::string>()));
        invocation->wait();
        return location;
    }

    std::shared_ptr<Probe> probe = std::make_shared<Probe>();
    tw::ThingServer server;
    TestClient client;
};

// A sink that records chunks, and keeps up only when told to.
class Sink final : public http::StreamSink {
  public:
    bool send(std::string_view chunk) override {
        chunks.emplace_back(chunk);
        return !gone;
    }
    void close() override { closed = true; }
    bool ready() const override { return keeping_up; }
    void set_resume(std::function<void()> callback) override { resume = std::move(callback); }

    std::vector<std::string> chunks;
    bool closed = false;
    bool gone = false;
    bool keeping_up = true;
    std::function<void()> resume;
};

std::string frame_chunk(const std::string& data) {
    return "--frame\r\nContent-Type: image/jpeg\r\n\r\n" + data + "\r\n";
}

}  // namespace output_test

using namespace output_test;

TEST(Output, TextTypesGetACharset) {
    EXPECT_EQ(http::detail::blob_content_type("text/plain"), "text/plain; charset=utf-8");
    EXPECT_EQ(http::detail::blob_content_type("text/csv; charset=latin-1"), "text/csv; charset=latin-1");
    EXPECT_EQ(http::detail::blob_content_type("text/csv; CHARSET=latin-1"), "text/csv; CHARSET=latin-1");
    EXPECT_EQ(http::detail::blob_content_type("image/jpeg"), "image/jpeg");
}

TEST(Output, HrefsAreMadeAbsoluteForLiveBlobsOnly) {
    const auto live = Text::from_bytes("x");
    const auto id = live.id()->to_string();
    Value value = {{"one", {{"href", "blob/" + id}}},
                   {"list", Value::array({{{"href", "blob/" + id}}})},
                   {"gone", {{"href", "blob/00000000-0000-0000-0000-000000000000"}}},
                   {"remote", {{"href", "https://example.com/blob/" + id}}}};
    http::detail::resolve_blobs(value, http::detail::Urls{"http://host", "/api"});
    EXPECT_EQ(value["one"]["href"], "http://host/api/blob/" + id);
    EXPECT_EQ(value["list"][0]["href"], "http://host/api/blob/" + id);
    EXPECT_EQ(value["gone"]["href"], "blob/00000000-0000-0000-0000-000000000000");
    EXPECT_EQ(value["remote"]["href"], "https://example.com/blob/" + id);
}

TEST(Output, ABlobOutputIsItsDataAndCanBeDownloaded) {
    Fixture f;
    const auto location = f.invoke("/probe/text");
    const auto invocation = f.client.get(location).json();
    const auto href = invocation["output"]["href"].get<std::string>();
    EXPECT_TRUE(href.starts_with("http://testserver/blob/")) << href;
    const auto output = f.client.get(location + "/output");
    EXPECT_EQ(output.status, 200);
    EXPECT_EQ(output.header("content-type"), "text/plain; charset=utf-8");
    EXPECT_EQ(output.body, "some text");
    const auto download = f.client.get(href);
    EXPECT_EQ(download.body, "some text");
    // Another host, another href.
    EXPECT_EQ(f.client.with_host("elsewhere:8000").get(location).json()["output"]["href"],
              "http://elsewhere:8000/blob/" + href.substr(href.rfind('/') + 1));
}

TEST(Output, SeveralBlobsAreJsonWithTheirHrefs) {
    Fixture f("/api");
    const auto location = f.invoke("/api/probe/two");
    const auto output = f.client.get(location + "/output");
    EXPECT_EQ(output.header("content-type"), "application/json");
    const auto a = output.json()["a"]["href"].get<std::string>();
    EXPECT_TRUE(a.starts_with("http://testserver/api/blob/")) << a;
    EXPECT_EQ(f.client.get(a).body, "a");
    EXPECT_EQ(f.client.get(output.json()["b"]["href"].get<std::string>()).body, "b");
    EXPECT_EQ(f.client.get("/blob/" + a.substr(a.rfind('/') + 1)).status, 404) << "only under the prefix";
}

TEST(Output, ARemoteBlobOutputRedirects) {
    Fixture f;
    const auto location = f.invoke("/probe/remote");
    const auto output = f.client.get(location + "/output");
    EXPECT_EQ(output.status, 307);
    EXPECT_EQ(output.header("location"), "https://example.com/a.jpg");
    EXPECT_EQ(f.client.get(location).json()["output"]["href"], "https://example.com/a.jpg");
}

TEST(Output, UnknownAndMalformedBlobIdsAreRefused) {
    Fixture f;
    const auto unknown = f.client.get("/blob/00000000-0000-0000-0000-000000000000");
    EXPECT_EQ(unknown.status, 404);
    EXPECT_EQ(unknown.json()["detail"], "Blob not found");
    const auto malformed = f.client.get("/blob/nope");
    EXPECT_EQ(malformed.status, 422);
    EXPECT_EQ(malformed.json()["detail"][0]["loc"], Value::array({"path", "blob_id"}));
}

TEST(Output, AFileThatCantBeReadIsAServerError) {
    Fixture f;
    f.probe->file =
        std::filesystem::temp_directory_path() / ("teta-wot-output-test-" + tw::Uuid::v4().to_string());
    std::ofstream(f.probe->file, std::ios::binary) << "on disk";
    const auto location = f.invoke("/probe/from_file");
    EXPECT_EQ(f.client.get(location + "/output").body, "on disk");
    std::filesystem::remove(f.probe->file);
    testing_support::LogCapture log;
    const auto output = f.client.get(location + "/output");
    EXPECT_EQ(output.status, 500);
    EXPECT_EQ(output.json()["detail"], "Internal Server Error");
    ASSERT_EQ(log.messages().size(), 1u);
    EXPECT_TRUE(log.messages()[0].starts_with("can't read the file of Blob ")) << log.messages()[0];
}

TEST(Output, TheViewerPageShowsTheStream) {
    Fixture f("/api");
    const auto page = f.client.get("/api/probe/stream/viewer");
    EXPECT_EQ(page.status, 200);
    EXPECT_EQ(page.header("content-type"), "text/html; charset=utf-8");
    EXPECT_EQ(page.body, "<html><body><img src='/api/probe/stream'></body></html>");
    EXPECT_EQ(f.client.get("/api/probe/nope/viewer").status, 404);
    EXPECT_EQ(f.client.post_json("/api/probe/stream").status, 405);
}

TEST(Output, TheStreamIsServedAsMultipartJpegs) {
    Fixture f;
    auto stream = f.client.stream("/probe/stream");
    EXPECT_EQ(stream.status, 200);
    EXPECT_EQ(stream.header("content-type"), "multipart/x-mixed-replace; boundary=frame");
    EXPECT_FALSE(stream.next_chunk_within(std::chrono::milliseconds(20))) << "frames from now on";
    (void)f.probe->stream.add_frame(fake_jpeg);
    EXPECT_EQ(stream.next_chunk(), frame_chunk(fake_jpeg));
    f.probe->stream.stop();
    EXPECT_EQ(stream.next_chunk(), std::nullopt);
    auto missing = f.client.stream("/probe/nope");
    EXPECT_EQ(missing.status, 404);
    EXPECT_EQ(missing.next_chunk(), R"({"detail":"Not Found"})");
    EXPECT_EQ(missing.next_chunk(), std::nullopt);
}

TEST(Output, AnMjpegClientGetsTheLatestFrameWhenReady) {
    const tw::MjpegStream stream;
    const auto broker = std::make_shared<tw::MessageBroker>();
    auto sink = std::make_shared<Sink>();
    {
        auto response = http::detail::mjpeg_response(stream, broker);
        EXPECT_EQ(response.headers.get("content-type"), "multipart/x-mixed-replace; boundary=frame");
        response.stream(sink);
    }  // the feed outlives the response
    (void)stream.add_frame(fake_jpeg);
    (void)stream.add_frame(fake_jpeg);
    ASSERT_EQ(sink->chunks.size(), 2u);
    EXPECT_EQ(sink->chunks[0], frame_chunk(fake_jpeg));
    // A slow client skips to the latest frame.
    sink->keeping_up = false;
    (void)stream.add_frame("\xff\xd8 1 \xff\xd9");
    (void)stream.add_frame("\xff\xd8 2 \xff\xd9");
    EXPECT_EQ(sink->chunks.size(), 2u);
    sink->keeping_up = true;
    sink->resume();
    ASSERT_EQ(sink->chunks.size(), 3u);
    EXPECT_EQ(sink->chunks[2], frame_chunk("\xff\xd8 2 \xff\xd9"));
    EXPECT_FALSE(sink->closed);
    stream.stop();
    EXPECT_TRUE(sink->closed);
}

TEST(Output, AnMjpegClientEndsWithTheBrokerOrWhenItGoes) {
    const tw::MjpegStream stream;
    const auto broker = std::make_shared<tw::MessageBroker>();
    auto first = std::make_shared<Sink>();
    auto second = std::make_shared<Sink>();
    http::detail::mjpeg_response(stream, broker).stream(first);
    http::detail::mjpeg_response(stream, broker).stream(second);
    second->gone = true;
    (void)stream.add_frame(fake_jpeg);
    EXPECT_TRUE(second->closed) << "a failed send ends the feed";
    EXPECT_FALSE(first->closed);
    broker->close();
    EXPECT_TRUE(first->closed);
    // A stopped stream ends at once.
    stream.stop();
    auto late = std::make_shared<Sink>();
    http::detail::mjpeg_response(stream, std::make_shared<tw::MessageBroker>()).stream(late);
    EXPECT_TRUE(late->closed);
    EXPECT_TRUE(late->chunks.empty());
}
