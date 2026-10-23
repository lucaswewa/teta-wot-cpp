#include "teta/wot/validate.hpp"

#include <re2/re2.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <system_error>

namespace teta::wot {

using td::ArrayItems;
using td::DataSchema;
using td::DataType;

// ---- Text as Rust sees it ---------------------------------------------------------

namespace {

// The Unicode code point starting at `text[i]`, and its length in bytes.
std::pair<char32_t, std::size_t> decode(std::string_view text, std::size_t i) {
    const auto b = static_cast<char32_t>(static_cast<unsigned char>(text[i]));
    auto continuation = [&](std::size_t k) {
        return i + k < text.size() ? static_cast<char32_t>(static_cast<unsigned char>(text[i + k])) & 0x3Fu
                                   : 0u;
    };
    if (b < 0x80) {
        return {b, 1};
    }
    if (b >> 5 == 0x6) {
        return {((b & 0x1Fu) << 6) | continuation(1), 2};
    }
    if (b >> 4 == 0xE) {
        return {((b & 0x0Fu) << 12) | (continuation(1) << 6) | continuation(2), 3};
    }
    return {((b & 0x07u) << 18) | (continuation(1) << 12) | (continuation(2) << 6) | continuation(3), 4};
}

// Rust's `char::is_whitespace`: the Unicode White_Space property.
bool is_whitespace(char32_t c) {
    return (c >= 0x09 && c <= 0x0D) || c == 0x20 || c == 0x85 || c == 0xA0 || c == 0x1680 ||
           (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F ||
           c == 0x3000;
}

// Rust's `str::trim`.
std::string_view trim(std::string_view text) {
    std::size_t start = 0;
    while (start < text.size()) {
        const auto [c, n] = decode(text, start);
        if (!is_whitespace(c)) {
            break;
        }
        start += n;
    }
    std::size_t end = text.size();
    while (end > start) {
        // Step back to the start of the last code point.
        std::size_t begin = end - 1;
        while (begin > start && (static_cast<unsigned char>(text[begin]) & 0xC0) == 0x80) {
            --begin;
        }
        if (!is_whitespace(decode(text, begin).first)) {
            break;
        }
        end = begin;
    }
    return text.substr(start, end - start);
}

// Rust's `chars().count()`: code points, not bytes.
std::uint64_t char_count(std::string_view text) {
    std::uint64_t count = 0;
    for (const char c : text) {
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) {
            ++count;
        }
    }
    return count;
}

bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

// The shortest decimal digits of a positive finite double, and the decimal
// exponent of the first digit.
std::pair<std::string, int> shortest_digits(double value) {
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof buffer, value, std::chars_format::scientific);
    std::string digits;
    const char* p = buffer;
    for (; p != result.ptr && *p != 'e'; ++p) {
        if (*p != '.') {
            digits += *p;
        }
    }
    int exponent = 0;
    std::from_chars(p + (p[1] == '+' ? 2 : 1), result.ptr, exponent);
    return {digits, exponent};
}

// Rust's `str::parse::<f64>`: an optional sign, then `inf`, `infinity` or
// `nan` (any case), or a decimal with an optional exponent; out-of-range
// values become infinities or zeros, as Rust's do.
std::optional<double> parse_rust_f64(std::string_view text) {
    bool negative = false;
    std::string_view rest = text;
    if (!rest.empty() && (rest[0] == '+' || rest[0] == '-')) {
        negative = rest[0] == '-';
        rest.remove_prefix(1);
    }
    std::string lower;
    for (const char c : rest) {
        lower += static_cast<char>((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c);
    }
    if (lower == "inf" || lower == "infinity") {
        return negative ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
    }
    if (lower == "nan") {
        return std::numeric_limits<double>::quiet_NaN();
    }
    // digits [. digits] [e [sign] digits], with at least one mantissa digit.
    std::size_t i = 0;
    std::size_t mantissa_digits = 0;
    bool seen_significant = false;
    int integer_digits = 0;
    while (i < rest.size() && is_digit(rest[i])) {
        if (rest[i] != '0' || seen_significant) {
            seen_significant = true;
            ++integer_digits;
        }
        ++i;
        ++mantissa_digits;
    }
    int fraction_zeros = 0;
    if (i < rest.size() && rest[i] == '.') {
        ++i;
        while (i < rest.size() && is_digit(rest[i])) {
            if (!seen_significant) {
                if (rest[i] == '0') {
                    ++fraction_zeros;
                } else {
                    seen_significant = true;
                }
            }
            ++i;
            ++mantissa_digits;
        }
    }
    if (mantissa_digits == 0) {
        return std::nullopt;
    }
    long long exponent = 0;
    if (i < rest.size() && (rest[i] == 'e' || rest[i] == 'E')) {
        ++i;
        bool exponent_negative = false;
        if (i < rest.size() && (rest[i] == '+' || rest[i] == '-')) {
            exponent_negative = rest[i] == '-';
            ++i;
        }
        if (i == rest.size()) {
            return std::nullopt;
        }
        while (i < rest.size() && is_digit(rest[i])) {
            exponent = std::min<long long>(exponent * 10 + (rest[i] - '0'), 100000000);
            ++i;
        }
        if (exponent_negative) {
            exponent = -exponent;
        }
    }
    if (i != rest.size()) {
        return std::nullopt;
    }
    if (!seen_significant) {
        return negative ? -0.0 : 0.0;
    }
    double value = 0.0;
    const auto result = std::from_chars(rest.data(), rest.data() + rest.size(), value);
    if (result.ec == std::errc::result_out_of_range) {
        // Too large (an infinity) or too small (a zero), by the decimal
        // exponent of the first significant digit.
        const long long magnitude =
            exponent + (integer_digits > 0 ? integer_digits - 1 : -(fraction_zeros + 1));
        value = magnitude > 0 ? std::numeric_limits<double>::infinity() : 0.0;
    } else if (result.ec != std::errc{} || result.ptr != rest.data() + rest.size()) {
        return std::nullopt;
    }
    return negative ? -value : value;
}

// Rust's `str::parse::<i64>`, then `u64`: an optional sign and decimal digits.
std::optional<json::Value> parse_rust_integer(std::string_view text) {
    std::string_view digits = text;
    bool negative = false;
    if (!digits.empty() && (digits[0] == '+' || digits[0] == '-')) {
        negative = digits[0] == '-';
        digits.remove_prefix(1);
    }
    if (digits.empty() || !std::ranges::all_of(digits, is_digit)) {
        return std::nullopt;
    }
    if (negative) {
        std::int64_t value = 0;
        std::string signed_text = "-" + std::string(digits);
        const auto result =
            std::from_chars(signed_text.data(), signed_text.data() + signed_text.size(), value);
        if (result.ec != std::errc{}) {
            return std::nullopt;
        }
        return json::Value(value);
    }
    std::uint64_t value = 0;
    const auto result = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (result.ec != std::errc{}) {
        return std::nullopt;
    }
    if (value <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return json::Value(static_cast<std::int64_t>(value));
    }
    return json::Value(value);
}

// serde's `Number::as_f64`.
double as_f64(const json::Value& n) {
    return n.get<double>();
}

// serde's `is_i64() || is_u64()`.
bool is_integer(const json::Value& n) {
    return n.is_number_integer();
}

// A bound or step as pydantic-core writes it (Rust's `Display`).
std::string display_number(const json::Value& n) {
    if (n.is_number_unsigned()) {
        return std::to_string(n.get<std::uint64_t>());
    }
    if (n.is_number_integer()) {
        return std::to_string(n.get<std::int64_t>());
    }
    return rust_display(n.get<double>());
}

const char* plural(std::uint64_t n) {
    return n == 1 ? "" : "s";
}

}  // namespace

std::string rust_display(double value) {
    if (std::isnan(value)) {
        return "NaN";
    }
    std::string out;
    if (std::signbit(value)) {
        out += '-';
        value = -value;
    }
    if (std::isinf(value)) {
        return out + "inf";
    }
    if (value == 0.0) {
        return out + "0";
    }
    const auto [digits, exponent] = shortest_digits(value);
    const int length = static_cast<int>(digits.size());
    if (exponent >= 0) {
        const int integer_digits = exponent + 1;
        if (length <= integer_digits) {
            out += digits;
            out.append(static_cast<std::size_t>(integer_digits - length), '0');
        } else {
            out += digits.substr(0, static_cast<std::size_t>(integer_digits));
            out += '.';
            out += digits.substr(static_cast<std::size_t>(integer_digits));
        }
    } else {
        out += "0.";
        out.append(static_cast<std::size_t>(-exponent - 1), '0');
        out += digits;
    }
    return out;
}

// ---- Locations and errors -----------------------------------------------------------

std::string LocItem::to_string() const {
    if (const auto* key = std::get_if<std::string>(&item_)) {
        return *key;
    }
    return std::to_string(std::get<std::size_t>(item_));
}

json::Value LocItem::to_json() const {
    if (const auto* key = std::get_if<std::string>(&item_)) {
        return *key;
    }
    return static_cast<std::uint64_t>(std::get<std::size_t>(item_));
}

json::Value to_json(const ValidationIssue& issue) {
    json::Value out = json::Value::object();
    out["type"] = issue.type;
    json::Value loc = json::Value::array();
    for (const auto& item : issue.loc) {
        loc.push_back(item.to_json());
    }
    out["loc"] = std::move(loc);
    out["msg"] = issue.msg;
    out["input"] = issue.input;
    if (issue.ctx) {
        out["ctx"] = *issue.ctx;
    }
    return out;
}

ValidationError::ValidationError(std::vector<ValidationIssue> issues) : issues_(std::move(issues)) {
    const auto n = issues_.size();
    text_ = std::to_string(n) + " validation error" + (n == 1 ? "" : "s");
    for (const auto& issue : issues_) {
        std::string at;
        for (std::size_t i = 0; i < issue.loc.size(); ++i) {
            at += (i ? "." : "") + issue.loc[i].to_string();
        }
        text_ += "\n  " + (at.empty() ? std::string() : at + ": ") + issue.msg + " [type=" + issue.type + "]";
    }
}

ValidationError ValidationError::single(ValidationIssue issue) {
    return ValidationError({std::move(issue)});
}

ValidationError ValidationError::missing(Loc loc, json::Value input) {
    return single({"missing", std::move(loc), "Field required", std::move(input), std::nullopt});
}

ValidationError ValidationError::value_error(Loc loc, std::string_view message, json::Value input) {
    json::Value ctx = {{"error", json::Value::object()}};
    return single(
        {"value_error", std::move(loc), "Value error, " + std::string(message), std::move(input), ctx});
}

ValidationError ValidationError::decode_error(Loc loc, std::string_view message, json::Value input) {
    return single({"value_error", std::move(loc), "Value error, " + std::string(message), std::move(input),
                   std::nullopt});
}

ValidationError ValidationError::value_error_at(
    Loc loc, const std::vector<std::variant<std::string, std::size_t>>& path, std::string_view message,
    const json::Value& input) {
    static const json::Value null;
    const json::Value* at = &input;
    for (const auto& step : path) {
        if (const auto* key = std::get_if<std::string>(&step)) {
            const auto it = at->is_object() ? at->find(*key) : at->end();
            at = at->is_object() && it != at->end() ? &*it : &null;
            loc.emplace_back(*key);
        } else {
            const auto index = std::get<std::size_t>(step);
            at = at->is_array() && index < at->size() ? &(*at)[index] : &null;
            loc.emplace_back(index);
        }
    }
    return value_error(std::move(loc), message, *at);
}

// ---- Coercions ----------------------------------------------------------------------

namespace coercion {

namespace {

constexpr Failure int_type{"int_type", "Input should be a valid integer"};
constexpr Failure int_parsing{"int_parsing",
                              "Input should be a valid integer, unable to parse string as an integer"};
constexpr Failure int_from_float{"int_from_float",
                                 "Input should be a valid integer, got a number with a fractional part"};
constexpr Failure float_type{"float_type", "Input should be a valid number"};
constexpr Failure float_parsing{"float_parsing",
                                "Input should be a valid number, unable to parse string as a number"};
constexpr Failure int_parsing_size{"int_parsing_size",
                                   "Unable to parse input string as an integer, exceeded maximum size"};
constexpr Failure bool_type{"bool_type", "Input should be a valid boolean"};
constexpr Failure bool_parsing{"bool_parsing", "Input should be a valid boolean, unable to interpret input"};

Result float_to_int(double f) {
    if (!std::isfinite(f)) {
        return int_parsing;
    }
    if (f != std::trunc(f)) {
        return int_from_float;
    }
    if (std::fabs(f) < 9.223'372'036'854'775e18) {
        return json::Value(static_cast<std::int64_t>(f));
    }
    return int_parsing_size;
}

// Digits, possibly with single `_` separators between them.
bool valid_digits(std::string_view digits) {
    return !digits.empty() && std::ranges::all_of(digits, [](char c) { return is_digit(c) || c == '_'; }) &&
           digits.front() != '_' && digits.back() != '_' && digits.find("__") == std::string_view::npos;
}

std::optional<json::Value> parse_int(std::string_view s) {
    const std::string_view t = trim(s);
    std::string_view whole = t;
    std::string_view fraction = "0";
    if (const auto dot = t.find('.'); dot != std::string_view::npos) {
        whole = t.substr(0, dot);
        fraction = t.substr(dot + 1);
    }
    if (fraction.empty() || !std::ranges::all_of(fraction, [](char c) { return c == '0'; })) {
        return std::nullopt;
    }
    std::string_view digits = whole;
    if (!digits.empty() && (digits[0] == '+' || digits[0] == '-')) {
        digits.remove_prefix(1);
    }
    if (!valid_digits(digits)) {
        return std::nullopt;
    }
    std::string cleaned;
    for (const char c : whole) {
        if (c != '_') {
            cleaned += c;
        }
    }
    return parse_rust_integer(cleaned);
}

std::optional<double> parse_float(std::string_view s) {
    const std::string_view t = trim(s);
    // `_` only between ASCII digits (`t` is examined by code point; `_` and
    // digits are ASCII, so bytes will do).
    for (std::size_t i = 0; i < t.size(); ++i) {
        if (t[i] == '_' && !(i > 0 && is_digit(t[i - 1]) && i + 1 < t.size() && is_digit(t[i + 1]))) {
            return std::nullopt;
        }
    }
    std::string cleaned;
    for (const char c : t) {
        if (c != '_') {
            cleaned += c;
        }
    }
    return parse_rust_f64(cleaned);
}

std::string ascii_lowercase(std::string_view s) {
    std::string out(s);
    for (auto& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

}  // namespace

Result integer(const json::Value& value) {
    if (value.is_number()) {
        if (is_integer(value)) {
            return value;
        }
        return float_to_int(value.get<double>());
    }
    if (value.is_boolean()) {
        return json::Value(static_cast<std::int64_t>(value.get<bool>() ? 1 : 0));
    }
    if (value.is_string()) {
        if (auto parsed = parse_int(value.get_ref<const std::string&>())) {
            return std::move(*parsed);
        }
        return int_parsing;
    }
    return int_type;
}

Result number(const json::Value& value) {
    double f = 0.0;
    if (value.is_number()) {
        f = as_f64(value);
    } else if (value.is_boolean()) {
        f = value.get<bool>() ? 1.0 : 0.0;
    } else if (value.is_string()) {
        const auto parsed = parse_float(value.get_ref<const std::string&>());
        if (!parsed) {
            return float_parsing;
        }
        f = *parsed;
    } else {
        return float_type;
    }
    if (!std::isfinite(f)) {
        return float_parsing;
    }
    return json::Value(f);
}

Result boolean(const json::Value& value) {
    if (value.is_boolean()) {
        return value;
    }
    if (value.is_number_unsigned()) {
        const auto u = value.get<std::uint64_t>();
        if (u == 0 || u == 1) {
            return json::Value(u == 1);
        }
        return bool_parsing;
    }
    if (value.is_number_integer()) {
        const auto i = value.get<std::int64_t>();
        if (i == 0 || i == 1) {
            return json::Value(i == 1);
        }
        return bool_parsing;
    }
    if (value.is_number_float()) {
        const double f = value.get<double>();
        if (f == 0.0 || f == 1.0) {
            return json::Value(f == 1.0);
        }
        return bool_type;
    }
    if (value.is_string()) {
        const auto s = ascii_lowercase(value.get_ref<const std::string&>());
        if (s == "0" || s == "off" || s == "f" || s == "false" || s == "n" || s == "no") {
            return json::Value(false);
        }
        if (s == "1" || s == "on" || s == "t" || s == "true" || s == "y" || s == "yes") {
            return json::Value(true);
        }
        return bool_parsing;
    }
    return bool_type;
}

Result string(const json::Value& value) {
    if (value.is_string()) {
        return value;
    }
    return Failure{"string_type", "Input should be a valid string"};
}

}  // namespace coercion

// ---- The validator ------------------------------------------------------------------

namespace {

using Patterns = std::map<std::string, std::shared_ptr<re2::RE2>>;

// Rust's `regex` makes `\d`, `\w` and `\s` Unicode-aware; RE2's are ASCII.
// Rewrites them as Unicode classes (Decimal_Number; letters, marks, digits
// and connectors, close to Rust's word characters; White_Space), inside and
// outside bracketed classes. `\W` and `\S` inside a bracketed class, `\b`,
// and Rust-only syntax (nested classes, set operations) are left as they are.
std::string rust_regex_for_re2(std::string_view pattern) {
    constexpr std::string_view digit = "\\p{Nd}";
    constexpr std::string_view word = "\\p{L}\\p{M}\\p{Nd}\\p{Pc}";
    constexpr std::string_view space = "\\t\\n\\x0B\\f\\r\\x{85}\\p{Z}";
    std::string out;
    bool in_class = false;
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        const char c = pattern[i];
        if (c == '\\' && i + 1 < pattern.size()) {
            const char e = pattern[i + 1];
            ++i;
            switch (e) {
                case 'd':
                    out += digit;
                    break;
                case 'D':
                    out += "\\P{Nd}";
                    break;
                case 'w':
                    out += in_class ? std::string(word) : "[" + std::string(word) + "]";
                    break;
                case 'W':
                    out += in_class ? std::string("\\W") : "[^" + std::string(word) + "]";
                    break;
                case 's':
                    out += in_class ? std::string(space) : "[" + std::string(space) + "]";
                    break;
                case 'S':
                    out += in_class ? std::string("\\S") : "[^" + std::string(space) + "]";
                    break;
                default:
                    out += c;
                    out += e;
            }
            continue;
        }
        if (!in_class && c == '[') {
            in_class = true;
            out += c;
            // A `]` or `^]` right after the bracket is literal.
            if (i + 1 < pattern.size() && pattern[i + 1] == '^') {
                out += pattern[++i];
            }
            if (i + 1 < pattern.size() && pattern[i + 1] == ']') {
                out += pattern[++i];
            }
            continue;
        }
        if (in_class && c == ']') {
            in_class = false;
        }
        out += c;
    }
    return out;
}

void collect_patterns(const DataSchema& schema, Patterns& out) {
    if (schema.pattern && !out.contains(*schema.pattern)) {
        auto regex = std::make_shared<re2::RE2>(rust_regex_for_re2(*schema.pattern), re2::RE2::Quiet);
        if (!regex->ok()) {
            throw InvalidPattern("invalid pattern `" + *schema.pattern + "`: " + regex->error());
        }
        out.emplace(*schema.pattern, std::move(regex));
    }
    if (schema.one_of) {
        for (const auto& branch : *schema.one_of) {
            collect_patterns(branch, out);
        }
    }
    if (schema.items) {
        for (const auto& item : schema.items->schemas) {
            collect_patterns(item, out);
        }
    }
    if (schema.properties) {
        for (const auto& [name, property] : *schema.properties) {
            collect_patterns(property, out);
        }
    }
}

// Whether a schema is a number, a union of numbers, or arrays of those
// nested to any depth (the innermost may allow anything).
bool nested_numbers_schema(const DataSchema& schema) {
    const bool plain = !schema.enumeration && !schema.constant;
    if (schema.one_of && !schema.data_type) {
        return plain && std::ranges::all_of(*schema.one_of, nested_numbers_schema);
    }
    if (schema.one_of) {
        return false;
    }
    if (schema.data_type == DataType::integer || schema.data_type == DataType::number) {
        return plain;
    }
    if (schema.data_type == DataType::array) {
        if (schema.items && !schema.items->tuple) {
            const auto& items = schema.items->schemas.at(0);
            return nested_numbers_schema(items) || (!items.data_type && !items.one_of);
        }
        return false;
    }
    return false;
}

// Whether a value is a number or lists of numbers, of any shape.
bool nested_numbers(const json::Value& value) {
    if (value.is_number()) {
        return true;
    }
    if (value.is_array()) {
        return std::ranges::all_of(value, nested_numbers);
    }
    return false;
}

// Whether `value` already has `schema`'s JSON type (no coercion needed).
bool exact_type(const DataSchema& schema, const json::Value& value) {
    if (!schema.data_type) {
        return true;
    }
    switch (*schema.data_type) {
        case DataType::null:
            return value.is_null();
        case DataType::boolean:
            return value.is_boolean();
        case DataType::string:
            return value.is_string();
        case DataType::array:
            return value.is_array();
        case DataType::object:
            return value.is_object();
        case DataType::integer:
            return value.is_number_integer();
        case DataType::number:
            return value.is_number();
    }
    return false;
}

// The tag pydantic puts in the location of a union branch's errors.
std::string branch_tag(const DataSchema& schema, std::size_t index) {
    if (!schema.data_type) {
        return "branch-" + std::to_string(index);
    }
    switch (*schema.data_type) {
        case DataType::integer:
            return "int";
        case DataType::number:
            return "float";
        case DataType::string:
            return "str";
        case DataType::boolean:
            return "bool";
        case DataType::null:
            return "none";
        case DataType::array:
            return "list";
        case DataType::object:
            return schema.title.value_or("dict");
    }
    return "?";
}

// serde_json's `Value` equality: integers and floats are different kinds
// (`1` isn't `1.0`), and objects compare whatever their key order.
bool serde_equal(const json::Value& a, const json::Value& b) {
    if (a.is_number_integer() && b.is_number_integer()) {
        if (a.is_number_unsigned() || b.is_number_unsigned()) {
            const bool a_negative = !a.is_number_unsigned() && a.get<std::int64_t>() < 0;
            const bool b_negative = !b.is_number_unsigned() && b.get<std::int64_t>() < 0;
            return !a_negative && !b_negative && a.get<std::uint64_t>() == b.get<std::uint64_t>();
        }
        return a.get<std::int64_t>() == b.get<std::int64_t>();
    }
    if (a.is_number_float() || b.is_number_float()) {
        return a.is_number_float() && b.is_number_float() && a.get<double>() == b.get<double>();
    }
    if (a.type() != b.type()) {
        return false;
    }
    if (a.is_array()) {
        if (a.size() != b.size()) {
            return false;
        }
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (!serde_equal(a[i], b[i])) {
                return false;
            }
        }
        return true;
    }
    if (a.is_object()) {
        if (a.size() != b.size()) {
            return false;
        }
        for (const auto& [key, value] : a.items()) {
            const auto it = b.find(key);
            if (it == b.end() || !serde_equal(value, *it)) {
                return false;
            }
        }
        return true;
    }
    return a == b;
}

// Whether `value` selects the literal `candidate`, by Python's equality:
// numbers compare by value whatever their kind, and `true`/`false` equal
// `1`/`0`. A string literal is only matched by the same string.
bool literal_matches(const json::Value& candidate, const json::Value& value) {
    auto numeric = [](const json::Value& v) -> std::optional<double> {
        if (v.is_number()) {
            return as_f64(v);
        }
        if (v.is_boolean()) {
            return v.get<bool>() ? 1.0 : 0.0;
        }
        return std::nullopt;
    };
    const auto a = numeric(candidate);
    const auto b = numeric(value);
    if (a && b) {
        return *a == *b;
    }
    return serde_equal(candidate, value);
}

std::string python_repr(const json::Value& value) {
    if (value.is_string()) {
        return "'" + value.get<std::string>() + "'";
    }
    if (value.is_boolean()) {
        return value.get<bool>() ? "True" : "False";
    }
    if (value.is_null()) {
        return "None";
    }
    if (value.is_number()) {
        return display_number(value);
    }
    return json::dump(value);
}

// Values as pydantic lists expected literals: `'a', 'b' or 'c'`.
std::string python_list(const std::vector<json::Value>& values) {
    std::string out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out += i + 1 == values.size() ? " or " : ", ";
        }
        out += python_repr(values[i]);
    }
    return out;
}

