// Blobs: teta-wot-core's `blob.rs` tests (media types, the registry, temporary
// directories, the JSON form and its schema), and what invocations do with
// Blobs: keeping their input and output alive, and placing a bad Blob's
// value_error where it is in the input.

#include "teta/wot/blob.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "teta/wot.hpp"

namespace tw = teta::wot;
namespace blob = teta::wot::blob;
using tw::json::Value;
using namespace std::chrono_literals;

namespace blob_test {

#define EXPECT_SAME(a, b) EXPECT_EQ(tw::json::dump(a), tw::json::dump(b))

struct VagueText {
    static constexpr std::string_view media_type = "text/*";
    static constexpr std::string_view title = "VagueTextBlob";
    static constexpr std::string_view description = "Some vague text.";
};

using Text = tw::Blob<blob::TextPlain>;

std::string decode_error(const Value& value) {
    try {
        (void)tw::Codec<Text>::decode(value);
    } catch (const tw::DecodeError& error) {
        return error.what();
    }
    return "<decoded>";
}

struct Probe {
    Text echo(Text blob) { return blob; }
    std::int64_t count(std::vector<Text> blobs) { return static_cast<std::int64_t>(blobs.size()); }
    Text made() { return Text::from_bytes("made"); }

    static void define(tw::ThingSpec<Probe>& t) {
        t.action<"blob">("echo", &Probe::echo).retention(1s);
        t.action<"blobs">("count", &Probe::count);
        t.action("made", &Probe::made).retention(1s);
    }
};

}  // namespace blob_test

using namespace blob_test;

TEST(Blob, MediaTypesParse) {
    EXPECT_EQ(blob::parse_media_type("text/plain"), (std::pair<std::string, std::string>{"text", "plain"}));
    EXPECT_EQ(blob::parse_media_type("text/plain; charset=utf-8"),
              (std::pair<std::string, std::string>{"text", "plain"}));
    EXPECT_EQ(blob::parse_media_type("*/*"), (std::pair<std::string, std::string>{"*", "*"}));
    for (const auto& [bad, message] :
         std::vector<std::pair<std::string, std::string>>{{"too/many/slashes", "exactly one '/'"},
                                                          {"/leadingslash", "both type and subtype"},
                                                          {"*/plain", "has no type"}}) {
        try {
            (void)blob::parse_media_type(bad);
            ADD_FAILURE() << bad;
        } catch (const blob::MediaTypeError& error) {
            EXPECT_NE(std::string(error.what()).find(message), std::string::npos) << error.what();
            EXPECT_EQ(error.kind(), blob::MediaTypeError::Kind::invalid);
        }
    }
    for (const auto& [media_type, pattern, expected] :
         std::vector<std::tuple<std::string, std::string, bool>>{{"text/plain", "text/plain", true},
                                                                 {"text/html", "text/*", true},
                                                                 {"image/png", "image/*", true},
                                                                 {"application/json", "*/*", true},
                                                                 {"text/plain", "text/html", false},
                                                                 {"image/jpeg", "image/png", false},
                                                                 {"text/plain", "image/*", false}}) {
        EXPECT_EQ(blob::media_types_match(media_type, pattern), expected) << media_type << " " << pattern;
    }
}

TEST(Blob, MediaTypesAreCheckedWhenBlobsAreMade) {
    const auto csv = tw::Blob<VagueText>::from_bytes_as("a,b", "text/csv");
    EXPECT_EQ(csv.media_type(), "text/csv");
    try {
        (void)Text::from_bytes_as("x", "image/png");
        ADD_FAILURE();
    } catch (const blob::MediaTypeError& error) {
        EXPECT_STREQ(error.what(),
                     "Can't create a TextBlob as media type 'image/png' doesn't match 'text/plain'.");
        EXPECT_EQ(error.kind(), blob::MediaTypeError::Kind::mismatch);
    }
    EXPECT_THROW((void)csv.cast<blob::TextPlain>(), blob::MediaTypeError);
    const auto any = csv.cast<blob::AnyMedia>();
    EXPECT_EQ(any.id(), csv.id()) << "a cast shares the data";
}

