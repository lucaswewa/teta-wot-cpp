// The C++ codecs against the schema corpus: for every Rust type in
// tests/td/schema_corpus.jsonl, the C++ type standing for it must have the
// same DataSchema, byte for byte.

#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "teta/wot/codec.hpp"

namespace corpus {

namespace tw = teta::wot;
using tw::json::Value;

/// A position in three dimensions.
struct Position {
    std::int64_t x;
    std::int64_t y;
    std::int64_t z = 0;

    static void describe(tw::StructInfo<Position>& s) {
        s.description("A position in three dimensions.");
        s.field("z").defaulted();
    }
};

/// A named position, optionally with a fallback.
struct Waypoint {
    std::string name;
    Position position;
    std::optional<Position> fallback;

    static void describe(tw::StructInfo<Waypoint>& s) {
        s.description("A named position, optionally with a fallback.");
        s.field("fallback").defaulted();
    }
};

enum class Mode { fast, slow };

/// An offset and a gain.
struct Calibration {
    double offset;
    double gain;

    static void describe(tw::StructInfo<Calibration>& s) { s.description("An offset and a gain."); }
};

/// A struct whose fields are documented.
struct Documented {
    std::uint32_t count;
    std::optional<std::string> label;
    std::int64_t level = 3;
    std::vector<bool> flags;

    static void describe(tw::StructInfo<Documented>& s) {
        s.description("A struct whose fields are documented.").deny_unknown_fields();
        s.field("count").description("How many.");
        s.field("label").description("A label, if any.");
        s.field("level").description("Defaults to three.").defaulted();
        s.field("flags").defaulted();
    }
};

/// Nested structs, to exercise references inside references.
struct Route {
    Waypoint start;
    std::vector<Waypoint> stops;
    std::optional<Calibration> calibration;
    Mode mode;

    static void describe(tw::StructInfo<Route>& s) {
        s.description("Nested structs, to exercise references inside references.");
    }
};

struct Undocumented {
    float a;
    std::optional<std::uint8_t> b;
};

/// A rectangular region of the sensor, in pixels.
struct Region {
    std::uint32_t x;
    std::uint32_t y;
    std::uint32_t width;
    std::uint32_t height;
    static void describe(tw::StructInfo<Region>& s) {
        s.description("A rectangular region of the sensor, in pixels.");
    }
};

/// How the camera reads out the sensor.
enum class Readout { global, rolling };

/// Settings for one acquisition.
struct Acquisition {
    double exposure_ms;
    std::optional<Region> roi;
    Readout readout = Readout::rolling;
    std::vector<std::uint8_t> gains;
    std::pair<std::uint8_t, std::uint8_t> binning;
    std::optional<std::string> label;

    static void describe(tw::StructInfo<Acquisition>& s) {
        s.description("Settings for one acquisition (the schema-to-dataschema example's).");
        s.field("exposure_ms").description("Exposure time in milliseconds.");
        s.field("roi").description("Region of interest. The whole sensor is used when this is missing.");
        s.field("readout").defaulted();
        s.field("gains").description("Analogue gain for each colour channel.");
        s.field("binning").description("Horizontal and vertical binning.");
        s.field("label").description("Optional label saved with the image.").defaulted();
    }
};

/// A tree of named nodes: it refers to itself, so it has no DataSchema.
struct Node {
    std::string name;
    std::vector<Node> children;
};

/// mything's input of an action with only `**kwargs`: a type with a
/// hand-written codec, as schemars derives it from attributes the C++
/// codecs have no equivalent for.
struct EmptyInput {};

}  // namespace corpus

template <>
struct teta::wot::Describe<corpus::Mode> {
    static void describe(EnumInfo<corpus::Mode>& e) {
        e.value(corpus::Mode::fast, "fast").value(corpus::Mode::slow, "slow");
    }
};

template <>
struct teta::wot::Describe<corpus::Readout> {
    static void describe(EnumInfo<corpus::Readout>& e) {
        e.description("How the camera reads out the sensor.")
            .value(corpus::Readout::global, "global", "All rows at once.")
            .value(corpus::Readout::rolling, "rolling", "Row by row.");
    }
};

template <>
struct teta::wot::Codec<corpus::EmptyInput> {
    static constexpr bool referenced = true;
    static json::Value json_schema() {
        return {
            {"description",
             "The input of an action that has no required parameters (mything's).\n\nThis may be either a "
             "dictionary or ``None``."},
            {"anyOf",
             json::Value::array({{{"description", "A model representing an object with no required keys."},
                                  {"type", "object"},
                                  {"properties", json::Value::object()},
                                  {"additionalProperties", true}},
                                 {{"type", "null"}}})}};
    }
    static json::Value encode(const corpus::EmptyInput&) { return nullptr; }
};