bool is_multiple(double x, double step) {
    const double ratio = x / step;
    return std::fabs(ratio - std::round(ratio)) < 1e-9;
}

class Run {
  public:
    Run(const Patterns& patterns, Loc loc) : patterns_(patterns), loc_(std::move(loc)) {}

    std::vector<ValidationIssue>& issues() { return issues_; }

    // Validates `value` against `schema`, recording errors and returning the
    // coerced value if it is valid.
    std::optional<json::Value> check(const DataSchema& schema, const json::Value& value) {
        if (schema.one_of) {
            return union_of(*schema.one_of, value);
        }
        if (schema.enumeration) {
            return literal(*schema.enumeration, value);
        }
        if (schema.constant) {
            return literal({*schema.constant}, value);
        }
        if (!schema.data_type) {
            return value;
        }
        switch (*schema.data_type) {
            case DataType::null:
                if (value.is_null()) {
                    return json::Value(nullptr);
                }
                return fail("none_required", "Input should be None", value);
            case DataType::boolean:
                return coerce(coercion::boolean, value);
            case DataType::integer: {
                auto v = coerce(coercion::integer, value);
                if (!v) {
                    return std::nullopt;
                }
                return numeric_bounds(schema, std::move(*v), value);
            }
            case DataType::number: {
                auto v = coerce(coercion::number, value);
                if (!v) {
                    return std::nullopt;
                }
                return numeric_bounds(schema, std::move(*v), value);
            }
            case DataType::string: {
                auto v = coerce(coercion::string, value);
                if (!v) {
                    return std::nullopt;
                }
                return string_bounds(schema, std::move(*v));
            }
            case DataType::array:
                return array(schema, value);
            case DataType::object:
                return object(schema, value);
        }
        return std::nullopt;
    }

