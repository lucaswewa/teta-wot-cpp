// A small baseline JPEG encoder for the examples' simulated cameras (the Rust
// examples use the `jpeg-encoder` crate): RGB or grey pixels in, a JFIF file out,
// without chroma subsampling, with the example tables of the JPEG
// specification (ITU-T T.81, Annex K) scaled by quality as libjpeg does.

#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

namespace examples::jpeg {

namespace detail {

// The natural (row-major) index of each coefficient in zig-zag order.
inline constexpr std::array<std::uint8_t, 64> zigzag = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
    41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
    30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

// Annex K.1's quantisation tables, in natural order.
inline constexpr std::array<std::uint8_t, 64> luminance_quantisation = {
    16, 11,  10,  16, 24, 40, 51, 61, 12,  12,  14,  19,  26, 58, 60, 55,  14,  13,  16,  24, 40, 57,
    69, 56,  14,  17, 22, 29, 51, 87, 80,  62,  18,  22,  37, 56, 68, 109, 103, 77,  24,  35, 55, 64,
    81, 104, 113, 92, 49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98,  112, 100, 103, 99};
inline constexpr std::array<std::uint8_t, 64> chrominance_quantisation = {
    17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99, 24, 26, 56, 99, 99, 99,
    99, 99, 47, 66, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99};

// A Huffman table as a DHT segment holds it: how many codes of each length,
// 1 to 16, and the symbols in code order (Annex K.3).
struct HuffmanSpec {
    std::array<std::uint8_t, 16> counts;
    std::vector<std::uint8_t> symbols;
};

inline const HuffmanSpec& dc_luminance() {
    static const HuffmanSpec spec{{0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0},
                                  {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}};
    return spec;
}

inline const HuffmanSpec& dc_chrominance() {
    static const HuffmanSpec spec{{0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0},
                                  {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}};
    return spec;
}

inline const HuffmanSpec& ac_luminance() {
    static const HuffmanSpec spec{
        {0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d},
        {0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22,
         0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33,
         0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x34,
         0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55,
         0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76,
         0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96,
         0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5,
         0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4,
         0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1,
         0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa}};
    return spec;
}

inline const HuffmanSpec& ac_chrominance() {
    static const HuffmanSpec spec{
        {0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77},
        {0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13,
         0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0, 0x15, 0x62,
         0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26, 0x27, 0x28, 0x29,
         0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54,
         0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75,
         0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94,
         0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3,
         0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2,
         0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea,
         0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa}};
    return spec;
}

// A code and its length in bits.
struct Code {
    std::uint16_t bits = 0;
    std::uint8_t length = 0;
};

// The canonical codes of a table, by symbol.
inline std::array<Code, 256> codes_of(const HuffmanSpec& spec) {
    std::array<Code, 256> codes{};
    std::uint16_t code = 0;
    std::size_t k = 0;
    for (std::uint8_t length = 1; length <= 16; ++length) {
        for (std::uint8_t i = 0; i < spec.counts[length - 1]; ++i) {
            codes[spec.symbols[k++]] = {code++, length};
        }
        code = static_cast<std::uint16_t>(code << 1);
    }
    return codes;
}

// Writes entropy-coded bits, stuffing a 0 after each 0xFF byte.
class BitWriter {
  public:
    explicit BitWriter(std::string& out) : out_(out) {}

    void write(std::uint32_t bits, int length) {
        for (int i = length - 1; i >= 0; --i) {
            byte_ = static_cast<std::uint8_t>((byte_ << 1) | ((bits >> i) & 1));
            if (++count_ == 8) {
                flush_byte();
            }
        }
    }

    // Pads the last byte with 1 bits.
    void finish() {
        while (count_ != 0) {
            write(1, 1);
        }
    }

  private:
    void flush_byte() {
        out_ += static_cast<char>(byte_);
        if (byte_ == 0xff) {
            out_ += '\0';
        }
        byte_ = 0;
        count_ = 0;
    }

