// A simulated microscope: the capstone example.
//
// Three Things, composed with slots:
//
// - `Stage` moves the slide in x, y and z at a speed that is a setting, one
//   small step at a time, so moves take time and can be cancelled. It
//   announces each arrival with an event.
// - `Camera` looks at the slide through the stage (a slot). A thread renders
//   what it sees into an MJPEG preview: a field of cells, blurred by the
//   distance from the focal plane. `capture` returns a JPEG Blob,
//   `sharpness` measures the image, and `exposure` is a setting.
// - `Autofocus` sweeps the stage's z through the camera's sharpness (two
//   slots), moves to the sharpest position, and returns the focus curve as
//   an array.
//
// `registry()` names them as a configuration file does, and `config` is such
// a file. The C++ port of teta-wot's `simulated-microscope` library, shared
// by the example's program and `microscope-service`.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <teta/wot.hpp>
#include <thread>

namespace microscope {

namespace tw = teta::wot;

/// The z position at which the slide is in focus.
inline constexpr std::int64_t focus_z = 250;

/// A configuration file serving the microscope, with the Things' import
/// strings and `./settings/microscope` as the settings folder: the text of
/// `microscope.json`.
extern const std::string_view config;

/// The Thing types, under the import strings of `config`.
tw::ThingRegistry registry();

// ---- The stage ---------------------------------------------------------------

/// A position of the stage, in steps.
struct Position {
    std::int64_t x = 0;
    std::int64_t y = 0;
    std::int64_t z = 0;

    static void describe(tw::StructInfo<Position>& s) {
        s.description("A position of the stage, in steps.");
        s.field("x").description("Left to right.");
        s.field("y").description("Front to back.");
        s.field("z").description("Up and down: the focus.");
    }
    bool operator==(const Position&) const = default;
};

/// As Rust's `Debug` writes it: `Position { x: 1, y: 2, z: 3 }`.
std::string debug(const Position& position);

/// A motorised stage, simulated.
///
/// It moves the slide under the objective in x and y, and the focus in z.
class Stage {
  public:
    /// Where the stage is, in steps.
    tw::Prop<Position> position;
    /// How fast the stage moves, in steps per second.
    tw::Prop<double> speed{2000.0};
    /// The stage arrived where it was sent.
    tw::Event<Position> arrived;

    /// The stage's position now.
    Position now() const { return position.get(); }

    /// Move to a position.
    Position move_to(std::int64_t x, std::int64_t y, std::int64_t z);
    /// Move by a distance from where the stage is.
    Position move_by(std::int64_t dx, std::int64_t dy, std::int64_t dz);
    /// Return to the origin.
    Position home();

    static void define(tw::ThingSpec<Stage>& t);

  private:
    Position travel(Position target);
};

// ---- The camera --------------------------------------------------------------

/// What a capture was.
struct Capture {
    Position position;
    double exposure = 0;
    std::uint64_t bytes = 0;

    static void describe(tw::StructInfo<Capture>& s) {
        s.description("What a capture was.");
        s.field("position").description("Where the stage was.");
        s.field("exposure").description("The exposure, in milliseconds.");
        s.field("bytes").description("The size of the JPEG, in bytes.");
    }
};

/// A camera looking at the slide on the stage, simulated.
///
/// It sees a field of cells, sharp at the focal plane and blurred away from
/// it, brighter with a longer exposure.
class Camera {
  public:
    /// The stage the slide is on.
    tw::Slot<Stage> stage;
    /// The live preview, as MJPEG.
    tw::MjpegStream preview;
    /// The exposure time, in milliseconds.
    tw::Prop<double> exposure{20.0};
    /// Frames per second of the preview.
    tw::Prop<double> fps{10.0};
    /// An image was captured.
    tw::Event<Capture> captured;

    /// Starts the thread that feeds the preview.
    void start_preview();
    /// Stops the preview's thread, and ends the clients' streams.
    void stop_preview();

    /// How sharp the image is: the mean squared difference between
    /// neighbouring pixels, which blurring reduces.
    double sharpness() const { return measure(); }
    /// Capture an image, as a JPEG.
    tw::Blob<tw::blob::Jpeg> capture();

    /// The sharpness of the image the camera sees now.
    double measure() const;

    static void define(tw::ThingSpec<Camera>& t);

  private:
    Position stage_now() const { return stage->thing().now(); }

    std::jthread thread_;
};

// ---- The autofocus -----------------------------------------------------------

/// What an autofocus found.
struct Focus {
    std::int64_t z = 0;
    double sharpness = 0;
    tw::NdArray<double, 2> curve;

    static void describe(tw::StructInfo<Focus>& s) {
        s.description("What an autofocus found.");
        s.field("z").description("The sharpest z.");
        s.field("sharpness").description("Its sharpness.");
        s.field("curve").description("The sweep: one row per position, `[z, sharpness]`.");
    }
};

/// Focuses the camera by moving the stage.
class Autofocus {
  public:
    /// The stage that moves the focus.
    tw::Slot<Stage> stage;
    /// The camera whose sharpness is measured.
    tw::Slot<Camera> camera;
    /// The last focus found, if any.
    tw::Prop<std::optional<std::int64_t>> last_focus;
    /// A focus was found.
    tw::Event<std::int64_t> focused;

    /// Find the focus.
    ///
    /// The stage sweeps z over `range` steps around where it is, in `steps`
    /// moves; the camera measures the sharpness at each, and the stage
    /// returns to the sharpest.
    Focus run(tw::ActionCtx& ctx, std::int64_t range, std::int64_t steps);

    static void define(tw::ThingSpec<Autofocus>& t);
};

}  // namespace microscope