TEST(Blob, DataIsFreedWithTheLastHandle) {
    auto blob = std::make_optional(Text::from_bytes("hello"));
    const auto id = *blob->id();
    auto copy = blob;
    blob.reset();
    EXPECT_NE(blob::BlobData::find(id), nullptr);
    EXPECT_EQ(copy->bytes(), "hello");
    copy.reset();
    EXPECT_EQ(blob::BlobData::find(id), nullptr);
}

TEST(Blob, TemporaryDirectoriesAreDeletedWithTheData) {
    std::filesystem::path path;
    {
        blob::TemporaryDirectory folder;
        path = folder.path();
        std::ofstream(path / "a.txt", std::ios::binary) << "file";
        const auto blob = Text::from_temporary_directory(std::move(folder), "a.txt");
        EXPECT_TRUE(std::filesystem::exists(path)) << "moving the folder doesn't delete it";
        EXPECT_EQ(blob.bytes(), "file");
    }
    EXPECT_FALSE(std::filesystem::exists(path));
    try {
        (void)Text::from_file(path / "gone.txt");
        ADD_FAILURE();
    } catch (const std::runtime_error& error) {
        EXPECT_STREQ(error.what(), "Tried to return a file that doesn't exist.");
    }
}

TEST(Blob, AFileOutsideATemporaryDirectoryIsLeftAlone) {
    blob::TemporaryDirectory folder;
    std::ofstream(folder.path() / "kept.txt", std::ios::binary) << "kept";
    {
        const auto blob = Text::from_file(folder.path() / "kept.txt");
        EXPECT_EQ(blob.bytes(), "kept");
        EXPECT_EQ(tw::Blob<VagueText>::from_file(folder.path() / "kept.txt").media_type(), "text/*")
            << "a file has the Blob's media type, unless it is given one";
        EXPECT_EQ(tw::Blob<VagueText>::from_file_as(folder.path() / "kept.txt", "text/csv").media_type(),
                  "text/csv");
        EXPECT_THROW((void)Text::from_file_as(folder.path() / "kept.txt", "image/png"), blob::MediaTypeError);
    }
    EXPECT_TRUE(std::filesystem::exists(folder.path() / "kept.txt"));
}

TEST(Blob, EncodedBlobsAreCapturedWithTheirJson) {
    Value value;
    std::vector<std::shared_ptr<blob::BlobData>> captured;
    tw::Uuid id;
    {
        const auto blob = Text::from_bytes("x");
        id = *blob.id();
        blob::Capture capture;
        value = tw::Codec<Text>::encode(blob);
        captured = capture.take();
    }
    EXPECT_SAME(value, (Value{{"href", "blob/" + id.to_string()},
                              {"media_type", "text/plain"},
                              {"rel", "output"},
                              {"description", std::string(blob::default_description)}}));
    ASSERT_EQ(captured.size(), 1u);
    EXPECT_EQ(blob::as_blob(value, captured), captured[0]);
    const auto back = tw::Codec<Text>::decode(value);
    EXPECT_EQ(back.id(), id);
    EXPECT_EQ(blob::as_blob(Value{{"a", 1}}, captured), nullptr);
    EXPECT_EQ(blob::as_blob(Value::array({value}), captured), nullptr) << "only exactly one Blob";
    EXPECT_EQ(blob::as_blob(value, {}), nullptr);
}

TEST(Blob, NestedCapturesCollectIntoTheInnermost) {
    const auto first = Text::from_bytes("1");
    const auto second = Text::from_bytes("2");
    blob::Capture outer;
    (void)tw::Codec<Text>::encode(first);
    {
        blob::Capture inner;
        (void)tw::Codec<Text>::encode(second);
        EXPECT_EQ(inner.take(), (std::vector{second.data()}));
    }
    EXPECT_EQ(outer.take(), (std::vector{first.data()}));
    blob::Capture::record(first.data());  // without a capture: nothing
}

