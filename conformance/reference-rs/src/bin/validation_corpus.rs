//! Writes what teta-wot v0.1.0's validator makes of many inputs against many
//! DataSchemas, for the C++ validator's tests
//! (`tests/validate/validation_corpus.jsonl`).
//!
//! Lines of two kinds:
//!
//! - `{"schema_id": N, "name": ..., "schema": <DataSchema>}`: a schema;
//! - `{"schema_id": N, "input": "<JSON>", "result": "<JSON>"}`: an input
//!   (as serde_json text) and teta-wot's result, as serde_json text: either
//!   `{"ok": <coerced value>}` or `{"errors": [<issues>]}` (located under
//!   `["body"]`). Texts, so number kinds (`1`, `1.0`) are compared exactly.
//!
//! Inputs are a pool of edge cases (most from the `wot` repository's
//! fixtures captured from pydantic) and, for every schema, values from a
//! seeded generator: the randomised comparison of the plan's Phase 2.

use std::collections::BTreeMap;

use indexmap::IndexMap;
use schemars::JsonSchema;
use serde::{Deserialize, Serialize};
use serde_json::{Map, Value, json};
use teta_wot::LocItem;
use teta_wot::td::{Constraints, DataSchema};
use teta_wot::validate::SchemaValidator;

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

/// How the camera reads out the sensor.
#[derive(Serialize, Deserialize, JsonSchema)]
#[serde(rename_all = "snake_case")]
enum Readout {
    /// All rows at once.
    Global,
    /// Row by row.
    Rolling,
}

#[derive(Serialize, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
struct Strict {
    a: i64,
    #[serde(default)]
    b: Option<String>,
}

fn of<T: JsonSchema>() -> DataSchema {
    DataSchema::for_type::<T>().expect("convertible")
}

fn constrained<T: JsonSchema>(constraints: Constraints) -> DataSchema {
    let mut schema = of::<T>();
    constraints.apply(&mut schema).expect("suitable");
    schema
}

fn from_json(value: Value) -> DataSchema {
    serde_json::from_value(value).expect("a DataSchema")
}

fn schemas() -> Vec<(&'static str, DataSchema)> {
    vec![
        ("bool", of::<bool>()),
        ("i64", of::<i64>()),
        ("i8", of::<i8>()),
        ("u8", of::<u8>()),
        ("u64", of::<u64>()),
        ("f64", of::<f64>()),
        ("String", of::<String>()),
        ("()", of::<()>()),
        ("Value", of::<Value>()),
        ("i64 ge 1 le 10", constrained::<i64>(Constraints::new().ge(1).le(10))),
        ("f64 gt 0 lt 1", constrained::<f64>(Constraints::new().gt(0).lt(1))),
        ("i64 multiple_of 5", constrained::<i64>(Constraints::new().multiple_of(5))),
        ("f64 multiple_of 0.5 ge -1.5", constrained::<f64>(Constraints::new().multiple_of(0.5).ge(-1.5))),
        ("f64 le 1e20", constrained::<f64>(Constraints::new().le(1e20))),
        ("String 1..8 ^[a-z]+$", constrained::<String>(Constraints::new().min_length(1).max_length(8).pattern("^[a-z]+$"))),
        ("String pattern \\d", constrained::<String>(Constraints::new().pattern(r"\d{2}"))),
        ("Option<i64>", of::<Option<i64>>()),
        ("Option<i64> ge 0", constrained::<Option<i64>>(Constraints::new().ge(0))),
        ("Option<String>", of::<Option<String>>()),
        ("Option<Position>", of::<Option<Position>>()),
        ("Vec<i64>", of::<Vec<i64>>()),
        ("Vec<String> max 3", constrained::<Vec<String>>(Constraints::new().max_length(3))),
        ("Vec<f64> min 2", constrained::<Vec<f64>>(Constraints::new().min_length(2))),
        ("(i64, String)", of::<(i64, String)>()),
        ("[i64; 3]", of::<[i64; 3]>()),
        ("BTreeMap<String, i64>", of::<BTreeMap<String, i64>>()),
        ("IndexMap<String, f64>", of::<IndexMap<String, f64>>()),
        ("Map<String, Value>", of::<Map<String, Value>>()),
        ("Position", of::<Position>()),
        ("Vec<Position>", of::<Vec<Position>>()),
        ("Waypoint", of::<Waypoint>()),
        ("Strict", of::<Strict>()),
        ("Mode", of::<Mode>()),
        ("Readout", of::<Readout>()),
        ("NoInput", of::<teta_wot::NoInput>()),
        ("literal 1 or 2", from_json(json!({"enum": [1, 2]}))),
        ("literal mixed", from_json(json!({"enum": [1, "a", null, true, 2.5]}))),
        ("const 3", from_json(json!({"const": 3}))),
        ("int or str", from_json(json!({"oneOf": [{"type": "integer"}, {"type": "string"}]}))),
        ("float or bool or str", from_json(json!({"oneOf": [{"type": "number"}, {"type": "boolean"}, {"type": "string"}]}))),
        ("models union", from_json(json!({"oneOf": [
            {"type": "object", "title": "A", "properties": {"a": {"type": "integer"}}, "required": ["a"]},
            {"type": "object", "properties": {"b": {"type": "string"}}, "required": ["b"]},
            {"type": "array", "items": {"type": "integer"}},
            {}
        ]}))),
        ("nested numbers", from_json(json!({"oneOf": [
            {"type": "number"},
            {"type": "array", "items": {"type": "number"}},
            {"type": "array", "items": {"type": "array", "items": {"type": "number"}}}
        ]}))),
        ("map of schema", from_json(json!({"type": "object", "additionalProperties": {"type": "integer", "minimum": 0}}))),
        ("model, extra true", from_json(json!({"type": "object", "properties": {"x": {"type": "integer"}}, "additionalProperties": true}))),
        ("model, extra schema", from_json(json!({"type": "object", "properties": {"x": {"type": "integer"}}, "required": ["x"], "additionalProperties": {"type": "boolean"}}))),
        ("array without items", from_json(json!({"type": "array", "minItems": 1}))),
        ("object without properties", from_json(json!({"type": "object"}))),
    ]
}

