//! Writes a corpus of values as `serde_json` serialises them, for the C++
//! serialiser's tests (`tests/json/serde_json_corpus.txt`).
//!
//! One case per line, `<kind> <input> <output>`:
//!
//! - `f <bits>`: an `f64`, by its bits in hexadecimal;
//! - `i <value>`, `u <value>`: an `i64` or `u64`;
//! - `s <utf8>`: a string, by its UTF-8 bytes in hexadecimal (`-` if empty);
//!
//! and the output is `serde_json::to_string` of it. Outputs never contain a
//! newline, since `serde_json` escapes control characters. Then:
//!
//! - `p <json> <pretty>`: a JSON text and what teta-wot's settings files make
//!   of it (`PrettyFormatter` with a four-space indent), both in hexadecimal.

use std::fmt::Write as _;

use serde::Serialize;
use serde_json::Value;
use serde_json::ser::{PrettyFormatter, Serializer};

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
}

fn float(out: &mut String, value: f64) {
    if value.is_finite() {
        let json = serde_json::to_string(&value).expect("a finite float");
        writeln!(out, "f {:016x} {json}", value.to_bits()).unwrap();
    }
}

fn string(out: &mut String, value: &str) {
    let hex: String = value.bytes().map(|b| format!("{b:02x}")).collect();
    let hex = if hex.is_empty() { "-".to_owned() } else { hex };
    let json = serde_json::to_string(value).expect("a string");
    writeln!(out, "s {hex} {json}").unwrap();
}

fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}

fn pretty(out: &mut String, text: &str) {
    let value: Value = serde_json::from_str(text).expect("valid JSON");
    let mut written = Vec::new();
    let formatter = PrettyFormatter::with_indent(b"    ");
    let mut serializer = Serializer::with_formatter(&mut written, formatter);
    value.serialize(&mut serializer).expect("serialisable");
    writeln!(out, "p {} {}", hex(text.as_bytes()), hex(&written)).unwrap();
}

fn main() {
    let mut out = String::new();
    let mut rng = SplitMix64(0x7E7A_0001);

    // Floats: special and boundary values.
    for value in [
        0.0, -0.0, 1.0, -1.0, 0.1, 0.2, 0.3, 1.5, 100.0, 1e-7, 1e-6, 1e-5, 1e-4, 123456.789,
        1e15, 1e16, 1e17, 1e20, 1e21, 1e22, 1e100, 1e300, 1.7976931348623157e308,
        5e-324, 2.2250738585072014e-308, 9007199254740992.0, 9007199254740993.0,
        4294967296.0, 0.000123, 1234567890123456789.0, -2.5e-8, 3.14159, 2.0 / 3.0,
    ] {
        float(&mut out, value);
    }
    // Powers of ten and their neighbours, where the layout changes.
    for exponent in -30..=30 {
        let value = 10f64.powi(exponent);
        for candidate in [value, value * 1.5, value * 9.999, f64::from_bits(value.to_bits() + 1)] {
            float(&mut out, candidate);
            float(&mut out, -candidate);
        }
    }
    // Decimals as people write them.
    for _ in 0..2000 {
        let mantissa = (rng.next() % 1_000_000) as f64;
        let scale = 10f64.powi((rng.next() % 24) as i32 - 12);
        float(&mut out, mantissa * scale);
    }
    // Arbitrary bit patterns.
    for _ in 0..5000 {
        float(&mut out, f64::from_bits(rng.next()));
    }

    // Integers.
    for value in [0i64, 1, -1, i64::MIN, i64::MAX, 1 << 53, -(1 << 53)] {
        writeln!(out, "i {value} {}", serde_json::to_string(&value).unwrap()).unwrap();
    }
    for value in [0u64, u64::MAX, 1 << 63] {
        writeln!(out, "u {value} {}", serde_json::to_string(&value).unwrap()).unwrap();
    }

    // Strings: every ASCII character, escapes, and some non-ASCII text.
    for byte in 0u8..0x80 {
        string(&mut out, &(byte as char).to_string());
    }
    for text in [
        "",
        "plain",
        "quote \" and backslash \\",
        "slash / stays",
        "tab\tnewline\ncarriage\rreturn",
        "\u{7f}\u{80}\u{9f}",
        "µm and °C",
        "日本語",
        "emoji 🔬",
        "\u{2028}\u{2029}",
        "\u{feff}bom",
    ] {
        string(&mut out, text);
    }

    // Indented, as settings files.
    for text in [
        "null",
        "1.0",
        "[]",
        "{}",
        "[1,2]",
        r#"{"speed":2000.0}"#,
        r#"{"z":1,"a":[],"m":{},"list":[1,[2,{"k":null}]],"s":"µ\n"}"#,
        r#"{"nested":{"deeper":{"deepest":[true,false]}}}"#,
    ] {
        pretty(&mut out, text);
    }

    print!("{out}");
}