  private:
    std::optional<json::Value> fail(const std::string& type, const std::string& msg, const json::Value& input,
                                    std::optional<json::Value> ctx = std::nullopt) {
        issues_.push_back({type, loc_, msg, input, std::move(ctx)});
        return std::nullopt;
    }

    template <typename F>
    auto at(LocItem item, F f) {
        loc_.push_back(std::move(item));
        auto out = f();
        loc_.pop_back();
        return out;
    }

    std::optional<json::Value> coerce(coercion::Result (*f)(const json::Value&), const json::Value& value) {
        auto result = f(value);
        if (auto* failure = std::get_if<coercion::Failure>(&result)) {
            return fail(failure->type, failure->msg, value);
        }
        return std::get<json::Value>(std::move(result));
    }

    // `oneOf`: an optional (one branch plus `null`) is validated against the
    // non-null branch with no extra location. Otherwise branches are tried
    // first without coercion, then with; if none fits, every branch's errors
    // are reported, each under its tag.
    std::optional<json::Value> union_of(const std::vector<DataSchema>& branches, const json::Value& value) {
        auto is_null = [](const DataSchema& b) { return b.data_type == DataType::null && !b.one_of; };
        if (branches.size() == 2 && std::ranges::any_of(branches, is_null)) {
            const auto other =
                std::ranges::find_if(branches, [&](const DataSchema& b) { return !is_null(b); });
            if (other != branches.end()) {
                if (value.is_null()) {
                    return json::Value(nullptr);
                }
                return check(*other, value);
            }
        }
        if (std::ranges::all_of(branches, [](const DataSchema& b) { return b.constant.has_value(); })) {
            std::vector<json::Value> allowed;
            for (const auto& b : branches) {
                allowed.push_back(*b.constant);
            }
            return literal(allowed, value);
        }
        // Nested lists of numbers pass whatever their shape; decoding checks
        // that they are rectangular.
        if (std::ranges::any_of(branches,
                                [](const DataSchema& b) { return b.data_type == DataType::array; }) &&
            std::ranges::all_of(branches, nested_numbers_schema) && nested_numbers(value)) {
            return value;
        }

        std::vector<ValidationIssue> attempts;
        for (const bool strict : {true, false}) {
            for (std::size_t i = 0; i < branches.size(); ++i) {
                const auto& branch = branches[i];
                if (strict && !exact_type(branch, value)) {
                    continue;
                }
                Run run(patterns_, loc_);
                run.loc_.emplace_back(branch_tag(branch, i));
                auto result = run.check(branch, value);
                if (result && run.issues_.empty()) {
                    return result;
                }
                if (!strict) {
                    attempts.insert(attempts.end(), run.issues_.begin(), run.issues_.end());
                }
            }
        }
        issues_.insert(issues_.end(), attempts.begin(), attempts.end());
        return std::nullopt;
    }

