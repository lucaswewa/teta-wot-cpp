// Settings: saved after every change, loaded before the Things start, with
// teta-wot's file format and warnings. The conformance scenarios `settings`
// and `settings-load` compare the files and warnings with the reference's.

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <string>

#include "../log_capture.hpp"
#include "../temp_folder.hpp"
#include "teta/wot.hpp"

namespace tw = teta::wot;
using tw::json::Value;

namespace settings_test {

struct Offsets {
    double offset = 0.0;
    double gain = 1.0;
    bool operator==(const Offsets&) const = default;
};

class Probe {
  public:
    tw::Prop<std::int64_t> count{0};
    tw::Prop<std::string> label{"probe"};
    tw::Prop<Offsets> offsets{Offsets{}};
    tw::Prop<bool> plain{false};
    std::optional<std::int64_t> count_at_start;

    double exposure() const {
        std::lock_guard lock(mutex_);
        return exposure_;
    }
    void set_exposure(double value) {
        std::lock_guard lock(mutex_);
        exposure_ = value;
    }
    void start(tw::ThingCtx& ctx) {
        count_at_start = count.get();
        folder = ctx.settings_folder();
    }
    std::optional<std::filesystem::path> folder;

    static void define(tw::ThingSpec<Probe>& t) {
        t.property("count", &Probe::count).setting().ge(0);
        t.property("label", &Probe::label).setting();
        t.property("offsets", &Probe::offsets).setting();
        t.property("plain", &Probe::plain);
        t.property("exposure", &Probe::exposure).setter(&Probe::set_exposure).setting();
        t.on_start(&Probe::start);
    }

  private:
    mutable std::mutex mutex_;
    double exposure_ = 10.0;
};

class Plain {
  public:
    tw::Prop<std::int64_t> value{0};
    static void define(tw::ThingSpec<Plain>& t) { t.property("value", &Plain::value); }
};

// The file's text, with CRLF read as LF; nullopt if there is none.
std::optional<std::string> read(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    std::string lf;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
            continue;
        }
        lf += text[i];
    }
    return lf;
}

void write(const std::filesystem::path& file, const std::string& text) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

struct Server {
    explicit Server(const std::filesystem::path& folder)
        : runtime(tw::Runtime::builder().settings_folder(folder).thing<Probe>("probe").build()),
          probe(*runtime.thing_ref<Probe>("probe")) {
        runtime.start();
    }
    ~Server() { runtime.stop(); }

    tw::Runtime runtime;
    tw::ThingRef<Probe> probe;
};

const std::string defaults_after_count_3 =
    "{\n"
    "    \"count\": 3,\n"
    "    \"exposure\": 10.0,\n"
    "    \"label\": \"probe\",\n"
    "    \"offsets\": {\n"
    "        \"offset\": 0.0,\n"
    "        \"gain\": 1.0\n"
    "    }\n"
    "}";

}  // namespace settings_test

using namespace settings_test;

TEST(Settings, TheFileIsPydanticsIndentedJson) {
    EXPECT_EQ(tw::to_settings_json(Value::parse(R"({"b":{"x":[1,2.5,{}],"y":[]},"a":"é\u0001","c":1e-7})")),
              "{\n"
              "    \"b\": {\n"
              "        \"x\": [\n"
              "            1,\n"
              "            2.5,\n"
              "            {}\n"
              "        ],\n"
              "        \"y\": []\n"
              "    },\n"
              "    \"a\": \"é\\u0001\",\n"
              "    \"c\": 1e-7\n"
              "}");
}

