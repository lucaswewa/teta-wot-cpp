// A port of `uuid` 1.26.1's `parser.rs` and `error.rs` (`InvalidUuid::into_err`).

#include "path_uuid.hpp"

#include <array>
#include <format>
#include <optional>

namespace teta::wot::http::detail {

namespace {

// The form `try_parse` was trying when it failed.
enum class Requested { any, hyphenated };

struct Invalid {
    std::string_view input;
    Requested requested;
};

int hex_digit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

std::optional<Uuid> decode_hex32(std::string_view hex) {
    std::array<unsigned char, 16> bytes{};
    for (std::size_t i = 0; i < 16; ++i) {
        const int high = hex_digit(hex[2 * i]);
        const int low = hex_digit(hex[2 * i + 1]);
        if (high < 0 || low < 0) {
            return std::nullopt;
        }
        bytes[i] = static_cast<unsigned char>(high << 4 | low);
    }
    return Uuid(bytes);
}

std::variant<Uuid, Invalid> parse_hyphenated(std::string_view s) {
    if (s.size() != 36 || s[8] != '-' || s[13] != '-' || s[18] != '-' || s[23] != '-') {
        return Invalid{s, Requested::hyphenated};
    }
    std::string hex;
    hex.reserve(32);
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (i != 8 && i != 13 && i != 18 && i != 23) {
            hex += s[i];
        }
    }
    if (auto id = decode_hex32(hex)) {
        return *id;
    }
    return Invalid{s, Requested::hyphenated};
}

std::variant<Uuid, Invalid> try_parse(std::string_view input) {
    if (input.size() == 32) {
        if (auto id = decode_hex32(input)) {
            return *id;
        }
        return Invalid{input, Requested::any};
    }
    if (input.size() == 36) {
        return parse_hyphenated(input);
    }
    if (input.size() == 38 && input.front() == '{' && input.back() == '}') {
        return parse_hyphenated(input.substr(1, 36));
    }
    if (input.size() == 45 && input.starts_with("urn:uuid:")) {
        return parse_hyphenated(input.substr(9));
    }
    return Invalid{input, Requested::any};
}

// The next character of UTF-8 text at `i`, and its length.
std::pair<std::string_view, std::size_t> char_at(std::string_view text, std::size_t i) {
    const auto b = static_cast<unsigned char>(text[i]);
    const std::size_t length = b < 0x80 ? 1 : b < 0xE0 ? 2 : b < 0xF0 ? 3 : 4;
    return {text.substr(i, length), length};
}

bool valid_utf8(std::string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto b = static_cast<unsigned char>(text[i]);
        std::size_t length = 0;
        std::uint32_t code = 0;
        if (b < 0x80) {
            length = 1;
            code = b;
        } else if (b >= 0xC2 && b <= 0xDF) {
            length = 2;
            code = b & 0x1F;
        } else if (b >= 0xE0 && b <= 0xEF) {
            length = 3;
            code = b & 0x0F;
        } else if (b >= 0xF0 && b <= 0xF4) {
            length = 4;
            code = b & 0x07;
        } else {
            return false;
        }
        if (i + length > text.size()) {
            return false;
        }
        for (std::size_t k = 1; k < length; ++k) {
            const auto c = static_cast<unsigned char>(text[i + k]);
            if ((c & 0xC0) != 0x80) {
                return false;
            }
            code = code << 6 | (c & 0x3F);
        }
        if ((length == 3 && (code < 0x800 || (code >= 0xD800 && code <= 0xDFFF))) ||
            (length == 4 && (code < 0x10000 || code > 0x10FFFF))) {
            return false;
        }
        i += length;
    }
    return true;
}

// `InvalidUuid::into_err`, displayed.
std::string describe(const Invalid& invalid) {
    const std::string_view input = invalid.input;
    if (input.empty() || input.size() > 45) {
        return std::format("invalid length: found {}", input.size());
    }
    if (!valid_utf8(input)) {
        return "non-UTF8 input";
    }
    enum class Format { any, hyphenated, braced, urn };
    Format format = invalid.requested == Requested::any ? Format::any : Format::hyphenated;
    std::size_t start = 0;
    std::size_t end = input.size();
    if (format == Format::any && input.size() >= 2 && input.front() == '{' && input.back() == '}') {
        start = 1;
        end = input.size() - 1;
        format = Format::braced;
    } else if (format == Format::any && input.starts_with("urn:uuid:")) {
        start = 9;
        format = Format::urn;
    }
    std::size_t hyphens = 0;
    std::array<std::size_t, 4> group_bounds{};
    for (std::size_t i = start; i < end;) {
        const auto [character, length] = char_at(input, i);
        if (length == 1 && hex_digit(character[0]) >= 0) {
            // A hex digit.
        } else if (character == "-") {
            if (format == Format::any) {
                format = Format::hyphenated;
            }
            if (hyphens < 4) {
                group_bounds[hyphens] = i - start;
            }
            ++hyphens;
        } else {
            return std::format("invalid character: found `{}` at {}", character, i);
        }
        i += length;
    }
    if (format == Format::any) {
        // Every character is valid, so the length isn't.
        return std::format("invalid length: found {}", input.size());
    }
    if (hyphens != 4) {
        return std::format("invalid group count: expected 5, found {}", hyphens + 1);
    }
    constexpr std::array<std::size_t, 5> block_starts = {0, 9, 14, 19, 24};
    constexpr std::array<std::size_t, 5> expected = {8, 4, 4, 4, 12};
    for (std::size_t i = 0; i < 4; ++i) {
        if (group_bounds[i] != block_starts[i + 1] - 1) {
            return std::format("invalid group length in group {}: expected {}, found {}", i, expected[i],
                               group_bounds[i] - block_starts[i]);
        }
    }
    // The last group is too long.
    return std::format("invalid group length in group 4: expected 12, found {}",
                       input.size() - block_starts[4]);
}

}  // namespace

std::variant<Uuid, std::string> parse_path_uuid(std::string_view text) {
    auto parsed = try_parse(text);
    if (const auto* id = std::get_if<Uuid>(&parsed)) {
        return *id;
    }
    return describe(std::get<Invalid>(parsed));
}

}  // namespace teta::wot::http::detail