    std::optional<json::Value> literal(const std::vector<json::Value>& allowed, const json::Value& value) {
        for (const auto& candidate : allowed) {
            if (literal_matches(candidate, value)) {
                return candidate;
            }
        }
        const std::string expected = python_list(allowed);
        return fail("literal_error", "Input should be " + expected, value,
                    json::Value{{"expected", expected}});
    }

    // Numeric constraints on the coerced `value`: only the first failure,
    // in the order `multiple_of`, `le`, `lt`, `ge`, `gt`, showing the
    // `original` input. For a `number` schema the bound in `ctx` is a float.
    std::optional<json::Value> numeric_bounds(const DataSchema& schema, json::Value value,
                                              const json::Value& original) {
        const double x = as_f64(value);
        const bool as_float = schema.data_type == DataType::number;
        auto ctx_number = [&](const json::Value& n) { return as_float ? json::Value(as_f64(n)) : n; };
        if (schema.multiple_of) {
            const double step = as_f64(*schema.multiple_of);
            if (step > 0.0 && !is_multiple(x, step)) {
                return fail("multiple_of",
                            "Input should be a multiple of " + display_number(*schema.multiple_of), original,
                            json::Value{{"multiple_of", ctx_number(*schema.multiple_of)}});
            }
        }
        struct Check {
            const std::optional<json::Value>& bound;
            const char* type;
            const char* key;
            const char* words;
            bool (*ok)(double, double);
        };
        const Check checks[] = {
            {schema.maximum, "less_than_equal", "le", "less than or equal to",
             [](double a, double b) { return a <= b; }},
            {schema.exclusive_maximum, "less_than", "lt", "less than",
             [](double a, double b) { return a < b; }},
            {schema.minimum, "greater_than_equal", "ge", "greater than or equal to",
             [](double a, double b) { return a >= b; }},
            {schema.exclusive_minimum, "greater_than", "gt", "greater than",
             [](double a, double b) { return a > b; }},
        };
        for (const auto& c : checks) {
            if (c.bound && !c.ok(x, as_f64(*c.bound))) {
                return fail(c.type,
                            std::string("Input should be ") + c.words + " " + display_number(*c.bound),
                            original, json::Value{{c.key, ctx_number(*c.bound)}});
            }
        }
        return value;
    }

