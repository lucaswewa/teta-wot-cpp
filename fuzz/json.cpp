// JSON, as serde_json reads and writes it: `parse` refuses a text with
// ParseError or returns a value; what `dump` writes parses back to the same
// value, but for floats, which serde_json's parser (without
// `float_roundtrip`, as ported) rounds within an ULP or two rather than
// exactly; and the compact and the indented texts parse to the same value.

#include "teta/wot/json.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <string>

#include "fuzz.hpp"

namespace json = teta::wot::json;

namespace {

// How many doubles apart `a` and `b` are (both finite).
std::uint64_t ulps(double a, double b) {
    if (a == b) {
        return 0;
    }
    if (std::signbit(a) != std::signbit(b)) {
        return UINT64_MAX;
    }
    const auto x = std::bit_cast<std::uint64_t>(std::fabs(a));
    const auto y = std::bit_cast<std::uint64_t>(std::fabs(b));
    return x > y ? x - y : y - x;
}

// The same value, floats within `tolerance` ULPs.
bool close(const json::Value& a, const json::Value& b, std::uint64_t tolerance) {
    if (a.is_number_float() && b.is_number_float()) {
        return ulps(a.get<double>(), b.get<double>()) <= tolerance;
    }
    if (a.type() != b.type() || a.size() != b.size()) {
        return false;
    }
    if (a.is_array()) {
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (!close(a[i], b[i], tolerance)) {
                return false;
            }
        }
        return true;
    }
    if (a.is_object()) {
        for (auto i = a.begin(), j = b.begin(); i != a.end(); ++i, ++j) {
            if (i.key() != j.key() || !close(i.value(), j.value(), tolerance)) {
                return false;
            }
        }
        return true;
    }
    return json::dump(a) == json::dump(b);
}

}  // namespace

void fuzz::one_input(std::string_view input) {
    json::Value value;
    try {
        value = json::parse(input);
    } catch (const json::ParseError&) {
        return;
    }
    const auto text = json::dump(value);
    json::Value again;
    try {
        again = json::parse(text);
    } catch (const json::ParseError& error) {
        throw fuzz::Failure("parse refuses what dump wrote, " + text + ": " + error.what());
    }
    fuzz::check(close(again, value, 2),
                "dump's text parses to another value, " + json::dump(again) + ": " + text);
    const auto pretty = json::dump_pretty(value, 4);
    fuzz::check(json::dump(json::parse(pretty)) == json::dump(again),
                "dump_pretty's text is another value: " + pretty);
}
