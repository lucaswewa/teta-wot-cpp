//! Writes what teta-wot v0.1.0's HTTP binding makes of request bodies and
//! path IDs, for the C++ binding's tests (`tests/http/wire_corpus.jsonl`).
//!
//! Lines of two kinds:
//!
//! - `{"body": "<hex>", ...}`: a request body, by its bytes in hexadecimal,
//!   and what `serde_json::from_slice::<Value>` makes of it, as the binding
//!   reads every JSON body: `"value"`, the value as serde_json writes it, or
//!   `"error"`, the message without its position, with the `line` and
//!   `column` serde_json reports and the `position` the binding puts in the
//!   error's `loc` (`char_offset` in `handlers.rs`, copied below);
//! - `{"uuid": "<text>", ...}`: a path segment and what `Uuid::parse_str`
//!   makes of it, as the binding reads invocation and Blob IDs: `"value"`,
//!   the UUID as it is displayed, or `"error"`, the message the binding puts
//!   in its 422.
//!
//! Bodies are a pool of edge cases and numbers from a seeded generator:
//! serde_json without `float_roundtrip` (as teta-wot builds it) parses
//! decimals with its own arithmetic, which isn't always correctly rounded,
//! so the C++ parser must do the same arithmetic.

use std::fmt::Write as _;

use serde_json::{Value, json};
use uuid::Uuid;

/// A deterministic generator, so the corpus is reproducible.
struct SplitMix64(u64);

