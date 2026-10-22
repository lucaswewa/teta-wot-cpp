//! The twin Things, written against teta-wot v0.1.0, ported from the `wot`
//! repository's `conformance/twin-server` (whose Python counterparts are in
//! its `conformance/reference/twins`).
//!
//! Each twin is written with the authoring macros (`#[derive(Thing)]`,
//! `#[thing_impl]`), with the Python docstrings as doc comments (a few with
//! trailing whitespace are `description` options instead, which doc comments
//! can't carry). The C++ twin server serves C++ ports of the same Things
//! under the same registry names, and the conformance harness compares the
//! two (ADR-0011).

use std::collections::BTreeMap;
use std::time::Duration;

use indexmap::IndexMap;
use schemars::JsonSchema;
use serde::{Deserialize, Serialize};
use serde_json::{Map, Value};
use teta_wot::prelude::*;
use teta_wot::server::{ThingRegistry, ThingServer, ThingServerBuilder};

/// The names the twins are served under, as in `twins.TWINS`.
pub const TWINS: [&str; 9] = [
    "mything",
    "broken",
    "counter",
    "types",
    "http_probe",
    "settings_probe",
    "ws_probe",
    "blob_probe",
    "ndarray_probe",
];

/// A server builder with the named twins.
pub fn server(names: &[&str]) -> anyhow::Result<ThingServerBuilder> {
    let mut builder = ThingServer::builder();
    for &name in names {
        builder = match name {
            "mything" => builder.thing(name, MyThing::default()),
            "broken" => builder.thing(name, ThingWithBrokenAffordances),
            "counter" => builder.thing(name, TestThing::default()),
            "types" => builder.thing(name, TypesThing::default()),
            "http_probe" => builder.thing(name, HttpProbe::default()),
            "settings_probe" => builder.thing(name, SettingsProbe::default()),
            "ws_probe" => builder.thing(name, ThingWithProperties::default()),
            "blob_probe" => builder.thing(name, BlobProbe::default()),
            "ndarray_probe" => builder.thing(name, NDArrayProbe::default()),
            "event_probe" => builder.thing(name, EventProbe::default()),
            other => anyhow::bail!("no twin named `{other}`"),
        };
    }
    Ok(builder)
}

/// The twins, registered under the import strings that LabThings
/// configuration files use for their Python counterparts, so that the same
/// files configure both servers.
pub fn registry() -> ThingRegistry {
    ThingRegistry::new()
        .register_as::<MyThing>(&[
            "labthings_fastapi.example_things:MyThing",
            "twins.mything:MyThing",
            "twins:MyThing",
        ])
        .register_as::<ThingWithBrokenAffordances>(&[
            "labthings_fastapi.example_things:ThingWithBrokenAffordances",
            "twins.mything:ThingWithBrokenAffordances",
            "twins:ThingWithBrokenAffordances",
        ])
        .register_as::<ThingThatCantStart>(&["labthings_fastapi.example_things:ThingThatCantStart"])
        .register_as::<TestThing>(&["twins.counter:TestThing", "twins:TestThing"])
        .register_as::<TypesThing>(&["twins.types_probe:TypesThing", "twins:TypesThing"])
        .register_as::<HttpProbe>(&["twins.http_probe:HttpProbe", "twins:HttpProbe"])
        .register_as::<SettingsProbe>(&[
            "twins.settings_probe:SettingsProbe",
            "twins:SettingsProbe",
        ])
        .register_as::<ThingWithProperties>(&[
            "twins.ws_probe:ThingWithProperties",
            "twins:ThingWithProperties",
        ])
        .register_as::<BlobProbe>(&["twins.blob_probe:BlobProbe", "twins:BlobProbe"])
        .register_as::<NDArrayProbe>(&["twins.ndarray_probe:NDArrayProbe", "twins:NDArrayProbe"])
        .register_as::<EventProbe>(&["twins.event_probe:EventProbe", "twins:EventProbe"])
}

// ---- MyThing (labthings_fastapi.example_things) ----------------------------

/// An example Thing with a few affordances.
#[derive(Thing)]
pub struct MyThing {
    /// A pointless counter
    #[property(readonly, default = 0)]
    counter: Prop<i64>,

    /// A pointless string for demo purposes.
    #[property(default = "Example")]
    foo: Prop<String>,
}

/// Represent the input of an action that has no required parameters.
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