    // String constraints: the first failure of `min_length`, `max_length`
    // and `pattern`, in that order.
    std::optional<json::Value> string_bounds(const DataSchema& schema, json::Value value) {
        const auto& s = value.get_ref<const std::string&>();
        const auto length = char_count(s);
        if (schema.min_length && length < *schema.min_length) {
            const auto min = *schema.min_length;
            return fail("string_too_short",
                        "String should have at least " + std::to_string(min) + " character" + plural(min),
                        value, json::Value{{"min_length", min}});
        }
        if (schema.max_length && length > *schema.max_length) {
            const auto max = *schema.max_length;
            return fail("string_too_long",
                        "String should have at most " + std::to_string(max) + " character" + plural(max),
                        value, json::Value{{"max_length", max}});
        }
        if (schema.pattern) {
            const auto it = patterns_.find(*schema.pattern);
            if (it != patterns_.end() && !re2::RE2::PartialMatch(s, *it->second)) {
                return fail("string_pattern_mismatch",
                            "String should match pattern '" + *schema.pattern + "'", value,
                            json::Value{{"pattern", *schema.pattern}});
            }
        }
        return value;
    }

    std::optional<json::Value> length_error(const char* type, const char* field_type, std::uint64_t limit,
                                            std::size_t actual, const json::Value& value) {
        const bool too_short = std::string_view(type) == "too_short";
        const std::string msg = std::string(field_type) + " should have " +
                                (too_short ? "at least " : "at most ") + std::to_string(limit) + " item" +
                                plural(limit) + " after validation, not " + std::to_string(actual);
        json::Value ctx = json::Value::object();
        ctx["field_type"] = field_type;
        ctx[too_short ? "min_length" : "max_length"] = limit;
        ctx["actual_length"] = static_cast<std::uint64_t>(actual);
        return fail(type, msg, value, std::move(ctx));
    }