TEST(Blob, DecodingFindsTheDataByItsHref) {
    const auto jpeg = tw::Blob<blob::Jpeg>::from_bytes("\xff");
    const auto href = "http://host/api/blob/" + jpeg.id()->to_string();
    const auto found = tw::Codec<tw::Blob<>>::decode(Value{{"href", href}, {"media_type", "image/jpeg"}});
    EXPECT_EQ(found.media_type(), "image/jpeg");
    EXPECT_EQ(found.id(), jpeg.id());
    const auto message = [](const Value& value) {
        try {
            (void)tw::Codec<Text>::decode(value);
        } catch (const tw::ValueDecodeError& error) {
            EXPECT_TRUE(error.path().empty());
            return error.message();
        }
        return std::string("<decoded>");
    };
    EXPECT_EQ(message(Value{{"href", href}, {"media_type", "x"}}),
              "Blob data media_type 'image/jpeg' does not match Blob media_type 'text/plain'.");
    EXPECT_EQ(message(Value{{"href", "http://x/bogus"}, {"media_type", "x"}}),
              "Blob URLs must contain a Blob ID.");
    EXPECT_EQ(message(Value{{"href", "blob/zzz"}, {"media_type", "x"}}),
              "badly formed hexadecimal UUID string");
    EXPECT_EQ(message(Value{{"href", "blob/" + tw::Uuid().to_string()}, {"media_type", "x"}}),
              "Blob ID 00000000-0000-0000-0000-000000000000 wasn't found on this server.");
    // The simple form of the ID is an ID too.
    auto simple = jpeg.id()->to_string();
    std::erase(simple, '-');
    EXPECT_EQ(tw::Codec<tw::Blob<>>::decode(Value{{"href", "blob/" + simple}, {"media_type", "x"}}).id(),
              jpeg.id());
}

TEST(Blob, TheJsonFormIsReadAsSerdeDerivesIt) {
    const auto text = Text::from_bytes("t");
    const auto href = "blob/" + text.id()->to_string();
    EXPECT_EQ(decode_error(Value{{"media_type", "text/plain"}}), "missing field `href`");
    EXPECT_EQ(decode_error(Value{{"href", href}}), "missing field `media_type`");
    EXPECT_EQ(decode_error(Value::object()), "missing field `href`");
    EXPECT_EQ(decode_error(Value{{"href", 5}, {"media_type", "text/plain"}}),
              "invalid type: integer `5`, expected a string");
    EXPECT_EQ(decode_error(Value{{"media_type", false}, {"href", 5}}),
              "invalid type: boolean `false`, expected a string")
        << "the fields in the order they come";
    EXPECT_EQ(decode_error("blob"), "invalid type: string \"blob\", expected struct BlobModel");
    // A sequence of the fields, as serde allows.
    EXPECT_EQ(tw::Codec<Text>::decode(Value::array({href, "text/plain"})).id(), text.id());
    EXPECT_EQ(decode_error(Value::array()), "invalid length 0, expected struct BlobModel with 2 elements");
    EXPECT_EQ(decode_error(Value::array({href})),
              "invalid length 1, expected struct BlobModel with 2 elements");
    EXPECT_EQ(decode_error(Value::array({href, "text/plain", 1})),
              "invalid length 3, expected fewer elements in array");
    // Other fields are ignored.
    EXPECT_EQ(tw::Codec<Text>::decode(Value{{"href", href}, {"media_type", "text/plain"}, {"extra", 1}}).id(),
              text.id());
}

TEST(Blob, TheSchemaHasAStringConst) {
    const auto jpeg = tw::Codec<tw::Blob<blob::Jpeg>>::json_schema();
    EXPECT_EQ(jpeg["title"], "JPEGBlob");
    EXPECT_EQ(jpeg["properties"]["media_type"]["const"], "image/jpeg");
    const auto vague = tw::Codec<tw::Blob<VagueText>>::json_schema();
    EXPECT_FALSE(vague["properties"]["media_type"].contains("const"));
    EXPECT_EQ(vague["properties"]["media_type"]["default"], "text/*");
    EXPECT_EQ(vague["properties"]["description"]["default"], "Some vague text.");
    EXPECT_SAME(vague["required"], Value::array({"href", "media_type"}));
}

TEST(Blob, DescriptionsAndRemoteBlobs) {
    const auto described = Text::from_bytes("x").with_description("Some words.");
    EXPECT_EQ(tw::Codec<Text>::encode(described)["description"], "Some words.");
    EXPECT_EQ(tw::Blob<VagueText>::from_bytes_as("x", "text/csv").description(), "Some vague text.");
    const auto remote = tw::Blob<blob::Jpeg>::from_url("https://example.com/a.jpg");
    EXPECT_EQ(remote.id(), std::nullopt);
    EXPECT_EQ(tw::Codec<tw::Blob<blob::Jpeg>>::encode(remote)["href"], "https://example.com/a.jpg");
    EXPECT_THROW((void)remote.bytes(), std::runtime_error);
    EXPECT_EQ(blob::BlobData::find(remote.data()->id()), nullptr) << "remote data isn't registered";
    EXPECT_THROW((void)tw::Codec<Text>::encode(Text()), std::invalid_argument);
}