#[thing_impl]
impl MyThing {
    /// Quite a complicated action.
    ///
    /// This action has lots of parameters and is designed to confuse my schema
    /// generator. I hope it doesn't!
    ///
    /// I might even use some Markdown here:
    ///
    /// * If this renders, it supports lists
    /// * With at least two items.
    ///
    /// There is also a parameter and return block to satisfy docstring validators.
    /// This may be preferable to annotations on the arguments.
    ///
    /// :param repeats: How many times to do it.
    /// :param undocumented: There's no description on this field's type hint.
    /// :param title: A human-readable title.
    /// :param attempts: A list of names of attempts.
    ///
    /// :return: A dictionary with strings as keys and values.
    #[action]
    async fn anaction(
        &self,
        #[param(description = "The number of times to try the action")] repeats: i64,
        undocumented: i64,
        #[param(default = "Untitled", description = "the title of the invocation")] title: String,
        #[param(
            default,
            description = "Names for each attempt - I suggest final, Final, FINAL."
        )]
        attempts: Option<Vec<String>>,
    ) -> Result<IndexMap<String, String>, ActionError> {
        let _ = (repeats, undocumented, title, attempts);
        self.counter.update(|c| *c += 1)?;
        Ok(IndexMap::from([(
            "end_result".to_owned(),
            "finished!!".to_owned(),
        )]))
    }

    /// Do something that returns a dict.
    ///
    /// :param extra_key: An additional key.
    /// :param extra_value: An additional value.
    /// :return: a dictionary.
    #[action]
    async fn make_a_dict(
        &self,
        #[param(default)] extra_key: Option<String>,
        #[param(default)] extra_value: Option<String>,
    ) -> IndexMap<String, Option<String>> {
        let mut out = IndexMap::from([("key".to_owned(), Some("value".to_owned()))]);
        if let Some(key) = extra_key {
            out.insert(key, extra_value);
        }
        out
    }

    /// Increment the counter property.
    ///
    /// This action doesn't do very much - all it does, in fact,
    /// is increment the counter (which may be read using the
    /// `counter` property).
    #[action]
    async fn increment_counter(&self) -> Result<(), ActionError> {
        self.counter.update(|c| *c += 1)?;
        Ok(())
    }

    /// Increment the counter slowly over a minute.
    ///
    /// :param increments: how many times to increment.
    /// :param delay: the wait time between increments.
    #[action]
    async fn slowly_increase_counter(
        &self,
        ctx: ActionCtx,
        #[param(default = 60)] increments: i64,
        #[param(default = 1.0)] delay: f64,
    ) -> Result<(), ActionError> {
        for _ in 0..increments {
            ctx.sleep(Duration::from_secs_f64(delay.max(0.0))).await?;
            self.counter.update(|c| *c += 1)?;
        }
        Ok(())
    }

    /// Do something that takes no arguments.
    #[action]
    async fn action_without_arguments(&self) {}

    /// Do something that takes \**kwargs.
    #[action(description = ":param \\**kwargs: Keyword arguments.\n        ")]
    async fn action_with_only_kwargs(&self, #[input] kwargs: EmptyInput) {
        let _ = kwargs;
    }
}

// ---- ThingWithBrokenAffordances --------------------------------------------

/// A Thing that raises exceptions in actions/properties.
#[derive(Thing)]
pub struct ThingWithBrokenAffordances;

#[thing_impl]
impl ThingWithBrokenAffordances {
    /// Raise an exception when the property is accessed.
    #[property(description = ":raise RuntimeError: every time.\n        ")]
    async fn broken_property(&self) -> Result<(), PropertyError> {
        Err(PropertyError::failed(anyhow::anyhow!(
            "This is a broken property"
        )))
    }

    /// Do something that raises an exception.
    #[action(description = ":raise RuntimeError: every time.\n        ")]
    async fn broken_action(&self) -> Result<(), ActionError> {
        Err(anyhow::anyhow!("This is a broken action").into())
    }
}

// ---- The quickstart counter ------------------------------------------------

/// A test thing with a counter property and a couple of actions.
#[derive(Thing)]
pub struct TestThing {
    /// A pointless counter.
    #[property(readonly, default = 0)]
    counter: Prop<i64>,
}

#[thing_impl]
impl TestThing {
    /// Increment the counter property.
    ///
    /// This action doesn't do very much - all it does, in fact,
    /// is increment the counter (which may be read using the
    /// `counter` property).
    #[action]
    async fn increment_counter(&self) -> Result<(), ActionError> {
        self.counter.update(|c| *c += 1)?;
        Ok(())
    }

    /// Increment the counter slowly over a minute.
    #[action]
    async fn slowly_increase_counter(&self, ctx: ActionCtx) -> Result<(), ActionError> {
        for _ in 0..60 {
            ctx.sleep(Duration::from_secs(1)).await?;
            self.counter.update(|c| *c += 1)?;
        }
        Ok(())
    }
}

