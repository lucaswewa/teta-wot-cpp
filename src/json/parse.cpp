// Parsing JSON as serde_json 1.0.151 does (`from_slice::<Value>`, without
// `float_roundtrip` or `arbitrary_precision`, as teta-wot builds it): a port
// of the code paths of its `de.rs` and `read.rs` (`SliceRead`) that a `Value`
// takes, so that values, error messages and error positions are serde_json's.
// An `error` is located at the byte just read, a `peek_error` at the byte
// peeked, as there.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

#include "teta/wot/json.hpp"

namespace teta::wot::json {

ParseError::ParseError(std::string message, std::size_t line, std::size_t column)
    : std::runtime_error(message + " at line " + std::to_string(line) + " column " + std::to_string(column)),
      message_(std::move(message)),
      line_(line),
      column_(column) {}

namespace {

// serde_json's `ErrorCode`s, as it writes them.
constexpr const char* eof_while_parsing_list = "EOF while parsing a list";
constexpr const char* eof_while_parsing_object = "EOF while parsing an object";
constexpr const char* eof_while_parsing_string = "EOF while parsing a string";
constexpr const char* eof_while_parsing_value = "EOF while parsing a value";
constexpr const char* expected_colon = "expected `:`";
constexpr const char* expected_list_comma_or_end = "expected `,` or `]`";
constexpr const char* expected_object_comma_or_end = "expected `,` or `}`";
constexpr const char* expected_some_ident = "expected ident";
constexpr const char* expected_some_value = "expected value";
constexpr const char* invalid_escape = "invalid escape";
constexpr const char* invalid_number = "invalid number";
constexpr const char* number_out_of_range = "number out of range";
constexpr const char* invalid_unicode_code_point = "invalid unicode code point";
constexpr const char* control_character = "control character (\\u0000-\\u001F) found while parsing a string";
constexpr const char* key_must_be_a_string = "key must be a string";
constexpr const char* lone_leading_surrogate = "lone leading surrogate in hex escape";
constexpr const char* trailing_comma = "trailing comma";
constexpr const char* trailing_characters = "trailing characters";
constexpr const char* unexpected_end_of_hex_escape = "unexpected end of hex escape";
constexpr const char* recursion_limit_exceeded = "recursion limit exceeded";

// serde_json's `POW10`: the literals, each the double nearest its power.
constexpr std::array<double, 309> pow10 = {
    1e000, 1e001, 1e002, 1e003, 1e004, 1e005, 1e006, 1e007, 1e008, 1e009, 1e010, 1e011, 1e012, 1e013, 1e014,
    1e015, 1e016, 1e017, 1e018, 1e019, 1e020, 1e021, 1e022, 1e023, 1e024, 1e025, 1e026, 1e027, 1e028, 1e029,
    1e030, 1e031, 1e032, 1e033, 1e034, 1e035, 1e036, 1e037, 1e038, 1e039, 1e040, 1e041, 1e042, 1e043, 1e044,
    1e045, 1e046, 1e047, 1e048, 1e049, 1e050, 1e051, 1e052, 1e053, 1e054, 1e055, 1e056, 1e057, 1e058, 1e059,
    1e060, 1e061, 1e062, 1e063, 1e064, 1e065, 1e066, 1e067, 1e068, 1e069, 1e070, 1e071, 1e072, 1e073, 1e074,
    1e075, 1e076, 1e077, 1e078, 1e079, 1e080, 1e081, 1e082, 1e083, 1e084, 1e085, 1e086, 1e087, 1e088, 1e089,
    1e090, 1e091, 1e092, 1e093, 1e094, 1e095, 1e096, 1e097, 1e098, 1e099, 1e100, 1e101, 1e102, 1e103, 1e104,
    1e105, 1e106, 1e107, 1e108, 1e109, 1e110, 1e111, 1e112, 1e113, 1e114, 1e115, 1e116, 1e117, 1e118, 1e119,
    1e120, 1e121, 1e122, 1e123, 1e124, 1e125, 1e126, 1e127, 1e128, 1e129, 1e130, 1e131, 1e132, 1e133, 1e134,
    1e135, 1e136, 1e137, 1e138, 1e139, 1e140, 1e141, 1e142, 1e143, 1e144, 1e145, 1e146, 1e147, 1e148, 1e149,
    1e150, 1e151, 1e152, 1e153, 1e154, 1e155, 1e156, 1e157, 1e158, 1e159, 1e160, 1e161, 1e162, 1e163, 1e164,
    1e165, 1e166, 1e167, 1e168, 1e169, 1e170, 1e171, 1e172, 1e173, 1e174, 1e175, 1e176, 1e177, 1e178, 1e179,
    1e180, 1e181, 1e182, 1e183, 1e184, 1e185, 1e186, 1e187, 1e188, 1e189, 1e190, 1e191, 1e192, 1e193, 1e194,
    1e195, 1e196, 1e197, 1e198, 1e199, 1e200, 1e201, 1e202, 1e203, 1e204, 1e205, 1e206, 1e207, 1e208, 1e209,
    1e210, 1e211, 1e212, 1e213, 1e214, 1e215, 1e216, 1e217, 1e218, 1e219, 1e220, 1e221, 1e222, 1e223, 1e224,
    1e225, 1e226, 1e227, 1e228, 1e229, 1e230, 1e231, 1e232, 1e233, 1e234, 1e235, 1e236, 1e237, 1e238, 1e239,
    1e240, 1e241, 1e242, 1e243, 1e244, 1e245, 1e246, 1e247, 1e248, 1e249, 1e250, 1e251, 1e252, 1e253, 1e254,
    1e255, 1e256, 1e257, 1e258, 1e259, 1e260, 1e261, 1e262, 1e263, 1e264, 1e265, 1e266, 1e267, 1e268, 1e269,
    1e270, 1e271, 1e272, 1e273, 1e274, 1e275, 1e276, 1e277, 1e278, 1e279, 1e280, 1e281, 1e282, 1e283, 1e284,
    1e285, 1e286, 1e287, 1e288, 1e289, 1e290, 1e291, 1e292, 1e293, 1e294, 1e295, 1e296, 1e297, 1e298, 1e299,
    1e300, 1e301, 1e302, 1e303, 1e304, 1e305, 1e306, 1e307, 1e308,
};

constexpr std::uint64_t u64_max = std::numeric_limits<std::uint64_t>::max();
constexpr std::int32_t i32_max = std::numeric_limits<std::int32_t>::max();
constexpr std::int32_t i32_min = std::numeric_limits<std::int32_t>::min();

// serde_json's `overflow!(a * 10 + b, c)`.
template <typename T>
bool overflows(T a, T b, T c) {
    return a >= c / 10 && (a > c / 10 || b > c % 10);
}

bool is_digit(int c) {
    return c >= '0' && c <= '9';
}

int hex_value(unsigned char c) {
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

std::int32_t saturate(std::int64_t value) {
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(value, i32_min, i32_max));
}

// The length of the UTF-8 sequence at `p` (`n` bytes left), or, if it is
// invalid, the length of its longest valid prefix (at least one byte),
// negated: Rust's `Utf8Chunks`, which make each such prefix one U+FFFD.
int utf8_sequence(const unsigned char* p, std::size_t n) {
    const unsigned char b = p[0];
    if (b < 0x80) {
        return 1;
    }
    int width = 0;
    unsigned char low = 0x80;
    unsigned char high = 0xBF;
    if (b >= 0xC2 && b <= 0xDF) {
        width = 2;
    } else if (b >= 0xE0 && b <= 0xEF) {
        width = 3;
        if (b == 0xE0) {
            low = 0xA0;
        } else if (b == 0xED) {
            high = 0x9F;
        }
    } else if (b >= 0xF0 && b <= 0xF4) {
        width = 4;
        if (b == 0xF0) {
            low = 0x90;
        } else if (b == 0xF4) {
            high = 0x8F;
        }
    } else {
        return -1;
    }
    for (int i = 1; i < width; ++i) {
        if (static_cast<std::size_t>(i) >= n) {
            return -i;
        }
        const unsigned char c = p[i];
        if (c < (i == 1 ? low : 0x80) || c > (i == 1 ? high : 0xBF)) {
            return -i;
        }
    }
    return width;
}

bool valid_utf8(std::string_view text) {
    const auto* p = reinterpret_cast<const unsigned char*>(text.data());
    for (std::size_t i = 0; i < text.size();) {
        const int length = utf8_sequence(p + i, text.size() - i);
        if (length < 0) {
            return false;
        }
        i += static_cast<std::size_t>(length);
    }
    return true;
}

void push_utf8(std::string& out, std::uint32_t n) {
    if (n < 0x80) {
        out += static_cast<char>(n);
    } else if (n < 0x800) {
        out += static_cast<char>(0xC0 | (n >> 6));
        out += static_cast<char>(0x80 | (n & 0x3F));
    } else if (n < 0x10000) {
        out += static_cast<char>(0xE0 | (n >> 12));
        out += static_cast<char>(0x80 | ((n >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (n & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (n >> 18));
        out += static_cast<char>(0x80 | ((n >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((n >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (n & 0x3F));
    }
}

class Parser {
  public:
    explicit Parser(std::string_view text)
        : data_(reinterpret_cast<const unsigned char*>(text.data())), size_(text.size()) {}

    // `from_trait`: a value, then only whitespace (`Deserializer::end`).
    Value document() {
        Value value = parse_value();
        if (parse_whitespace() != -1) {
            throw peek_error(trailing_characters);
        }
        return value;
    }

  private:
    // ---- SliceRead ----

    int peek() const { return index_ < size_ ? data_[index_] : -1; }
    int peek_or_null() const { return index_ < size_ ? data_[index_] : 0; }
    int next() { return index_ < size_ ? data_[index_++] : -1; }
    void eat() { ++index_; }

    // `position_of_index`: the line, and the bytes before `i` in it.
    ParseError at(std::size_t i, const char* message) const {
        std::size_t line = 1;
        std::size_t start_of_line = 0;
        for (std::size_t k = 0; k < i; ++k) {
            if (data_[k] == '\n') {
                ++line;
                start_of_line = k + 1;
            }
        }
        return ParseError(message, line, i - start_of_line);
    }
    // An error caused by the byte just read.
    ParseError error(const char* message) const { return at(index_, message); }
    // An error caused by the byte peeked.
    ParseError peek_error(const char* message) const { return at(std::min(size_, index_ + 1), message); }

    // ---- Deserializer ----

    // The first byte that isn't whitespace, not consumed; -1 at the end.
    int parse_whitespace() {
        for (;;) {
            const int c = peek();
            if (c != ' ' && c != '\n' && c != '\t' && c != '\r') {
                return c;
            }
            eat();
        }
    }

    // `deserialize_any`, with serde_json's `ValueVisitor`.
    Value parse_value() {
        const int c = parse_whitespace();
        switch (c) {
            case -1:
                throw peek_error(eof_while_parsing_value);
            case 'n':
                eat();
                parse_ident("ull");
                return nullptr;
            case 't':
                eat();
                parse_ident("rue");
                return true;
            case 'f':
                eat();
                parse_ident("alse");
                return false;
            case '-':
                eat();
                return parse_integer(false);
            case '"':
                eat();
                return Value(parse_string());
            case '[':
                return parse_array();
            case '{':
                return parse_object();
            default:
                if (is_digit(c)) {
                    return parse_integer(true);
                }
                throw peek_error(expected_some_value);
        }
    }

    void parse_ident(std::string_view ident) {
        for (const char expected : ident) {
            const int c = next();
            if (c == -1) {
                throw error(eof_while_parsing_value);
            }
            if (c != static_cast<unsigned char>(expected)) {
                throw error(expected_some_ident);
            }
        }
    }

    // ---- Numbers ----

    Value parse_integer(bool positive) {
        const int c = next();
        if (c == -1) {
            throw error(eof_while_parsing_value);
        }
        if (c == '0') {
            // There can be only one leading '0'.
            if (is_digit(peek_or_null())) {
                throw peek_error(invalid_number);
            }
            return parse_number(positive, 0);
        }
        if (c < '1' || c > '9') {
            throw error(invalid_number);
        }
        auto significand = static_cast<std::uint64_t>(c - '0');
        for (;;) {
            const int p = peek_or_null();
            if (!is_digit(p)) {
                return parse_number(positive, significand);
            }
            const auto digit = static_cast<std::uint64_t>(p - '0');
            // A u64 until it would overflow; then a double.
            if (overflows(significand, digit, u64_max)) {
                return Value(parse_long_integer(positive, significand));
            }
            eat();
            significand = significand * 10 + digit;
        }
    }

    Value parse_number(bool positive, std::uint64_t significand) {
        const int p = peek_or_null();
        if (p == '.') {
            return Value(parse_decimal(positive, significand, 0));
        }
        if (p == 'e' || p == 'E') {
            return Value(parse_exponent(positive, significand, 0));
        }
        if (positive) {
            return Value(significand);
        }
        // `(significand as i64).wrapping_neg()`: a double if that underflows,
        // and for `-0`.
        const auto negated = static_cast<std::int64_t>(std::uint64_t{0} - significand);
        if (negated >= 0) {
            return Value(-static_cast<double>(significand));
        }
        return Value(negated);
    }

    double parse_decimal(bool positive, std::uint64_t significand, std::int32_t exponent_before_point) {
        eat();
        std::int32_t exponent_after_point = 0;
        for (int p = peek_or_null(); is_digit(p); p = peek_or_null()) {
            const auto digit = static_cast<std::uint64_t>(p - '0');
            if (overflows(significand, digit, u64_max)) {
                return parse_decimal_overflow(positive, significand,
                                              exponent_before_point + exponent_after_point);
            }
            eat();
            significand = significand * 10 + digit;
            exponent_after_point -= 1;
        }
        // At least one digit after the decimal point.
        if (exponent_after_point == 0) {
            throw peek_error(peek() == -1 ? eof_while_parsing_value : invalid_number);
        }
        const std::int32_t exponent = exponent_before_point + exponent_after_point;
        if (const int p = peek_or_null(); p == 'e' || p == 'E') {
            return parse_exponent(positive, significand, exponent);
        }
        return f64_from_parts(positive, significand, exponent);
    }

    double parse_exponent(bool positive, std::uint64_t significand, std::int32_t starting_exp) {
        eat();
        bool positive_exp = true;
        if (const int p = peek_or_null(); p == '+') {
            eat();
        } else if (p == '-') {
            eat();
            positive_exp = false;
        }
        const int c = next();
        if (c == -1) {
            throw error(eof_while_parsing_value);
        }
        // A digit follows the exponent's sign.
        if (!is_digit(c)) {
            throw error(invalid_number);
        }
        std::int32_t exp = c - '0';
        for (int p = peek_or_null(); is_digit(p); p = peek_or_null()) {
            eat();
            const std::int32_t digit = p - '0';
            if (overflows(exp, digit, i32_max)) {
                return parse_exponent_overflow(positive, significand == 0, positive_exp);
            }
            exp = exp * 10 + digit;
        }
        const std::int32_t final_exp = positive_exp ? saturate(std::int64_t{starting_exp} + exp)
                                                    : saturate(std::int64_t{starting_exp} - exp);
        return f64_from_parts(positive, significand, final_exp);
    }

    // serde_json's arithmetic without `float_roundtrip`: the significand as a
    // double, multiplied or divided by a power of ten from the table. It isn't
    // always the nearest double, which is why it is ported.
    double f64_from_parts(bool positive, std::uint64_t significand, std::int32_t exponent) {
        auto f = static_cast<double>(significand);
        for (;;) {
            // `exponent.wrapping_abs() as usize`: i32::MIN stays out of the table.
            const std::int64_t magnitude = exponent == i32_min ? -1 : std::abs(std::int64_t{exponent});
            if (magnitude >= 0 && magnitude < static_cast<std::int64_t>(pow10.size())) {
                const double power = pow10[static_cast<std::size_t>(magnitude)];
                if (exponent >= 0) {
                    f *= power;
                    if (std::isinf(f)) {
                        throw error(number_out_of_range);
                    }
                } else {
                    f /= power;
                }
                break;
            }
            if (f == 0.0) {
                break;
            }
            if (exponent >= 0) {
                throw error(number_out_of_range);
            }
            f /= 1e308;
            exponent += 308;
        }
        return positive ? f : -f;
    }

    double parse_long_integer(bool positive, std::uint64_t significand) {
        std::int32_t exponent = 0;
        for (;;) {
            const int p = peek_or_null();
            if (is_digit(p)) {
                eat();
                exponent += 1;
            } else if (p == '.') {
                return parse_decimal(positive, significand, exponent);
            } else if (p == 'e' || p == 'E') {
                return parse_exponent(positive, significand, exponent);
            } else {
                return f64_from_parts(positive, significand, exponent);
            }
        }
    }

    // The next digit would overflow, so the rest are ignored.
    double parse_decimal_overflow(bool positive, std::uint64_t significand, std::int32_t exponent) {
        while (is_digit(peek_or_null())) {
            eat();
        }
        if (const int p = peek_or_null(); p == 'e' || p == 'E') {
            return parse_exponent(positive, significand, exponent);
        }
        return f64_from_parts(positive, significand, exponent);
    }

    double parse_exponent_overflow(bool positive, bool zero_significand, bool positive_exp) {
        // An error rather than an infinity.
        if (!zero_significand && positive_exp) {
            throw error(number_out_of_range);
        }
        while (is_digit(peek_or_null())) {
            eat();
        }
        return positive ? 0.0 : -0.0;
    }

    // ---- Strings ----

    // After the opening quote: `parse_str_bytes`, validating.
    std::string parse_string() {
        std::string scratch;
        std::size_t start = index_;
        for (;;) {
            while (index_ < size_ && data_[index_] != '"' && data_[index_] != '\\' && data_[index_] >= 0x20) {
                ++index_;
            }
            if (index_ == size_) {
                throw error(eof_while_parsing_string);
            }
            const unsigned char c = data_[index_];
            scratch.append(reinterpret_cast<const char*>(data_ + start), index_ - start);
            ++index_;
            if (c == '"') {
                if (!valid_utf8(scratch)) {
                    throw error(invalid_unicode_code_point);
                }
                return scratch;
            }
            if (c != '\\') {
                throw error(control_character);
            }
            parse_escape(scratch);
            start = index_;
        }
    }

    void parse_escape(std::string& scratch) {
        switch (next()) {
            case -1:
                throw error(eof_while_parsing_string);
            case '"':
                scratch += '"';
                break;
            case '\\':
                scratch += '\\';
                break;
            case '/':
                scratch += '/';
                break;
            case 'b':
                scratch += '\b';
                break;
            case 'f':
                scratch += '\f';
                break;
            case 'n':
                scratch += '\n';
                break;
            case 'r':
                scratch += '\r';
                break;
            case 't':
                scratch += '\t';
                break;
            case 'u':
                parse_unicode_escape(scratch);
                break;
            default:
                throw error(invalid_escape);
        }
    }

    // After `\u`. Surrogates must come in pairs.
    void parse_unicode_escape(std::string& scratch) {
        const std::uint32_t n1 = decode_hex_escape();
        if (n1 >= 0xDC00 && n1 <= 0xDFFF) {
            // A trailing surrogate, which serde_json's message calls leading.
            throw error(lone_leading_surrogate);
        }
        if (n1 < 0xD800 || n1 > 0xDBFF) {
            push_utf8(scratch, n1);
            return;
        }
        // A leading surrogate: `\u` and a trailing one must follow.
        for (const char expected : {'\\', 'u'}) {
            if (peek_or_eof() != expected) {
                eat();
                throw error(unexpected_end_of_hex_escape);
            }
            eat();
        }
        const std::uint32_t n2 = decode_hex_escape();
        if (n2 < 0xDC00 || n2 > 0xDFFF) {
            throw error(lone_leading_surrogate);
        }
        push_utf8(scratch, (((n1 - 0xD800) << 10) | (n2 - 0xDC00)) + 0x10000);
    }

    int peek_or_eof() const {
        if (index_ == size_) {
            throw error(eof_while_parsing_string);
        }
        return data_[index_];
    }

    std::uint32_t decode_hex_escape() {
        if (size_ - index_ < 4) {
            index_ = size_;
            throw error(eof_while_parsing_string);
        }
        const unsigned char* p = data_ + index_;
        index_ += 4;
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            const int digit = hex_value(p[i]);
            if (digit < 0) {
                throw error(invalid_escape);
            }
            value = value * 16 + static_cast<std::uint32_t>(digit);
        }
        return value;
    }

    // ---- Arrays and objects ----

    // `check_recursion!`, then the bracket.
    void enter() {
        if (--remaining_depth_ == 0) {
            throw peek_error(recursion_limit_exceeded);
        }
        eat();
    }

    Value parse_array() {
        enter();
        Value array = Value::array();
        for (bool first = true;; first = false) {
            // `SeqAccess::has_next_element`
            const int c = parse_whitespace();
            if (c == -1) {
                throw peek_error(eof_while_parsing_list);
            }
            if (c == ']') {
                break;
            }
            if (!first) {
                if (c != ',') {
                    throw peek_error(expected_list_comma_or_end);
                }
                eat();
                if (const int d = parse_whitespace(); d == ']') {
                    throw peek_error(trailing_comma);
                } else if (d == -1) {
                    throw peek_error(eof_while_parsing_value);
                }
            }
            array.push_back(parse_value());
        }
        ++remaining_depth_;
        eat();  // `end_seq`
        return array;
    }

    Value parse_object() {
        enter();
        Value object = Value::object();
        for (bool first = true;; first = false) {
            // `MapAccess::has_next_key`
            const int c = parse_whitespace();
            if (c == -1) {
                throw peek_error(eof_while_parsing_object);
            }
            if (c == '}') {
                break;
            }
            if (first) {
                if (c != '"') {
                    throw peek_error(key_must_be_a_string);
                }
            } else {
                if (c != ',') {
                    throw peek_error(expected_object_comma_or_end);
                }
                eat();
                if (const int d = parse_whitespace(); d == '}') {
                    throw peek_error(trailing_comma);
                } else if (d == -1) {
                    throw peek_error(eof_while_parsing_value);
                } else if (d != '"') {
                    throw peek_error(key_must_be_a_string);
                }
            }
            eat();
            std::string key = parse_string();
            // `parse_object_colon`
            if (const int colon = parse_whitespace(); colon == -1) {
                throw peek_error(eof_while_parsing_object);
            } else if (colon != ':') {
                throw peek_error(expected_colon);
            }
            eat();
            // A repeated key keeps its place and takes the new value, as an
            // IndexMap's `insert` does.
            object[key] = parse_value();
        }
        ++remaining_depth_;
        eat();  // `end_map`
        return object;
    }

    const unsigned char* data_;
    std::size_t size_;
    std::size_t index_ = 0;
    int remaining_depth_ = 128;
};

}  // namespace

Value parse(std::string_view text) {
    return Parser(text).document();
}

std::size_t character_offset(std::string_view text, std::size_t line, std::size_t column) {
    const auto* p = reinterpret_cast<const unsigned char*>(text.data());
    std::size_t before = 0;
    std::size_t i = 0;
    // The characters of each line before `line`, and one for its newline (or
    // for the end of the text: Rust's `split` yields a last, unterminated
    // line too).
    for (std::size_t lines = 1; lines < line && i <= text.size(); ++lines) {
        while (i < text.size() && p[i] != '\n') {
            const int length = utf8_sequence(p + i, text.size() - i);
            i += static_cast<std::size_t>(length < 0 ? -length : length);
            ++before;
        }
        ++before;
        ++i;
    }
    return before + (column == 0 ? 0 : column - 1);
}

}  // namespace teta::wot::json
