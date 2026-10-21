// Describes some C++ types and prints, for each, the JSON Schema their codec
// generates (shaped as schemars shapes the Rust type's) next to the TD
// DataSchema it converts to.
//
// Look for:
//
// - optionals (`"type": [..., "null"]` or `anyOf`) turned into `oneOf`, with
//   type-specific keywords such as `minimum` moved into their branch;
// - tuples and pairs (`prefixItems`) turned into an `items` array;
// - the numeric formats (`uint8`, `double`) dropped, but their bounds kept;
// - constraints (`gt`, `le`, ...) written as DataSchema keywords;
// - a recursive struct, which has no DataSchema.

#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <teta/wot.hpp>
#include <utility>
#include <vector>

namespace tw = teta::wot;
namespace td = teta::wot::td;

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

template <>
struct teta::wot::Describe<Readout> {
    static void describe(EnumInfo<Readout>& e) {
        e.description("How the camera reads out the sensor.")
            .value(Readout::global, "global", "All rows at once.")
            .value(Readout::rolling, "rolling", "Row by row.");
    }
};

/// Settings for one acquisition. Fields are found with Boost.PFR; `describe`
/// adds what Rust takes from doc comments and serde attributes.
struct Acquisition {
    double exposure_ms;
    std::optional<Region> roi;
    Readout readout = Readout::rolling;
    std::vector<std::uint8_t> gains;
    std::pair<std::uint8_t, std::uint8_t> binning;
    std::optional<std::string> label;

    static void describe(tw::StructInfo<Acquisition>& s) {
        s.description("Settings for one acquisition.");
        s.field("exposure_ms").description("Exposure time in milliseconds.");
        s.field("roi").description("Region of interest. The whole sensor is used when this is missing.");
        s.field("readout").defaulted();  // `#[serde(default = ...)]`: the default is `rolling`
        s.field("gains").description("Analogue gain for each colour channel.");
        s.field("binning").description("Horizontal and vertical binning.");
        s.field("label").description("Optional label saved with the image.").defaulted();
    }
};

/// A tree of named nodes. It refers to itself, so it has no DataSchema.
struct Node {
    std::string name;
    std::vector<Node> children;
};

template <typename T>
void show(const char* label) {
    std::cout << "=== " << label << "\n--- JSON Schema\n"
              << tw::json::dump_pretty(tw::json_schema_for<T>()) << "\n--- TD DataSchema\n"
              << tw::json::dump_pretty(td::to_json(tw::for_type<T>())) << "\n\n";
}

int main() {
    try {
        show<std::optional<std::uint8_t>>("std::optional<std::uint8_t>");
        show<std::pair<std::uint8_t, std::uint8_t>>("std::pair<std::uint8_t, std::uint8_t>");
        show<Acquisition>("Acquisition");

        // A property's constraints go into its schema.
        auto exposure = tw::for_type<double>();
        td::Constraints{}.gt(0).le(10'000).apply(exposure);
        std::cout << "=== double with gt=0, le=10000\n"
                  << tw::json::dump_pretty(td::to_json(exposure)) << "\n\n";
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }

    // A recursive type can't be inlined; the conversion stops at the
    // recursion limit (99) with an error.
    try {
        tw::for_type<Node>();
    } catch (const td::ConversionError& error) {
        if (error.kind() == td::ConversionError::Kind::recursion_limit) {
            std::cout << "=== Node (recursive)\n" << error.what() << '\n';
            return 0;
        }
        std::cerr << "error: expected a recursion error, got: " << error.what() << '\n';
        return 1;
    }
    std::cerr << "error: expected a recursion error\n";
    return 1;
}
