//! Writes what teta-wot v0.1.0 makes of Rust types' JSON Schemas, for the C++
//! schema tests (`tests/td/schema_corpus.jsonl`).
//!
//! One JSON object per line, for one Rust type:
//!
//! - `type`: the Rust type, as written here;
//! - `json_schema`: what schemars generates for it (draft 2020-12, the
//!   generator `DataSchema::for_type` uses), with its root title;
//! - `data_schema`: `DataSchema::for_type`'s result, as teta-wot writes it
//!   (serde_json text, so the C++ side compares it byte for byte).
//!
//! The C++ tests check both halves: converting `json_schema` with the port
//! of `convert` gives `data_schema`, and the C++ type standing for `type`
//! (`tests/td/schema_corpus_test.cpp`) gives `data_schema` too.

use std::collections::{BTreeMap, HashMap};

use indexmap::IndexMap;
use schemars::JsonSchema;
use schemars::generate::SchemaSettings;
use serde::{Deserialize, Serialize};
use serde_json::{Map, Value, json};
use teta_wot::td::DataSchema;

/// A position in three dimensions.
#[derive(Serialize, Deserialize, JsonSchema)]
struct Position {
    x: i64,
    y: i64,
    #[serde(default)]
    z: i64,
}

/// A named position, optionally with a fallback.
#[derive(Serialize, Deserialize, JsonSchema)]
struct Waypoint {
    name: String,
    position: Position,
    #[serde(default)]
    fallback: Option<Position>,
}

#[derive(Serialize, Deserialize, JsonSchema)]
#[serde(rename_all = "lowercase")]
enum Mode {
    Fast,
    Slow,
}

/// An offset and a gain.
#[derive(Serialize, Deserialize, JsonSchema)]
struct Calibration {
    offset: f64,
    gain: f64,
}

/// A struct whose fields are documented.
#[derive(Serialize, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
struct Documented {
    /// How many.
    count: u32,
    /// A label, if any.
    label: Option<String>,
    /// Defaults to three.
    #[serde(default = "three")]
    level: i64,
    #[serde(default)]
    flags: Vec<bool>,
}

fn three() -> i64 {
    3
}

/// Nested structs, to exercise references inside references.
#[derive(Serialize, Deserialize, JsonSchema)]
struct Route {
    start: Waypoint,
    stops: Vec<Waypoint>,
    calibration: Option<Calibration>,
    mode: Mode,
}

#[derive(Serialize, Deserialize, JsonSchema)]
struct Undocumented {
    a: f32,
    b: Option<u8>,
}

/// The input of an action that has no required parameters (mything's).
///
/// This may be either a dictionary or ``None``.
#[derive(Serialize, Deserialize, JsonSchema)]
struct EmptyInput(Option<EmptyObject>);

/// A model representing an object with no required keys.
#[derive(Serialize, Deserialize, JsonSchema)]
#[schemars(extend("properties" = {}))]
struct EmptyObject {
    #[serde(flatten)]
    extra: Map<String, Value>,
}

fn case<T: JsonSchema>(out: &mut String, name: &str) {
    let json_schema = SchemaSettings::draft2020_12()
        .into_generator()
        .into_root_schema_for::<T>()
        .to_value();
    let data_schema = DataSchema::for_type::<T>().expect("convertible");
    let line = json!({
        "type": name,
        "json_schema": json_schema,
        "data_schema": serde_json::to_string(&data_schema).expect("serialisable"),
    });
    out.push_str(&serde_json::to_string(&line).expect("serialisable"));
    out.push('\n');
}

macro_rules! cases {
    ($out:expr; $($t:ty),* $(,)?) => {
        $( case::<$t>($out, stringify!($t)); )*
    };
}

fn main() {
    let mut out = String::new();
    cases!(&mut out;
        // Scalars.
        bool, i8, i16, i32, i64, u8, u16, u32, u64, f32, f64, String, (), char,
        Value,
        // Optional values.
        Option<bool>, Option<i64>, Option<u8>, Option<f64>, Option<String>,
        Option<Vec<String>>, Option<Position>, Option<Mode>, Option<Value>,
        Option<Option<i64>>,
        // Sequences, tuples and arrays.
        Vec<i64>, Vec<String>, Vec<Position>, Vec<Vec<f64>>, Vec<Option<i64>>,
        (i64, String), (f64, f64, f64), [i64; 3], (bool,),
        // Maps.
        BTreeMap<String, f64>, HashMap<String, i64>, IndexMap<String, String>,
        IndexMap<String, Option<String>>, IndexMap<String, Position>,
        Map<String, Value>,
        // Structs and enums.
        Position, Waypoint, Mode, Calibration, Documented, Route, Undocumented,
        EmptyInput, teta_wot::NoInput,
    );
    print!("{out}");
}