    std::string& out_;
    std::uint8_t byte_ = 0;
    int count_ = 0;
};

// The number of bits of |value|, and its bits as JPEG writes them.
inline std::pair<int, std::uint32_t> magnitude(int value) {
    const int size =
        value == 0 ? 0 : static_cast<int>(std::bit_width(static_cast<unsigned>(std::abs(value))));
    const int bits = value < 0 ? value + (1 << size) - 1 : value;
    return {size, static_cast<std::uint32_t>(bits)};
}

// The quantisation table for `quality` (1 to 100), as libjpeg scales it.
inline std::array<std::uint8_t, 64> scaled(const std::array<std::uint8_t, 64>& table, int quality) {
    quality = std::clamp(quality, 1, 100);
    const int scale = quality < 50 ? 5000 / quality : 200 - 2 * quality;
    std::array<std::uint8_t, 64> out{};
    for (std::size_t i = 0; i < 64; ++i) {
        out[i] = static_cast<std::uint8_t>(std::clamp((table[i] * scale + 50) / 100, 1, 255));
    }
    return out;
}

inline void put_u16(std::string& out, std::size_t value) {
    out += static_cast<char>((value >> 8) & 0xff);
    out += static_cast<char>(value & 0xff);
}

inline void put_marker(std::string& out, std::uint8_t marker, std::size_t length) {
    out += '\xff';
    out += static_cast<char>(marker);
    put_u16(out, length);
}

// Encodes an image of `components` components: 1 (greyscale) or 3 (YCbCr).
// `sample(x, y, out)` writes the pixel's components, level-shifted (-128
// to 127).
template <typename Sample>
std::string encode_components(std::size_t components, int width, int height, int quality, Sample sample) {
    // Luminance's tables, then chrominance's for colour.
    const std::size_t tables = components == 1 ? 1 : 2;
    const std::array<std::array<std::uint8_t, 64>, 2> quantisation = {
        scaled(luminance_quantisation, quality), scaled(chrominance_quantisation, quality)};
    const std::array<const HuffmanSpec*, 4> specs = {&dc_luminance(), &ac_luminance(), &dc_chrominance(),
                                                     &ac_chrominance()};

    std::string out = "\xff\xd8";
    // APP0: JFIF 1.01, no units, a 1:1 pixel aspect, no thumbnail.
    put_marker(out, 0xe0, 16);
    out.append("JFIF\0\x01\x01\0\0\x01\0\x01\0\0", 14);
    // DQT: the tables, in zig-zag order.
    put_marker(out, 0xdb, 2 + tables * 65);
    for (std::uint8_t t = 0; t < tables; ++t) {
        out += static_cast<char>(t);
        for (const auto index : zigzag) {
            out += static_cast<char>(quantisation[t][index]);
        }
    }
    // SOF0: 8 bits, the components without subsampling.
    put_marker(out, 0xc0, 8 + 3 * components);
    out += '\x08';
    put_u16(out, static_cast<std::size_t>(height));
    put_u16(out, static_cast<std::size_t>(width));
    out += static_cast<char>(components);
    for (std::size_t c = 0; c < components; ++c) {
        out += static_cast<char>(c + 1);
        out += '\x11';
        out += static_cast<char>(c == 0 ? 0 : 1);
    }
    // DHT: DC and AC tables for luminance (0) and chrominance (1).
    std::size_t dht_length = 2;
    for (std::size_t i = 0; i < 2 * tables; ++i) {
        dht_length += 17 + specs[i]->symbols.size();
    }
    put_marker(out, 0xc4, dht_length);
    const std::array<std::uint8_t, 4> classes = {0x00, 0x10, 0x01, 0x11};
    for (std::size_t i = 0; i < 2 * tables; ++i) {
        out += static_cast<char>(classes[i]);
        for (const auto count : specs[i]->counts) {
            out += static_cast<char>(count);
        }
        out.append(specs[i]->symbols.begin(), specs[i]->symbols.end());
    }
    // SOS: all the components, in one scan.
    put_marker(out, 0xda, 6 + 2 * components);
    out += static_cast<char>(components);
    for (std::size_t c = 0; c < components; ++c) {
        out += static_cast<char>(c + 1);
        out += static_cast<char>(c == 0 ? 0x00 : 0x11);
    }
    out.append("\x00\x3f\x00", 3);

    const std::array<std::array<Code, 256>, 4> codes = {codes_of(*specs[0]), codes_of(*specs[1]),
                                                        codes_of(*specs[2]), codes_of(*specs[3])};
    std::array<std::array<double, 8>, 8> cosines{};
    for (int x = 0; x < 8; ++x) {
        for (int u = 0; u < 8; ++u) {
            cosines[x][u] = std::cos((2 * x + 1) * u * std::numbers::pi / 16);
        }
    }
    BitWriter writer(out);
    std::array<int, 3> previous_dc{};
    for (int top = 0; top < height; top += 8) {
        for (int left = 0; left < width; left += 8) {
            // The block's samples, repeating the edge pixels past the image.
            std::array<std::array<double, 64>, 3> samples{};
            for (int y = 0; y < 8; ++y) {
                for (int x = 0; x < 8; ++x) {
                    const auto px = static_cast<std::size_t>(std::min(left + x, width - 1));
                    const auto py = static_cast<std::size_t>(std::min(top + y, height - 1));
                    std::array<double, 3> pixel{};
                    sample(px, py, pixel);
                    const auto i = static_cast<std::size_t>(y * 8 + x);
                    for (std::size_t c = 0; c < components; ++c) {
                        samples[c][i] = pixel[c];
                    }
                }
            }
            for (std::size_t c = 0; c < components; ++c) {
                const std::size_t table = c == 0 ? 0 : 1;
                // A separable forward DCT, then quantisation.
                std::array<double, 64> rows{};
                for (int y = 0; y < 8; ++y) {
                    for (int u = 0; u < 8; ++u) {
                        double sum = 0;
                        for (int x = 0; x < 8; ++x) {
                            sum += samples[c][static_cast<std::size_t>(y * 8 + x)] * cosines[x][u];
                        }
                        rows[static_cast<std::size_t>(y * 8 + u)] =
                            sum * (u == 0 ? std::numbers::sqrt2 / 2 : 1.0);
                    }
                }
                std::array<int, 64> quantised{};
                for (int u = 0; u < 8; ++u) {
                    for (int v = 0; v < 8; ++v) {
                        double sum = 0;
                        for (int y = 0; y < 8; ++y) {
                            sum += rows[static_cast<std::size_t>(y * 8 + u)] * cosines[y][v];
                        }
                        const double coefficient = sum * (v == 0 ? std::numbers::sqrt2 / 2 : 1.0) / 4;
                        const auto i = static_cast<std::size_t>(v * 8 + u);
                        quantised[i] = static_cast<int>(std::lround(coefficient / quantisation[table][i]));
                    }
                }
                // Entropy coding: the DC difference, then runs of zeros.
                const auto& dc = codes[table * 2];
                const auto& ac = codes[table * 2 + 1];
                const auto [dc_size, dc_bits] = magnitude(quantised[0] - previous_dc[c]);
                previous_dc[c] = quantised[0];
                writer.write(dc[static_cast<std::size_t>(dc_size)].bits,
                             dc[static_cast<std::size_t>(dc_size)].length);
                writer.write(dc_bits, dc_size);
                int zeros = 0;
                for (std::size_t k = 1; k < 64; ++k) {
                    const int value = quantised[zigzag[k]];
                    if (value == 0) {
                        ++zeros;
                        continue;
                    }
                    while (zeros >= 16) {
                        writer.write(ac[0xf0].bits, ac[0xf0].length);
                        zeros -= 16;
                    }
                    const auto [size, bits] = magnitude(value);
                    const auto symbol = static_cast<std::size_t>((zeros << 4) | size);
                    writer.write(ac[symbol].bits, ac[symbol].length);
                    writer.write(bits, size);
                    zeros = 0;
                }
                if (zeros > 0) {
                    writer.write(ac[0x00].bits, ac[0x00].length);
                }
            }
        }
    }
    writer.finish();
    out += "\xff\xd9";
    return out;
}

inline void check_size(std::size_t size, int width, int height, std::size_t components) {
    if (width <= 0 || height <= 0 || width > 65535 || height > 65535 ||
        size != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * components) {
        throw std::invalid_argument("the pixels don't match the size of the image");
    }
}

}  // namespace detail

/// Encodes `rgb` (three bytes per pixel, row by row) as a baseline JPEG.
inline std::string encode(const std::vector<std::uint8_t>& rgb, int width, int height, int quality) {
    detail::check_size(rgb.size(), width, height, 3);
    return detail::encode_components(
        3, width, height, quality, [&](std::size_t x, std::size_t y, std::array<double, 3>& out) {
            const auto* pixel = &rgb[(y * static_cast<std::size_t>(width) + x) * 3];
            const double r = pixel[0];
            const double g = pixel[1];
            const double b = pixel[2];
            out[0] = 0.299 * r + 0.587 * g + 0.114 * b - 128;
            out[1] = -0.168736 * r - 0.331264 * g + 0.5 * b;
            out[2] = 0.5 * r - 0.418688 * g - 0.081312 * b;
        });
}

/// Encodes `grey` (one byte per pixel, row by row) as a greyscale baseline
/// JPEG.
inline std::string encode_grey(const std::vector<std::uint8_t>& grey, int width, int height, int quality) {
    detail::check_size(grey.size(), width, height, 1);
    return detail::encode_components(
        1, width, height, quality, [&](std::size_t x, std::size_t y, std::array<double, 3>& out) {
            out[0] = static_cast<double>(grey[y * static_cast<std::size_t>(width) + x]) - 128;
        });
}

}  // namespace examples::jpeg
