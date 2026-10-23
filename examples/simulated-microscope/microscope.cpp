// The simulated microscope's Things, and what the camera sees. The image
// arithmetic is teta-wot's, operation for operation, so that sharpnesses and
// focus curves are the same numbers.

#include "microscope.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <format>
#include <limits>
#include <mutex>
#include <stop_token>
#include <utility>
#include <vector>

#include "../common/jpeg.hpp"
#include "microscope_config.hpp"

namespace microscope {

const std::string_view config = microscope_json;

tw::ThingRegistry registry() {
    tw::ThingRegistry registry;
    registry.add<Stage>("microscope.stage:Stage")
        .add<Camera>("microscope.camera:Camera")
        .add<Autofocus>("microscope.autofocus:Autofocus");
    return registry;
}

std::string debug(const Position& position) {
    return std::format("Position {{ x: {}, y: {}, z: {} }}", position.x, position.y, position.z);
}

namespace {

/// How far out of focus the image is fully blurred, in steps.
constexpr double depth_of_field = 40.0;

/// The preview's size, in pixels.
constexpr std::size_t width = 320;
constexpr std::size_t height = 240;

/// Stage steps per pixel.
constexpr std::int64_t steps_per_pixel = 2;

/// A greyscale image, row by row.
using Image = std::vector<double>;

std::int64_t div_euclid(std::int64_t a, std::int64_t b) {
    const std::int64_t q = a / b;
    return a % b < 0 ? q - 1 : q;
}

std::int64_t rem_euclid(std::int64_t a, std::int64_t b) {
    const std::int64_t r = a % b;
    return r < 0 ? r + b : r;
}

std::uint64_t hash(std::int64_t x, std::int64_t y) {
    std::uint64_t h = static_cast<std::uint64_t>(x) * 0x9e37'79b9'7f4a'7c15ULL ^
                      static_cast<std::uint64_t>(y) * 0xc2b2'ae3d'27d4'eb4fULL;
    h ^= h >> 29;
    h *= 0xbf58'476d'1ce4'e5b9ULL;
    return h ^ (h >> 32);
}

/// The slide at a pixel: cells (bright discs with a dark nucleus) on a dim
/// background, one per 32-pixel square, placed by a hash.
double slide(std::int64_t x, std::int64_t y) {
    const std::int64_t cell_x = div_euclid(x, 32);
    const std::int64_t cell_y = div_euclid(y, 32);
    double brightness = 0.1;
    constexpr std::array<std::pair<std::int64_t, std::int64_t>, 9> neighbours = {
        {{0, 0}, {-1, 0}, {0, -1}, {-1, -1}, {1, 0}, {0, 1}, {1, 1}, {1, -1}, {-1, 1}}};
    for (const auto& [dx, dy] : neighbours) {
        const std::int64_t cx = cell_x + dx;
        const std::int64_t cy = cell_y + dy;
        const std::uint64_t h = hash(rem_euclid(cx, 32), rem_euclid(cy, 32));
        const std::int64_t centre_x = cx * 32 + 8 + static_cast<std::int64_t>(h % 16);
        const std::int64_t centre_y = cy * 32 + 8 + static_cast<std::int64_t>((h >> 8) % 16);
        const double radius = 6.0 + static_cast<double>((h >> 16) % 6);
        const double distance =
            std::sqrt(static_cast<double>((x - centre_x) * (x - centre_x) + (y - centre_y) * (y - centre_y)));
        if (distance < radius / 3.0) {
            brightness = std::max(brightness, 0.35);
        } else if (distance < radius) {
            brightness = std::max(brightness, 0.8);
        }
    }
    return brightness;
}

/// A box blur of `radius` pixels, horizontally then vertically, with a
/// running sum so that it costs the same whatever the radius.
void box_blur(Image& pixels, std::size_t radius) {
    if (radius == 0) {
        return;
    }
    struct Pass {
        std::size_t length, stride, lines, step;
    };
    Image out(pixels.size());
    for (const Pass pass : {Pass{width, 1, height, width}, Pass{height, width, width, 1}}) {
        for (std::size_t line = 0; line < pass.lines; ++line) {
            const auto at = [&](std::size_t i) { return line * pass.step + i * pass.stride; };
            double sum = 0;
            for (std::size_t j = 0; j <= std::min(radius, pass.length - 1); ++j) {
                sum += pixels[at(j)];
            }
            for (std::size_t i = 0; i < pass.length; ++i) {
                const std::size_t from = i > radius ? i - radius : 0;
                const std::size_t to = std::min(i + radius, pass.length - 1);
                out[at(i)] = sum / static_cast<double>(to - from + 1);
                // Slide the window: add the next pixel, drop the oldest.
                if (i + radius + 1 < pass.length) {
                    sum += pixels[at(i + radius + 1)];
                }
                if (i >= radius) {
                    sum -= pixels[at(i - radius)];
                }
            }
        }
        pixels = out;
    }
}

/// What the camera sees with the stage at `position`: cells on a slide, in
/// a pattern that repeats every 1024 pixels, blurred by the distance from
/// the focal plane.
Image render(Position position, double exposure) {
    const std::int64_t left = position.x / steps_per_pixel;
    const std::int64_t top = position.y / steps_per_pixel;
    Image pixels(width * height);
    for (std::size_t row = 0; row < height; ++row) {
        for (std::size_t column = 0; column < width; ++column) {
            pixels[row * width + column] =
                slide(left + static_cast<std::int64_t>(column), top + static_cast<std::int64_t>(row));
        }
    }
    const auto radius = static_cast<std::size_t>(
        std::round(static_cast<double>(std::abs(position.z - focus_z)) / depth_of_field * 3.0));
    for (int pass = 0; pass < 3; ++pass) {
        box_blur(pixels, radius);
    }
    const double gain = std::clamp(exposure / 20.0, 0.05, 10.0);
    for (auto& pixel : pixels) {
        pixel = std::min(pixel * gain, 1.0);
    }
    return pixels;
}

double sharpness_of(const Image& pixels) {
    double total = 0.0;
    for (std::size_t row = 0; row < height; ++row) {
        for (std::size_t column = 1; column < width; ++column) {
            const std::size_t i = row * width + column;
            const double difference = pixels[i] - pixels[i - 1];
            total += difference * difference;
        }
    }
    return total / static_cast<double>(height * (width - 1)) * 1000.0;
}

std::string encode(const Image& image) {
    std::vector<std::uint8_t> grey(image.size());
    std::transform(image.begin(), image.end(), grey.begin(),
                   [](double pixel) { return static_cast<std::uint8_t>(pixel * 255.0); });
    return examples::jpeg::encode_grey(grey, static_cast<int>(width), static_cast<int>(height), 85);
}

}  // namespace

// ---- The stage ---------------------------------------------------------------

Position Stage::move_to(std::int64_t x, std::int64_t y, std::int64_t z) {
    return travel({x, y, z});
}

Position Stage::move_by(std::int64_t dx, std::int64_t dy, std::int64_t dz) {
    const Position here = now();
    return travel({here.x + dx, here.y + dy, here.z + dz});
}

Position Stage::home() {
    return travel({});
}

Position Stage::travel(Position target) {
    constexpr std::chrono::milliseconds tick(20);
    const Position start = now();
    const std::int64_t distance =
        std::max({std::abs(target.x - start.x), std::abs(target.y - start.y), std::abs(target.z - start.z)});
    const double steps_per_tick = std::max(speed.get() * std::chrono::duration<double>(tick).count(), 1.0);
    const auto ticks = static_cast<std::int64_t>(std::ceil(static_cast<double>(distance) / steps_per_tick));
    tw::log::info("moving from {} to {} in {} ticks", debug(start), debug(target), ticks);
    for (std::int64_t step = 1; step <= ticks; ++step) {
        tw::cancellable_sleep(tick);
        const auto along = [&](std::int64_t from, std::int64_t to) {
            return from + (to - from) * step / ticks;
        };
        position.set({along(start.x, target.x), along(start.y, target.y), along(start.z, target.z)});
    }
    position.set(target);
    arrived.emit(target);
    return target;
}

void Stage::define(tw::ThingSpec<Stage>& t) {
    t.doc(
        "A motorised stage, simulated.\n\nIt moves the slide under the objective in x and y, and the focus "
        "in z.");
    t.property("position", &Stage::position).read_only().doc("Where the stage is, in steps.");
    t.property("speed", &Stage::speed)
        .setting()
        .doc("How fast the stage moves, in steps per second.")
        .gt(0)
        .le(100000);
    t.event("arrived", &Stage::arrived).doc("The stage arrived where it was sent.");
    t.action<"x", "y", "z">("move_to", &Stage::move_to)
        .doc(
            "Move to a position.\n\nThe stage moves in small steps at its speed, and stops where it is "
            "if\nthe move "
            "is cancelled.")
        .global_lock(false);
    auto& move_by = t.action<"dx", "dy", "dz">("move_by", &Stage::move_by);
    move_by.doc("Move by a distance from where the stage is.").global_lock(false);
    move_by.param("dx").default_value(0);
    move_by.param("dy").default_value(0);
    move_by.param("dz").default_value(0);
    t.action("home", &Stage::home).doc("Return to the origin.").global_lock(false);
}

// ---- The camera --------------------------------------------------------------

void Camera::start_preview() {
    thread_ = std::jthread([this, stage = *stage, preview = preview](std::stop_token stop) {
        std::mutex mutex;
        std::condition_variable_any wake;
        while (!stop.stop_requested()) {
            try {
                (void)preview.add_frame(encode(render(stage.thing().now(), exposure.get())));
            } catch (const std::exception& error) {
                tw::log::error("the camera failed: {}", error.what());
            }
            std::unique_lock lock(mutex);
            wake.wait_for(lock, stop, std::chrono::duration<double>(1.0 / fps.get()), [] { return false; });
        }
    });
}

void Camera::stop_preview() {
    thread_ = {};
    preview.stop();
}

tw::Blob<tw::blob::Jpeg> Camera::capture() {
    const Position position = stage_now();
    const double exposure_now = exposure.get();
    auto jpeg = encode(render(position, exposure_now));
    captured.emit({position, exposure_now, jpeg.size()});
    return tw::Blob<tw::blob::Jpeg>::from_bytes(std::move(jpeg));
}

double Camera::measure() const {
    return sharpness_of(render(stage_now(), exposure.get()));
}

void Camera::define(tw::ThingSpec<Camera>& t) {
    t.doc(
        "A camera looking at the slide on the stage, simulated.\n\nIt sees a field of cells, sharp at the "
        "focal "
        "plane and blurred away\nfrom it, brighter with a longer exposure.");
    t.slot("stage", &Camera::stage);
    t.stream("preview", &Camera::preview);
    t.property("exposure", &Camera::exposure)
        .setting()
        .doc("The exposure time, in milliseconds.")
        .gt(0)
        .le(1000);
    t.property("fps", &Camera::fps).doc("Frames per second of the preview.").gt(0).le(30);
    t.event("captured", &Camera::captured).doc("An image was captured.");
    t.on_start(&Camera::start_preview);
    t.on_stop(&Camera::stop_preview);
    t.property("sharpness", &Camera::sharpness)
        .doc(
            "How sharp the image is: the mean squared difference between\nneighbouring pixels, which "
            "blurring "
            "reduces.");
    t.action("capture", &Camera::capture).doc("Capture an image, as a JPEG.");
}

// ---- The autofocus -----------------------------------------------------------

Focus Autofocus::run(tw::ActionCtx& ctx, std::int64_t range, std::int64_t steps) {
    steps = std::clamp<std::int64_t>(steps, 2, 1000);
    const Position here = stage->thing().now();
    auto curve = tw::NdArray<double, 2>::zeros({static_cast<std::size_t>(steps), 2});
    double best_sharpness = std::numeric_limits<double>::lowest();
    std::int64_t best_z = here.z;
    for (std::int64_t i = 0; i < steps; ++i) {
        ctx.check_cancelled();
        const std::int64_t z = here.z - range / 2 + range * i / (steps - 1);
        stage->call(&Stage::move_to, here.x, here.y, z);
        const double sharpness = camera->thing().measure();
        tw::log::info("z = {}: sharpness {:.3f}", z, sharpness);
        curve.at({static_cast<std::size_t>(i), 0}) = static_cast<double>(z);
        curve.at({static_cast<std::size_t>(i), 1}) = sharpness;
        if (sharpness > best_sharpness) {
            best_sharpness = sharpness;
            best_z = z;
        }
    }
    stage->call(&Stage::move_to, here.x, here.y, best_z);
    last_focus.set(best_z);
    focused.emit(best_z);
    tw::log::info("in focus at z = {}", best_z);
    return {best_z, best_sharpness, std::move(curve)};
}

void Autofocus::define(tw::ThingSpec<Autofocus>& t) {
    t.doc("Focuses the camera by moving the stage.");
    t.slot("stage", &Autofocus::stage);
    t.slot("camera", &Autofocus::camera);
    t.property("last_focus", &Autofocus::last_focus).read_only().doc("The last focus found, if any.");
    t.event("focused", &Autofocus::focused).doc("A focus was found.");
    auto& run = t.action<"range", "steps">("run", &Autofocus::run);
    run.doc(
        "Find the focus.\n\nThe stage sweeps z over `range` steps around where it is, in `steps`\nmoves; the "
        "camera "
        "measures the sharpness at each, and the stage\nreturns to the sharpest.");
    run.param("range").default_value(1000);
    run.param("steps").default_value(21);
}

}  // namespace microscope