    // Lists and tuples. A list longer than its maximum reports only
    // `too_long`; otherwise item errors come first, and `too_short` only if
    // every item is valid.
    std::optional<json::Value> array(const DataSchema& schema, const json::Value& value) {
        const bool tuple = schema.items && schema.items->tuple;
        if (!value.is_array()) {
            return tuple ? fail("tuple_type", "Input should be a valid tuple", value)
                         : fail("list_type", "Input should be a valid list", value);
        }
        const char* field_type = tuple ? "Tuple" : "List";
        std::optional<std::uint64_t> max =
            tuple ? std::optional<std::uint64_t>(schema.items->schemas.size()) : schema.max_items;
        if (max && value.size() > *max) {
            return length_error("too_long", field_type, *max, value.size(), value);
        }
        const auto before = issues_.size();
        json::Value out = json::Value::array();
        if (tuple) {
            const auto& positions = schema.items->schemas;
            for (std::size_t i = 0; i < positions.size(); ++i) {
                if (i < value.size()) {
                    if (auto v = at(i, [&] { return check(positions[i], value[i]); })) {
                        out.push_back(std::move(*v));
                    }
                } else {
                    at(i, [&] { return fail("missing", "Field required", value); });
                }
            }
        } else if (schema.items) {
            const auto& item_schema = schema.items->schemas.at(0);
            for (std::size_t i = 0; i < value.size(); ++i) {
                if (auto v = at(i, [&] { return check(item_schema, value[i]); })) {
                    out.push_back(std::move(*v));
                }
            }
        } else {
            out = value;
        }
        if (issues_.size() > before) {
            return std::nullopt;
        }
        if (!tuple && schema.min_items && value.size() < *schema.min_items) {
            return length_error("too_short", field_type, *schema.min_items, value.size(), value);
        }
        return out;
    }

