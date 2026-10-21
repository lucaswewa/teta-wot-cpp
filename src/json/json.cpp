#include "teta/wot/json.hpp"

#include <charconv>
#include <cmath>
#include <stdexcept>
#include <system_error>

namespace teta::wot::json {

namespace {

constexpr char kHex[] = "0123456789abcdef";

// The shortest decimal digits of a positive finite double, and the exponent
// of its first digit: `value == 0.d1d2d3... * 10^(exponent + 1)`.
struct Decimal {
    char digits[32];
    int length = 0;
    int exponent = 0;
};

Decimal shortest(double value) {
    // `to_chars` without a precision gives the shortest representation that
    // round-trips; in scientific form it is `d[.ddd]e±XX`.
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof buffer, value, std::chars_format::scientific);
    if (result.ec != std::errc{}) {
        throw std::logic_error("to_chars failed on a finite double");
    }
    Decimal decimal;
    const char* p = buffer;
    for (; p != result.ptr && *p != 'e'; ++p) {
        if (*p != '.') {
            decimal.digits[decimal.length++] = *p;
        }
    }
    std::from_chars(p + (p[1] == '+' ? 2 : 1), result.ptr, decimal.exponent);
    return decimal;
}

void append_integer_text(std::string& out, auto value) {
    char buffer[24];
    const auto result = std::to_chars(buffer, buffer + sizeof buffer, value);
    out.append(buffer, result.ptr);
}

void append_compact(std::string& out, const Value& value) {
    switch (value.type()) {
        case Value::value_t::null:
            out += "null";
            return;
        case Value::value_t::boolean:
            out += value.get<bool>() ? "true" : "false";
            return;
        case Value::value_t::number_integer:
            append_integer_text(out, value.get<std::int64_t>());
            return;
        case Value::value_t::number_unsigned:
            append_integer_text(out, value.get<std::uint64_t>());
            return;
        case Value::value_t::number_float:
            append_double(out, value.get<double>());
            return;
        case Value::value_t::string:
            append_string(out, value.get_ref<const std::string&>());
            return;
        case Value::value_t::array: {
            out += '[';
            bool first = true;
            for (const auto& item : value) {
                if (!first) {
                    out += ',';
                }
                first = false;
                append_compact(out, item);
            }
            out += ']';
            return;
        }
        case Value::value_t::object: {
            out += '{';
            bool first = true;
            for (const auto& [key, item] : value.items()) {
                if (!first) {
                    out += ',';
                }
                first = false;
                append_string(out, key);
                out += ':';
                append_compact(out, item);
            }
            out += '}';
            return;
        }
        case Value::value_t::binary:
        case Value::value_t::discarded:
            break;
    }
    throw std::invalid_argument("a binary or discarded value has no JSON text");
}

void newline_and_indent(std::string& out, int level) {
    out += '\n';
    out.append(static_cast<std::size_t>(level) * 4, ' ');
}

void append_pretty(std::string& out, const Value& value, int level) {
    if (value.is_array() && !value.empty()) {
        out += '[';
        bool first = true;
        for (const auto& item : value) {
            if (!first) {
                out += ',';
            }
            first = false;
            newline_and_indent(out, level + 1);
            append_pretty(out, item, level + 1);
        }
        newline_and_indent(out, level);
        out += ']';
        return;
    }
    if (value.is_object() && !value.empty()) {
        out += '{';
        bool first = true;
        for (const auto& [key, item] : value.items()) {
            if (!first) {
                out += ',';
            }
            first = false;
            newline_and_indent(out, level + 1);
            append_string(out, key);
            out += ": ";
            append_pretty(out, item, level + 1);
        }
        newline_and_indent(out, level);
        out += '}';
        return;
    }
    // Scalars, `[]` and `{}` are the same in both forms.
    append_compact(out, value);
}

}  // namespace

void append_double(std::string& out, double value) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument("JSON can't hold a non-finite number");
    }
    if (std::signbit(value)) {
        out += '-';
        value = -value;
    }
    if (value == 0.0) {
        out += "0.0";
        return;
    }
    const Decimal d = shortest(value);
    const std::string_view digits(d.digits, static_cast<std::size_t>(d.length));
    // The layout of `serde_json` 1.0.151: decimal for exponents -5 to 15,
    // scientific otherwise.
    if (d.exponent >= 0 && d.exponent <= 15) {
        const int integer_digits = d.exponent + 1;
        if (d.length <= integer_digits) {
            // 1234e7 -> 12340000000.0
            out += digits;
            out.append(static_cast<std::size_t>(integer_digits - d.length), '0');
            out += ".0";
        } else {
            // 1234e-2 -> 12.34
            out += digits.substr(0, static_cast<std::size_t>(integer_digits));
            out += '.';
            out += digits.substr(static_cast<std::size_t>(integer_digits));
        }
    } else if (d.exponent < 0 && d.exponent >= -5) {
        // 1234e-6 -> 0.001234
        out += "0.";
        out.append(static_cast<std::size_t>(-d.exponent - 1), '0');
        out += digits;
    } else {
        // 1234e30 -> 1.234e+33
        out += digits[0];
        if (d.length > 1) {
            out += '.';
            out += digits.substr(1);
        }
        out += d.exponent < 0 ? "e-" : "e+";
        append_integer_text(out, d.exponent < 0 ? -d.exponent : d.exponent);
    }
}

void append_string(std::string& out, std::string_view text) {
    out += '"';
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (byte < 0x20) {
                    out += "\\u00";
                    out += kHex[byte >> 4];
                    out += kHex[byte & 0xF];
                } else {
                    out += c;
                }
        }
    }
    out += '"';
}

std::string dump(const Value& value) {
    std::string out;
    append_compact(out, value);
    return out;
}

std::string dump_pretty(const Value& value) {
    std::string out;
    append_pretty(out, value, 0);
    return out;
}

}  // namespace teta::wot::json