// ---- TypesThing (conformance/reference/twins/types_probe.py) ---------------

/// A position in three dimensions.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize, JsonSchema)]
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

// No doc comments here: schemars would add them to the schema.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize, JsonSchema)]
#[serde(rename_all = "lowercase")]
enum Mode {
    Fast,
    Slow,
}

// The TD has the Python docstring as it is, with its indentation.
/// The `TypesThing` probe of DataSchema conversion.
#[derive(Thing)]
#[thing(
    description = "A Thing whose affordances use many different data types.\n\n    It is used to check how data types are described in the Thing Description.\n    "
)]
pub struct TypesThing {
    /// An integer between 1 and 10.
    #[property(default = 5, ge = 1, le = 10)]
    bounded_int: Prop<i64>,

    /// A number strictly between 0 and 1.
    #[property(default = 0.5, gt = 0, lt = 1)]
    open_interval: Prop<f64>,

    /// A multiple of five.
    #[property(default = 10, multiple_of = 5)]
    step: Prop<i64>,

    /// A short lower-case word.
    #[property(default = "abc", min_length = 1, max_length = 8, pattern = "^[a-z]+$")]
    word: Prop<String>,

    /// An integer that may be missing.
    #[property(default = None)]
    maybe_int: Prop<Option<i64>>,

    /// The speed mode.
    #[property(default = Mode::Fast)]
    mode: Prop<Mode>,

    /// The current position.
    #[property(default = Position { x: 1, y: 2, z: 0 })]
    position: Prop<Position>,

    /// Up to three tags.
    #[property(default = Vec::new(), max_length = 3)]
    tags: Prop<Vec<String>>,

    /// A pair of an integer and a string.
    #[property(default = (1, "a".to_owned()))]
    pair: Prop<(i64, String)>,

    /// A lookup table from names to numbers.
    #[property(default = BTreeMap::new())]
    lookup: Prop<BTreeMap<String, f64>>,
}

#[thing_impl]
impl TypesThing {
    /// Move to a target position.
    ///
    /// :param target: Where to move to.
    /// :param speed: How fast to move, or the default speed.
    /// :return: The final position.
    #[action]
    async fn r#move(
        &self,
        target: Position,
        #[param(default)] speed: Option<f64>,
    ) -> Result<Position, ActionError> {
        let _ = speed;
        self.position.set(target.clone())?;
        Ok(target)
    }

    /// Plan a route through some waypoints.
    ///
    /// :param waypoints: The waypoints to visit, in order.
    /// :param mode: The speed mode to use.
    /// :return: The position of each waypoint, by name.
    #[action]
    async fn plan_route(
        &self,
        waypoints: Vec<Waypoint>,
        #[param(default = Mode::Fast)] mode: Mode,
    ) -> IndexMap<String, Position> {
        let _ = mode;
        waypoints
            .into_iter()
            .map(|w| (w.name, w.position))
            .collect()
    }
}

// ---- HttpProbe (conformance/reference/twins/http_probe.py) -----------------

/// A Thing for probing the HTTP binding.
#[derive(Thing)]
pub struct HttpProbe {
    /// A writable integer.
    #[property(default = 0)]
    value: Prop<i64>,
}

#[thing_impl]
impl HttpProbe {
    /// Return zero.
    #[action]
    async fn zero(&self) -> i64 {
        0
    }

    /// Sleep, unless cancelled.
    ///
    /// :param seconds: How long to sleep.
    /// :return: "done".
    #[action]
    async fn slow(
        &self,
        ctx: ActionCtx,
        #[param(default = 10.0)] seconds: f64,
    ) -> Result<String, ActionError> {
        ctx.sleep(Duration::from_secs_f64(seconds.max(0.0))).await?;
        Ok("done".to_owned())
    }

    /// Fail in an anticipated way.
    #[action]
    async fn refuse(&self) -> Result<(), ActionError> {
        Err(ActionError::handled("Not now."))
    }
}

// ---- ThingThatCantStart (labthings_fastapi.example_things) -----------------

/// A Thing that raises an exception in __enter__.
#[derive(Thing)]
pub struct ThingThatCantStart;

#[thing_impl]
impl ThingThatCantStart {
    #[on_start]
    async fn enter(&self) -> anyhow::Result<()> {
        anyhow::bail!("This thing can't start")
    }
}

// ---- SettingsProbe (conformance/reference/twins/settings_probe.py) ---------

/// An offset and a gain.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize, JsonSchema)]
struct Calibration {
    offset: f64,
    gain: f64,
}