namespace {

using namespace corpus;
namespace tw = teta::wot;

template <typename T>
std::string data_schema_text() {
    return tw::json::dump(tw::td::to_json(tw::for_type<T>()));
}

#define CASE(rust, ...) \
    { rust, &data_schema_text<__VA_ARGS__> }

const std::map<std::string, std::function<std::string()>> cases = {
    CASE("bool", bool),
    CASE("i8", std::int8_t),
    CASE("i16", std::int16_t),
    CASE("i32", std::int32_t),
    CASE("i64", std::int64_t),
    CASE("u8", std::uint8_t),
    CASE("u16", std::uint16_t),
    CASE("u32", std::uint32_t),
    CASE("u64", std::uint64_t),
    CASE("f32", float),
    CASE("f64", double),
    CASE("String", std::string),
    CASE("()", std::monostate),
    CASE("Value", tw::json::Value),
    CASE("Option<bool>", std::optional<bool>),
    CASE("Option<i64>", std::optional<std::int64_t>),
    CASE("Option<u8>", std::optional<std::uint8_t>),
    CASE("Option<f64>", std::optional<double>),
    CASE("Option<String>", std::optional<std::string>),
    CASE("Option<Vec<String>>", std::optional<std::vector<std::string>>),
    CASE("Option<Position>", std::optional<Position>),
    CASE("Option<Mode>", std::optional<Mode>),
    CASE("Option<Value>", std::optional<tw::json::Value>),
    CASE("Option<Option<i64>>", std::optional<std::optional<std::int64_t>>),
    CASE("Vec<i64>", std::vector<std::int64_t>),
    CASE("Vec<String>", std::vector<std::string>),
    CASE("Vec<Position>", std::vector<Position>),
    CASE("Vec<Vec<f64>>", std::vector<std::vector<double>>),
    CASE("Vec<Option<i64>>", std::vector<std::optional<std::int64_t>>),
    CASE("(i64, String)", std::tuple<std::int64_t, std::string>),
    CASE("(f64, f64, f64)", std::tuple<double, double, double>),
    CASE("[i64; 3]", std::array<std::int64_t, 3>),
    CASE("(bool,)", std::tuple<bool>),
    CASE("BTreeMap<String, f64>", std::map<std::string, double>),
    CASE("HashMap<String, i64>", std::unordered_map<std::string, std::int64_t>),
    CASE("IndexMap<String, String>", tw::OrderedMap<std::string>),
    CASE("IndexMap<String, Option<String>>", tw::OrderedMap<std::optional<std::string>>),
    CASE("IndexMap<String, Position>", tw::OrderedMap<Position>),
    CASE("Map<String, Value>", tw::OrderedMap<tw::json::Value>),
    CASE("Position", Position),
    CASE("Waypoint", Waypoint),
    CASE("Mode", Mode),
    CASE("Calibration", Calibration),
    CASE("Documented", Documented),
    CASE("Route", Route),
    CASE("Undocumented", Undocumented),
    CASE("EmptyInput", EmptyInput),
    CASE("teta_wot::NoInput", tw::NoInput),
    CASE("Region", Region),
    CASE("Readout", Readout),
    CASE("Option<Readout>", std::optional<Readout>),
    CASE("Acquisition", Acquisition),
    CASE("(u8, u8)", std::pair<std::uint8_t, std::uint8_t>),
};

// Rust types with no C++ counterpart.
const std::vector<std::string> not_in_cpp = {
    "char",  // a Unicode scalar; C++'s `char` is a byte
};

TEST(CodecCorpus, EveryRustTypeHasTheSameDataSchema) {
    std::ifstream file(TETA_WOT_SCHEMA_CORPUS, std::ios::binary);
    ASSERT_TRUE(file);
    std::string line;
    int checked = 0;
    while (std::getline(file, line)) {
        const auto entry = Value::parse(line);
        const std::string type = entry["type"];
        if (std::ranges::find(not_in_cpp, type) != not_in_cpp.end()) {
            continue;
        }
        const auto it = cases.find(type);
        ASSERT_NE(it, cases.end()) << "no C++ type for " << type;
        EXPECT_EQ(it->second(), entry["data_schema"].get<std::string>()) << type;
        ++checked;
    }
    EXPECT_EQ(checked, static_cast<int>(cases.size()));
}

// A recursive type can't be inlined: the conversion stops at the recursion
// limit, as teta-wot's does.
TEST(Codec, ARecursiveStructReachesTheRecursionLimit) {
    try {
        tw::for_type<Node>();
        FAIL() << "no error";
    } catch (const tw::td::ConversionError& error) {
        EXPECT_EQ(error.kind(), tw::td::ConversionError::Kind::recursion_limit);
    }
}

TEST(Codec, EncodesAsSerde) {
    EXPECT_EQ(tw::json::dump(tw::encode(Position{1, 2, 0})), R"({"x":1,"y":2,"z":0})");
    EXPECT_EQ(tw::json::dump(tw::encode(std::tuple<std::int64_t, std::string>{1, "a"})), R"([1,"a"])");
    EXPECT_EQ(tw::json::dump(tw::encode(std::optional<std::int64_t>{})), "null");
    EXPECT_EQ(tw::json::dump(tw::encode(Mode::slow)), R"("slow")");
    EXPECT_EQ(tw::json::dump(tw::encode(0.1f)), "0.1");
    EXPECT_EQ(tw::json::dump(tw::encode(Calibration{0.0, 1.0})), R"({"offset":0.0,"gain":1.0})");
    EXPECT_EQ(tw::json::dump(tw::encode(std::map<std::string, double>{})), "{}");
    EXPECT_EQ(tw::json::dump(tw::encode(tw::NoInput{})), "null");
}

}  // namespace