TEST(Blob, LocalHrefsAreOnlyThoseOfLiveData) {
    auto blob = std::make_optional(Text::from_bytes("x"));
    const auto id = *blob->id();
    EXPECT_EQ(blob::local_href(id), "blob/" + id.to_string());
    EXPECT_EQ(blob::local_href_id("blob/" + id.to_string()), id);
    EXPECT_EQ(blob::local_href_id("http://host/blob/" + id.to_string()), std::nullopt);
    EXPECT_EQ(blob::local_href_id("blob/nope"), std::nullopt);
    blob.reset();
    EXPECT_EQ(blob::local_href_id("blob/" + id.to_string()), std::nullopt);
}

TEST(Blob, AnInvocationKeepsItsInputAndOutputUntilItExpires) {
    auto now = std::make_shared<std::chrono::steady_clock::time_point>(std::chrono::steady_clock::now());
    auto runtime = tw::Runtime::builder()
                       .clock([now] { return *now; })
                       .thing("probe", std::make_shared<Probe>())
                       .build();
    const auto* thing = runtime.thing("probe");
    auto input = std::make_optional(Text::from_bytes("in"));
    const auto input_id = *input->id();
    auto echoed = thing->action("echo")->invoke(
        Value{{"blob", {{"href", "blob/" + input_id.to_string()}, {"media_type", "text/plain"}}}});
    input.reset();
    ASSERT_EQ(echoed->wait(), tw::InvocationStatus::completed);
    EXPECT_NE(blob::BlobData::find(input_id), nullptr) << "held by the invocation";
    EXPECT_EQ(echoed->output_blob(), blob::BlobData::find(input_id)) << "an output of one Blob is its data";
    EXPECT_EQ((*echoed->output())["href"], "blob/" + input_id.to_string());

    auto made = thing->action("made")->invoke(nullptr);
    ASSERT_EQ(made->wait(), tw::InvocationStatus::completed);
    const auto made_id = blob::local_href_id((*made->output())["href"].get<std::string>());
    ASSERT_TRUE(made_id) << "the action's own handle is gone, the invocation's is not";
    EXPECT_EQ(made->output_blob()->media_type(), "text/plain");

    *now += 2s;
    EXPECT_EQ(runtime.invocations().expire(), 2u);
    echoed.reset();
    made.reset();
    EXPECT_EQ(blob::BlobData::find(input_id), nullptr);
    EXPECT_EQ(blob::BlobData::find(*made_id), nullptr);
}

TEST(Blob, ABadBlobsValueErrorIsWhereItIsInTheInput) {
    auto runtime = tw::Runtime::builder().thing("probe", std::make_shared<Probe>()).build();
    const auto good = Text::from_bytes("good");
    const Value bad = {{"href", "blob/zzz"}, {"media_type", "text/plain"}};
    const Value blobs = Value::array({tw::Codec<Text>::encode(good), bad});
    try {
        (void)runtime.thing("probe")->action("count")->invoke(Value{{"blobs", blobs}});
        ADD_FAILURE();
    } catch (const tw::ValidationError& error) {
        ASSERT_EQ(error.issues().size(), 1u);
        EXPECT_SAME(tw::to_json(error.issues()[0]),
                    (Value{{"type", "value_error"},
                           {"loc", Value::array({"body", "blobs", 1})},
                           {"msg", "Value error, badly formed hexadecimal UUID string"},
                           {"input", bad},
                           {"ctx", {{"error", Value::object()}}}}));
    }
    const auto counted = runtime.thing("probe")->action("count")->invoke(
        Value{{"blobs", Value::array({tw::Codec<Text>::encode(good)})}});
    EXPECT_EQ(counted->wait(), tw::InvocationStatus::completed);
    EXPECT_EQ(*counted->output(), 1);
}