/// A Thing whose settings are saved to disk.
#[derive(Thing)]
pub struct SettingsProbe {
    /// A non-negative integer.
    #[setting(default = 0, ge = 0)]
    count: Prop<i64>,

    /// A number.
    #[setting(default = 0.5)]
    ratio: Prop<f64>,

    /// A label.
    #[setting(default = "probe")]
    label: Prop<String>,

    /// A flag.
    #[setting(default = false)]
    enabled: Prop<bool>,

    /// Some tags.
    #[setting(default = Vec::new())]
    tags: Prop<Vec<String>>,

    /// Named limits.
    #[setting(default = IndexMap::new())]
    limits: Prop<IndexMap<String, f64>>,

    /// The calibration.
    #[setting(default = Calibration { offset: 0.0, gain: 1.0 })]
    calibration: Prop<Calibration>,

    /// An integer that may be missing.
    #[setting(default = None)]
    maybe: Prop<Option<i64>>,

    #[thing(init = std::sync::Mutex::new(10.0))]
    exposure: std::sync::Mutex<f64>,
}

#[thing_impl]
impl SettingsProbe {
    /// The exposure time, in milliseconds.
    #[setting]
    async fn exposure(&self) -> f64 {
        *self.exposure.lock().unwrap_or_else(|e| e.into_inner())
    }

    #[setter(exposure)]
    async fn set_exposure(&self, value: f64) {
        *self.exposure.lock().unwrap_or_else(|e| e.into_inner()) = value;
    }
}

// ---- ThingWithProperties (conformance/reference/twins/ws_probe.py) ---------

/// A Thing with various different properties and actions.
///
/// This is used by the earlier tests, ensuring properties may
/// be observed.
#[derive(Thing)]
#[thing(
    description = "A Thing with various different properties and actions.\n\n    This is used by the earlier tests, ensuring properties may\n    be observed.\n    "
)]
pub struct ThingWithProperties {
    #[property(default = 0)]
    dataprop: Prop<i64>,
    #[thing(init = 0)]
    non_property: i64,
}

#[thing_impl]
impl ThingWithProperties {
    /// An undecorated function that returns an int.
    pub fn undecorated(&self) -> i64 {
        self.non_property
    }

    /// A property that isn't a LabThings property.
    pub fn python_property(&self) -> i64 {
        0
    }

    #[property]
    async fn funcprop(&self) -> i64 {
        0
    }

    #[setter(funcprop)]
    async fn set_funcprop(&self, _value: i64) {}

    /// Increment the data property.
    #[action]
    async fn increment_dataprop(&self) -> Result<Value, ActionError> {
        self.dataprop.update(|v| *v += 1)?;
        Ok(Value::Null)
    }

    /// Raise an exception to test for error status.
    #[action]
    async fn raise_error(&self) -> Result<Value, ActionError> {
        self.dataprop.update(|v| *v += 1)?;
        Err(anyhow::anyhow!("A deliberate failure.").into())
    }

    /// Increment the data property, then pretend to be cancelled.
    #[action]
    async fn cancel_myself(&self) -> Result<Value, ActionError> {
        self.dataprop.update(|v| *v += 1)?;
        Err(Cancelled.into())
    }
}

// ---- BlobProbe (conformance/reference/twins/blob_probe.py) ----------------

/// Enough of a JPEG for the stream's check of its start and end markers.
const FAKE_JPEG: &[u8] = b"\xff\xd8 not really a JPEG \xff\xd9";

/// Text of any subtype (the probe's `VagueTextBlob`).
pub struct VagueText;

impl teta_wot::blob::MediaType for VagueText {
    const MEDIA_TYPE: &'static str = "text/*";
    const TITLE: &'static str = "VagueTextBlob";
    const DESCRIPTION: Option<&'static str> = Some("This URL will download some vague text data.");
}

/// A Thing for probing Blobs and MJPEG streams.
#[derive(Thing)]
pub struct BlobProbe {
    #[stream]
    stream: MjpegStream,
}

#[thing_impl]
impl BlobProbe {
    /// Capture an image, held in memory.
    #[action]
    async fn capture(&self) -> Result<Blob<teta_wot::blob::Jpeg>, ActionError> {
        Ok(Blob::from_bytes(FAKE_JPEG))
    }

    /// Capture an image, saved in a temporary directory.
    #[action]
    async fn capture_to_file(&self) -> Result<Blob<teta_wot::blob::Jpeg>, ActionError> {
        let folder = tempfile::tempdir()?;
        std::fs::write(folder.path().join("image.jpg"), FAKE_JPEG)?;
        Ok(Blob::from_temporary_directory(folder, "image.jpg")?)
    }