impl SplitMix64 {
    fn next(&mut self) -> u64 {
        self.0 = self.0.wrapping_add(0x9E37_79B9_7F4A_7C15);
        let mut z = self.0;
        z = (z ^ (z >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
        z ^ (z >> 31)
    }

    fn below(&mut self, n: u64) -> u64 {
        self.next() % n
    }
}

/// The character offset of a 1-based line and column (teta-wot-http's
/// `handlers.rs`).
fn char_offset(text: &str, line: usize, column: usize) -> usize {
    let before: usize = text
        .split('\n')
        .take(line.saturating_sub(1))
        .map(|l| l.chars().count() + 1)
        .sum();
    before + column.saturating_sub(1)
}

fn body(out: &mut String, bytes: &[u8]) {
    let hex: String = bytes.iter().map(|b| format!("{b:02x}")).collect();
    let line = match serde_json::from_slice::<Value>(bytes) {
        Ok(value) => json!({"body": hex, "value": serde_json::to_string(&value).expect("JSON")}),
        Err(error) => {
            let text = String::from_utf8_lossy(bytes);
            let message = error.to_string();
            let message = message.split(" at line ").next().unwrap_or(&message);
            json!({
                "body": hex,
                "error": message,
                "line": error.line(),
                "column": error.column(),
                "position": char_offset(&text, error.line(), error.column()),
            })
        }
    };
    writeln!(out, "{line}").unwrap();
}

fn uuid(out: &mut String, text: &str) {
    let line = match Uuid::parse_str(text) {
        Ok(id) => json!({"uuid": text, "value": id.to_string()}),
        Err(error) => json!({"uuid": text, "error": error.to_string()}),
    };
    writeln!(out, "{line}").unwrap();
}

/// A decimal number as people and programs write them: a sign, digits, a
/// fraction, an exponent.
fn random_number(rng: &mut SplitMix64) -> String {
    let mut text = String::new();
    if rng.below(4) == 0 {
        text.push('-');
    }
    let most = if rng.below(3) == 0 { 40 } else { 19 };
    let digits = 1 + rng.below(most);
    let point = if rng.below(3) == 0 {
        None
    } else {
        Some(rng.below(digits + 1))
    };
    for i in 0..digits {
        if point == Some(i) {
            if i == 0 {
                text.push('0');
            }
            text.push('.');
        }
        let digit = if i == 0 && point != Some(0) {
            1 + rng.below(9)
        } else {
            rng.below(10)
        };
        text.push(char::from(b'0' + digit as u8));
    }
    if point == Some(digits) {
        text.push_str(".0");
    }
    if rng.below(2) == 0 {
        text.push(if rng.below(2) == 0 { 'e' } else { 'E' });
        match rng.below(3) {
            0 => text.push('-'),
            1 => text.push('+'),
            _ => {}
        }
        text.push_str(&rng.below(330).to_string());
    }
    text
}

fn main() {
    let mut out = String::new();
    let texts: &[&str] = &[
        // Values.
        "null", "true", "false", "0", "-0", "1", "-1", "0.0", "-0.0", "1.5", "1e5", "1E5", "1e+5",
        "1e-5", "1.25e2", "0.1", "0.2", "0.3", "0.30000000000000004", "123.456", "123.456e-7",
        "18446744073709551615", "18446744073709551616", "184467440737095516150",
        "9223372036854775807", "-9223372036854775808", "-9223372036854775809",
        "-18446744073709551615", "-18446744073709551616", "123456789012345678901234567890",
        "1.7976931348623157e308", "1.7976931348623158e308", "1.7976931348623159e308", "1e308",
        "1e309", "-1e309", "1e-307", "1e-308", "1e-320", "4.9e-324", "5e-324", "2.5e-324",
        "1e-400", "-1e-400", "2.2250738585072014e-308", "2.2250738585072011e-308",
        "9007199254740993", "9007199254740993.0", "0.1e1", "100e-2", "1e99999999999",
        "0e99999999999", "-1e-99999999999", "1e-99999999999", "0.000000000000000000000000001",
        "12345678901234567890.12345678901234567890", "0.12345678901234567890123",
        "1234567890123456789012e-10", "3.141592653589793238462643383279",
        "\"\"", "\"a\"", "\"é\"", "\"日本\"", "\"\\n\\t\\\\\\\"\\/\\b\\f\\r\"", "\"\\u0041\"",
        "\"\\u00e9\"", "\"\\uD83D\\uDE00\"", "\"\\ud83d\\ude00\"", "\"a\\u0000b\"", "\"\\u001f\"",
        "[]", "[1,2,3]", "[ 1 , [ 2 , [ ] ] ]", "{}", "{\"a\":1}", "{ \"a\" : [ true , null ] }",
        "{\"a\":1,\"a\":2}", "{\"b\":1,\"a\":2,\"b\":3}", "{\"\":0}", " \t\r\n1 \t\r\n",
        "{\"x\":{\"y\":{\"z\":[1,{\"w\":null}]}}}",
        // Errors.
        "", " ", "\n\n", "nul", "nulx", "nullx", "tru", "t", "f", "fals", "true false", "[1] x",
        "{not json", "[1,", "[1", "[", "{", "{\"a\"", "{\"a\":", "{\"a\":1", "{\"a\":1,", "[1,]",
        "[1 2]", "[,1]", "{\"a\":1,}", "{\"a\" 1}", "{\"a\":1 \"b\":2}", "{1:2}", "{,}",
        "{\"a\":1,2}", "{\"a\":1,,}", "[1,,2]", "]", "}", ":", ",", "01", "00", "-01", "-",
        "-a", "- 1", "1.", "1.e5", "1e", "1e+", "1e-", "1ea", ".5", "+1", "NaN", "Infinity",
        "-Infinity", "0x10", "1_000", "\"abc", "\"\\", "\"\\x\"", "\"\\u12\"", "\"\\u12", "\"\\uzzzz\"",
        "\"\\ud83d\"", "\"\\ud83dx\"", "\"\\ud83d\\n\"", "\"\\ud83d\\u0041\"", "\"\\ude00\"",
        "\"\\uD800\\uD800\"", "\"a\nb\"", "\"\t\"", "'a'", "{\n  \"x\":\n}", "[\n1,\n2,\n]",
        "{\"é\": [1,\n\"日本\" 2]}", "\u{feff}1", "[1]\n\n  x", "// comment\n1", "[1]/*x*/",
        "\"\u{7f}\"",
    ];
    for text in texts {
        body(&mut out, text.as_bytes());
    }
    // Not UTF-8, inside and outside strings.
    for bytes in [
        &b"\"\xff\""[..],
        b"\"a\xc3\"",
        b"\"\xed\xa0\x80\"",
        b"[\"\xe6\x97\xa5\", \xff]",
        b"\xff",
        b"\"\xc3\xa9\xff\"\n1",
        b"[\"\xff\",\n\"\xc3\" x]",
        b"{\"\xff\": 1}",
    ] {
        body(&mut out, bytes);
    }
    // Nesting up to and beyond the recursion limit (128).
    for depth in [126, 127, 128, 129] {
        body(&mut out, format!("{}{}", "[".repeat(depth), "]".repeat(depth)).as_bytes());
        body(&mut out, format!("{}1{}", "{\"a\":".repeat(depth), "}".repeat(depth)).as_bytes());
    }
    // Numbers as people and programs write them.
    let mut rng = SplitMix64(4);
    for _ in 0..4000 {
        body(&mut out, random_number(&mut rng).as_bytes());
    }
    // Every u64 near a power of two or ten, and the shortest text of random doubles.
    for i in 0..64 {
        for delta in [-1i128, 0, 1] {
            let n = (1i128 << i) + delta;
            body(&mut out, n.to_string().as_bytes());
            body(&mut out, (-n).to_string().as_bytes());
        }
    }
    for _ in 0..2000 {
        let value = f64::from_bits(rng.next());
        if value.is_finite() {
            body(&mut out, serde_json::to_string(&value).expect("JSON").as_bytes());
            body(&mut out, format!("{value:e}").as_bytes());
        }
    }

    for text in [
        "bad-id",
        "a",
        "67e55044-10b1-426f-9247-bb680e5fe0c8",
        "67E55044-10B1-426F-9247-BB680E5FE0C8",
        "67e5504410b1426f9247bb680e5fe0c8",
        "{67e55044-10b1-426f-9247-bb680e5fe0c8}",
        "urn:uuid:67e55044-10b1-426f-9247-bb680e5fe0c8",
        "00000000-0000-0000-0000-000000000000",
        "67e55044-10b1-426f-9247-bb680e5fe0c",
        "67e55044-10b1-426f-9247-bb680e5fe0c88",
        "67e55044-10b1-426f-9247bb680e5fe0c8",
        "67e55044-10b1-426f-9247-bb68-0e5fe0c8",
        "67e5504-410b1-426f-9247-bb680e5fe0c8",
        "67e55044-10b14-26f-9247-bb680e5fe0c8",
        "67e55044-10b1-426f9-247-bb680e5fe0c8",
        "67e55044-10b1-426f-92477-bb680e5fe0c",
        "67e55044x10b1-426f-9247-bb680e5fe0c8",
        "g7e55044-10b1-426f-9247-bb680e5fe0c8",
        "67e55044-10b1-426f-9247-bb680e5fe0cg",
        "67e5504410b1426f9247bb680e5fe0cg",
        "67e5504410b1426f9247bb680e5fe0c",
        "67e5504410b1426f9247bb680e5fe0c8a",
        "{67e55044-10b1-426f-9247-bb680e5fe0c8",
        "67e55044-10b1-426f-9247-bb680e5fe0c8}",
        "{67e55044-10b1-426f-9247-bb680e5fe0cg}",
        "{67e5504410b1426f9247bb680e5fe0c8}",
        "{}",
        "urn:uuid:67e55044-10b1-426f-9247-bb680e5fe0cg",
        "urn:uuid:67e5504410b1426f9247bb680e5fe0c8",
        "urn:uuid:",
        "urn:uuid:67e55044-10b1-426f-9247-bb680e5fe0c8a",
        "%7B67e55044-10b1-426f-9247-bb680e5fe0c8%7D",
        "67e55044-10b1-426f-9247-bb680e5fe0é",
        "日本",
        "----",
        "-------------------------------------",
        "0123456789abcdef0123456789abcdef0123456789abcdef",
        "123",
    ] {
        uuid(&mut out, text);
    }
    print!("{out}");
}