TEST(Settings, NothingIsSavedBeforeAChangeThenEverySettingIsInKeyOrder) {
    const testing_support::TemporaryFolder folder;
    const auto file = folder.path() / "probe" / "Settings-Probe.json";
    Server server(folder.path());
    EXPECT_EQ(server.runtime.thing("probe")->settings_file(), file);
    EXPECT_FALSE(std::filesystem::exists(file));
    server.probe.write_property("count", 3);
    EXPECT_EQ(read(file), defaults_after_count_3);

    // CRLF on Windows, as Python writes text files there.
    std::ifstream in(file, std::ios::binary);
    const std::string raw{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
#ifdef _WIN32
    EXPECT_NE(raw.find("{\r\n    \"count\""), std::string::npos);
#else
    EXPECT_EQ(raw.find('\r'), std::string::npos);
#endif
    EXPECT_FALSE(std::filesystem::exists(file.string() + ".tmp"));
}

TEST(Settings, ChangesFromCppAreSavedAndInvalidOnesNot) {
    const testing_support::TemporaryFolder folder;
    const auto file = folder.path() / "probe" / "Settings-Probe.json";
    Server server(folder.path());
    server.probe.thing().count.set(3);
    EXPECT_EQ(read(file), defaults_after_count_3);
    EXPECT_THROW(server.probe.thing().count.set(-1), tw::PropertyError);
    EXPECT_THROW(server.probe.write_property("count", -1), tw::PropertyError);
    EXPECT_EQ(read(file), defaults_after_count_3);
    // A property that isn't a setting is never saved.
    server.probe.write_property("plain", true);
    EXPECT_EQ(read(file), defaults_after_count_3);
}

TEST(Settings, AFunctionalSettingSavesItsGettersValueAfterAWriteThroughTheProperty) {
    const testing_support::TemporaryFolder folder;
    const auto file = folder.path() / "probe" / "Settings-Probe.json";
    Server server(folder.path());
    server.probe.write_property("exposure", 12.5);
    EXPECT_NE(read(file)->find("\"exposure\": 12.5,"), std::string::npos);
    // A change that bypasses the property is saved by the next write through
    // it, not by another setting's.
    server.probe.thing().set_exposure(99);
    server.probe.write_property("count", 1);
    EXPECT_NE(read(file)->find("\"exposure\": 12.5,"), std::string::npos);
    server.probe.write_property("exposure", 13);
    EXPECT_NE(read(file)->find("\"exposure\": 13.0,"), std::string::npos);
}

TEST(Settings, TheFileIsLoadedBeforeTheThingsStart) {
    const testing_support::TemporaryFolder folder;
    const auto file = folder.path() / "probe" / "Settings-Probe.json";
    write(file, R"({"count": 7, "label": "loaded", "exposure": 3, "offsets": {"offset": 1, "gain": 2}})");
    testing_support::LogCapture log;
    Server server(folder.path());
    EXPECT_EQ(server.probe.thing().count_at_start, 7);
    EXPECT_EQ(server.probe.thing().label.get(), "loaded");
    EXPECT_EQ(server.probe.thing().exposure(), 3.0);
    EXPECT_EQ(server.probe.thing().offsets.get(), (Offsets{1.0, 2.0}));
    EXPECT_TRUE(log.messages().empty());
    // Loading doesn't rewrite the file.
    EXPECT_EQ(read(file),
              R"({"count": 7, "label": "loaded", "exposure": 3, "offsets": {"offset": 1, "gain": 2}})");
    EXPECT_EQ(server.probe.thing().folder, folder.path() / "probe");
}

TEST(Settings, UnknownKeysAndInvalidValuesOnlyWarn) {
    const testing_support::TemporaryFolder folder;
    const auto file = folder.path() / "probe" / "Settings-Probe.json";
    write(file, R"({"count": -5, "label": 5, "lamp": "on", "exposure": "15"})");
    testing_support::LogCapture log;
    Server server(folder.path());
    const auto messages = log.messages();
    ASSERT_EQ(messages.size(), 3u);
    EXPECT_EQ(
        messages[0],
        "Could not load setting count from settings file because of a validation error: 1 validation error\n"
        "  body: Input should be greater than or equal to 0 [type=greater_than_equal]");
    EXPECT_EQ(
        messages[1],
        "Could not load setting label from settings file because of a validation error: 1 validation error\n"
        "  body: Input should be a valid string [type=string_type]");
    EXPECT_EQ(
        messages[2],
        "An extra key lamp was found in the settings file. It will be deleted the next time settings are "
        "saved.");
    EXPECT_EQ(server.probe.thing().count.get(), 0);
    EXPECT_EQ(server.probe.thing().exposure(), 15.0);  // "15", read as a client's value
    // The next save leaves the extra key out.
    server.probe.write_property("count", 3);
    EXPECT_EQ(read(file)->find("lamp"), std::string::npos);
}

TEST(Settings, AFileThatIsntAnObjectWarnsAndLeavesTheDefaults) {
    const testing_support::TemporaryFolder folder;
    const auto file = folder.path() / "probe" / "Settings-Probe.json";
    const auto path = tw::detail::path_text(file);
    {
        write(file, "[1, 2]");
        testing_support::LogCapture log;
        Server server(folder.path());
        EXPECT_EQ(log.messages(), std::vector<std::string>{"Error loading settings for Probe from " + path +
                                                           ". The file does not contain a Mapping"});
    }
    {
        write(file, R"({"count": )");
        testing_support::LogCapture log;
        Server server(folder.path());
        EXPECT_EQ(log.messages(), std::vector<std::string>{
                                      "Error loading settings for Probe from " + path +
                                      ", could not load a JSON object. Settings for this Thing will be reset "
                                      "to default. (EOF while parsing a value at line 1 column 10)"});
        EXPECT_EQ(server.probe.thing().count.get(), 0);
        EXPECT_EQ(read(file), R"({"count": )");
    }
}

TEST(Settings, AFailedSaveIsAPropertyError) {
    const testing_support::TemporaryFolder folder;
    const auto file = folder.path() / "probe" / "Settings-Probe.json";
    Server server(folder.path());
    std::filesystem::create_directories(file);  // a folder where the file goes
    try {
        server.probe.write_property("count", 3);
        ADD_FAILURE() << "saved over a folder";
    } catch (const tw::PropertyError& error) {
        EXPECT_EQ(error.kind(), tw::PropertyError::Kind::failed);
        EXPECT_TRUE(std::string(error.what())
                        .starts_with("couldn't save the settings of `probe` to " +
                                     tw::detail::path_text(file) + ": "))
            << error.what();
    }
}

TEST(Settings, EveryThingGetsAFolderButOnlyThingsWithSettingsAFile) {
    const testing_support::TemporaryFolder folder;
    auto runtime = tw::Runtime::builder().settings_folder(folder.path()).thing<Plain>("plain").build();
    EXPECT_TRUE(std::filesystem::is_directory(folder.path() / "plain"));
    EXPECT_FALSE(runtime.thing("plain")->settings_file());
}

TEST(Settings, WithoutAFolderSettingsAreOrdinaryProperties) {
    auto runtime = tw::Runtime::builder().thing<Probe>("probe").build();
    runtime.start();
    const auto probe = *runtime.thing_ref<Probe>("probe");
    probe.write_property("count", 3);
    EXPECT_EQ(probe.thing().count.get(), 3);
    EXPECT_FALSE(runtime.thing("probe")->settings_file());
    EXPECT_FALSE(probe.thing().folder);
    runtime.stop();
}