    /// Return some text as a Blob.
    #[action]
    async fn text(&self) -> Result<Blob<teta_wot::blob::TextPlain>, ActionError> {
        Ok(Blob::from_bytes("Some text from the probe."))
    }

    /// Return CSV data as a vaguely typed Blob.
    #[action]
    async fn csv(&self) -> Result<Blob<VagueText>, ActionError> {
        Ok(Blob::from_bytes_as("a,b\n1,2\n", "text/csv")?)
    }

    /// Return two Blobs in a dictionary.
    #[action]
    async fn two_texts(&self) -> Result<IndexMap<String, Blob<teta_wot::blob::TextPlain>>, ActionError> {
        Ok(IndexMap::from([
            ("first".to_owned(), Blob::from_bytes("first")),
            ("second".to_owned(), Blob::from_bytes("second")),
        ]))
    }

    /// Return the Blob it is given.
    #[action]
    async fn passthrough(
        &self,
        blob: Blob<teta_wot::blob::TextPlain>,
    ) -> Result<Blob<teta_wot::blob::TextPlain>, ActionError> {
        Ok(blob)
    }

    /// Return the size of an image, in bytes.
    #[action]
    async fn jpeg_size(&self, image: Blob<teta_wot::blob::Jpeg>) -> Result<i64, ActionError> {
        Ok(image.bytes().await?.len() as i64)
    }

    /// Add frames to the stream, and return how many there were.
    #[action]
    async fn add_frames(&self, #[param(default = 3)] count: i64) -> Result<i64, ActionError> {
        for _ in 0..count {
            self.stream.add_frame(FAKE_JPEG)?;
        }
        Ok(count)
    }

    /// Stop the stream: its clients' responses end.
    #[action]
    async fn stop_stream(&self) {
        self.stream.stop();
    }

    /// Start the stream again, with an empty buffer.
    #[action]
    async fn restart_stream(&self) {
        self.stream.reset(None);
    }
}

// ---- NDArrayProbe (conformance/reference/twins/ndarray_probe.py) ----------

// The TD has the Python docstring as it is, with its indentation.
/// The `NDArrayProbe` probe of arrays.
#[derive(Thing)]
#[thing(
    description = "A Thing with array-valued affordances.\n\n    It is used to check how arrays are described and serialised.\n    "
)]
pub struct NDArrayProbe {
    #[thing(init = std::sync::Mutex::new(NdArray(teta_wot::ndarray::Array::zeros(vec![2, 3]))))]
    matrix: std::sync::Mutex<NdArray>,
}

#[thing_impl]
impl NDArrayProbe {
    /// A matrix of numbers, 2 by 3 to start with.
    #[property]
    async fn matrix(&self) -> NdArray {
        self.matrix
            .lock()
            .unwrap_or_else(|e| e.into_inner())
            .clone()
    }

    #[setter(matrix)]
    async fn set_matrix(&self, value: NdArray) {
        *self.matrix.lock().unwrap_or_else(|e| e.into_inner()) = value;
    }

    /// Double every element of an array.
    #[action]
    async fn double(&self, arr: NdArray) -> NdArray {
        NdArray(arr.0 * 2.0)
    }

    /// Return the integers from 0 to n - 1.
    #[action]
    async fn ramp(&self, n: i64) -> NdArray<i64> {
        NdArray(teta_wot::ndarray::Array::from_iter(0..n).into_dyn())
    }

    /// Add up every element of an array.
    #[action]
    async fn total(&self, arr: NdArray) -> f64 {
        arr.sum()
    }
}

// ---- EventProbe (teta-wot-cpp's own: none of the `wot` twins has events) ---

/// What happened.
#[derive(Debug, Clone, Serialize, JsonSchema)]
pub struct Happening {
    /// What it was.
    what: String,
    /// How many times it happened.
    times: i64,
}

/// A Thing whose actions emit events.
#[derive(Thing)]
pub struct EventProbe {
    /// Something happened.
    ///
    /// Emitted by `happen`.
    #[event]
    happened: Event<Happening>,

    /// A count, one event per step.
    #[event]
    counted: Event<i64>,

    /// A number, observable.
    #[property(default = 0)]
    level: Prop<i64>,
}

#[thing_impl]
impl EventProbe {
    /// Emit `happened`.
    #[action]
    async fn happen(&self, what: String) {
        self.happened.emit(Happening { what, times: 1 });
    }

    /// Emit `counted` from 1 to `to`.
    #[action]
    async fn count(&self, to: i64) {
        for n in 1..=to {
            self.counted.emit(n);
        }
    }
}