    // Objects: models (with `properties`) and maps. A model that isn't an
    // object is `model_attributes_type`.
    std::optional<json::Value> object(const DataSchema& schema, const json::Value& value) {
        const json::Value* additional = nullptr;
        if (auto it = schema.extra.find("additionalProperties"); it != schema.extra.end()) {
            additional = &*it;
        }
        if (!value.is_object()) {
            if (schema.properties) {
                return fail("model_attributes_type",
                            "Input should be a valid dictionary or object to extract fields from", value);
            }
            return fail("dict_type", "Input should be a valid dictionary", value);
        }
        const auto before = issues_.size();
        json::Value out = json::Value::object();
        if (schema.properties) {
            const auto& required = schema.required ? *schema.required : std::vector<std::string>{};
            for (const auto& [name, property] : *schema.properties) {
                if (auto member = value.find(name); member != value.end()) {
                    if (auto v = at(name, [&] { return check(property, *member); })) {
                        out[name] = std::move(*v);
                    }
                } else if (std::ranges::find(required, name) != required.end()) {
                    at(name, [&] { return fail("missing", "Field required", value); });
                }
            }
        }
        std::optional<DataSchema> member_schema;
        if (additional && additional->is_object()) {
            try {
                member_schema = td::data_schema_from_json(*additional);
            } catch (const td::InvalidDataSchema&) {
                // Members that would need it are dropped, as teta-wot does.
            }
        }
        for (const auto& [name, member] : value.items()) {
            if (schema.properties &&
                std::ranges::any_of(*schema.properties, [&](const auto& p) { return p.first == name; })) {
                continue;
            }
            if (additional && additional->is_boolean() && !additional->get<bool>()) {
                at(name, [&] { return fail("extra_forbidden", "Extra inputs are not permitted", member); });
            } else if (additional && additional->is_object()) {
                if (member_schema) {
                    if (auto v = at(name, [&] { return check(*member_schema, member); })) {
                        out[name] = std::move(*v);
                    }
                }
            } else if (additional) {
                out[name] = member;  // `true`: keep
            } else if (!schema.properties) {
                out[name] = member;  // a map: keep
            }
            // A model without `additionalProperties` ignores unknown members.
        }
        if (issues_.size() != before) {
            return std::nullopt;
        }
        return out;
    }

    const Patterns& patterns_;
    Loc loc_;
    std::vector<ValidationIssue> issues_;
};

}  // namespace

SchemaValidator::SchemaValidator(td::DataSchema schema) : schema_(std::move(schema)) {
    auto patterns = std::make_shared<Patterns>();
    collect_patterns(schema_, *patterns);
    patterns_ = std::move(patterns);
}

json::Value SchemaValidator::validate(const json::Value& value, const Loc& loc) const {
    Run run(*patterns_, loc);
    auto result = run.check(schema_, value);
    if (result && run.issues().empty()) {
        return std::move(*result);
    }
    throw ValidationError(std::move(run.issues()));
}

}  // namespace teta::wot
