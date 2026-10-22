#include "teta/wot/uuid.hpp"

#include <random>

namespace teta::wot {

Uuid Uuid::v4() {
    thread_local std::mt19937_64 engine = [] {
        std::random_device device;
        std::seed_seq seed{device(), device(), device(), device(), device(), device(), device(), device()};
        return std::mt19937_64(seed);
    }();
    std::array<unsigned char, 16> bytes{};
    for (std::size_t i = 0; i < 16; i += 8) {
        auto word = engine();
        for (std::size_t k = 0; k < 8; ++k) {
            bytes[i + k] = static_cast<unsigned char>(word >> (8 * k));
        }
    }
    bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0F) | 0x40);
    bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3F) | 0x80);
    return Uuid(bytes);
}

namespace {

int hex_value(char c) {
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

}  // namespace

std::optional<Uuid> Uuid::parse(std::string_view text) {
    if (text.size() != 36) {
        return std::nullopt;
    }
    std::array<unsigned char, 16> bytes{};
    std::size_t byte = 0;
    for (std::size_t i = 0; i < text.size();) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (text[i] != '-') {
                return std::nullopt;
            }
            ++i;
            continue;
        }
        const int high = hex_value(text[i]);
        const int low = hex_value(text[i + 1]);
        if (high < 0 || low < 0) {
            return std::nullopt;
        }
        bytes[byte++] = static_cast<unsigned char>(high * 16 + low);
        i += 2;
    }
    return Uuid(bytes);
}

std::string Uuid::to_string() const {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(36);
    for (std::size_t i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            out += '-';
        }
        out += digits[bytes_[i] >> 4];
        out += digits[bytes_[i] & 0x0F];
    }
    return out;
}

bool Uuid::is_nil() const {
    for (unsigned char b : bytes_) {
        if (b != 0) {
            return false;
        }
    }
    return true;
}

}  // namespace teta::wot