fn pool() -> Vec<Value> {
    let mut values = vec![
        Value::Null,
        json!(true),
        json!(false),
        json!(0),
        json!(1),
        json!(-1),
        json!(2),
        json!(3),
        json!(5),
        json!(7),
        json!(10),
        json!(11),
        json!(1.0),
        json!(0.0),
        json!(2.0),
        json!(-2.0),
        json!(1.5),
        json!(0.5),
        json!(-1.5),
        json!(-2.5),
        json!(2.5),
        json!(1e20),
        json!(1e21),
        json!(1e-7),
        json!(255),
        json!(256),
        json!(-129),
        json!(u64::MAX),
        json!(i64::MIN),
        json!(i64::MAX),
        json!(9.223372036854775e18),
        json!(0.30000000000000004),
    ];
    for s in [
        "", " ", "1", "2", "-1", "0", " +1_234.00 ", "-002", "1e3", "2.", "2.01", "0x10", "\u{ff11}\u{ff12}",
        "18446744073709551616", "9223372036854775808", "inf", "-inf", "nan", "Infinity", "1_000.5", "_1",
        "1__0", "1_", "+", "1.", ".5", "1e", "1e+2", "1E-2", "abc", "ABC", "fast", "FAST", "slow", "global",
        "yes", "No", "off", "ON", "T", "f", "y", "n", "true", "False", "2.5", "a", "ab", "abcdefghi", "été",
        "日本語", "12", "x12y", " 1", "1 ", "\u{a0}7\u{2003}", "\t3\n",
    ] {
        values.push(json!(s));
    }
    for v in [
        json!([]),
        json!([1]),
        json!([1, "2"]),
        json!([1, "a"]),
        json!(["a"]),
        json!(["a", "b", "c", "d"]),
        json!([1, 2, 3]),
        json!([1, 2, 3, 4]),
        json!([1.5, 2]),
        json!([[1, 2], [3]]),
        json!([[1.0, 2.0], [3.0, 4.0]]),
        json!([[1, "x"]]),
        json!([null]),
        json!([{"x": 1}, {"x": "q"}]),
        json!([{}]),
        json!({}),
        json!({"x": 1}),
        json!({"x": "1"}),
        json!({"y": 1}),
        json!({"x": 1, "y": 2, "z": "3"}),
        json!({"x": 1, "y": 2, "extra": true}),
        json!({"a": "1"}),
        json!({"a": "x"}),
        json!({"a": 1, "b": null}),
        json!({"a": 1, "c": 2}),
        json!({"b": "s"}),
        json!({"k": -1, "m": "2"}),
        json!({"name": "w", "position": {"x": 1, "y": 2}}),
        json!({"name": "w", "position": {"x": "a"}, "fallback": {"x": 1, "y": 1}}),
        json!({"name": 3}),
    ] {
        values.push(v);
    }
    values
}

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

    fn value(&mut self, depth: u32) -> Value {
        const KEYS: [&str; 9] = ["x", "y", "z", "a", "b", "name", "position", "extra", "fallback"];
        const STRINGS: [&str; 12] = ["1", "2.5", "-3", "abc", "yes", "fast", "Rolling", "", " 4 ", "1_0", "x", "nan"];
        match self.below(if depth == 0 { 7 } else { 9 }) {
            0 => Value::Null,
            1 => json!(self.below(2) == 1),
            2 => json!(self.below(21) as i64 - 10),
            3 => json!((self.below(41) as f64 - 20.0) / 4.0),
            4 => json!(STRINGS[self.below(STRINGS.len() as u64) as usize]),
            5 => json!(self.next() as i64),
            6 => json!(f64::from_bits(self.next() >> 2)),
            7 => {
                let n = self.below(4);
                Value::Array((0..n).map(|_| self.value(depth - 1)).collect())
            }
            _ => {
                let n = self.below(4);
                let mut map = Map::new();
                for _ in 0..n {
                    let key = KEYS[self.below(KEYS.len() as u64) as usize];
                    map.insert(key.to_owned(), self.value(depth - 1));
                }
                Value::Object(map)
            }
        }
    }
}

fn result(validator: &SchemaValidator, input: &Value) -> String {
    let outcome = match validator.validate(input, &[LocItem::from("body")]) {
        Ok(value) => json!({"ok": value}),
        Err(error) => json!({"errors": error.issues}),
    };
    serde_json::to_string(&outcome).expect("serialisable")
}

fn main() {
    let mut out = String::new();
    let mut rng = SplitMix64(0x7E7A_0002);
    let pool = pool();
    for (id, (name, schema)) in schemas().into_iter().enumerate() {
        let validator = SchemaValidator::new(schema.clone()).expect("valid patterns");
        let line = json!({"schema_id": id, "name": name, "schema": schema});
        out.push_str(&serde_json::to_string(&line).unwrap());
        out.push('\n');
        let random: Vec<Value> = (0..60).map(|_| rng.value(3)).collect();
        for input in pool.iter().chain(random.iter()) {
            // Floats that aren't finite can't be written as JSON.
            let Ok(text) = serde_json::to_string(input) else { continue };
            let line = json!({"schema_id": id, "input": text, "result": result(&validator, input)});
            out.push_str(&serde_json::to_string(&line).unwrap());
            out.push('\n');
        }
    }
    print!("{out}");
}
