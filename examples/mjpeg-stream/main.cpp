// MJPEG streams: a simulated camera thread feeds JPEG frames into a stream,
// which a browser plays at `/camera/preview/viewer`. Actions wait for frames:
// one measures the next frame's size (an autofocus sharpness measure),
// another saves one.
//
//     example-mjpeg-stream                        # an in-process demo
//     example-mjpeg-stream --serve                # serve on http://127.0.0.1:5000
//     example-mjpeg-stream --serve --port 0       # any free port
//
// While it serves, open http://127.0.0.1:5000/camera/preview/viewer.
// The C++ port of teta-wot's `mjpeg-stream`.

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <teta/wot.hpp>
#include <thread>
#include <vector>

#include "../common/jpeg.hpp"

namespace tw = teta::wot;
using tw::json::Value;
using namespace std::chrono_literals;

constexpr std::size_t width = 320;
constexpr std::size_t height = 240;

/// Frame `n`: a gradient with a bar sweeping across it, and a frame counter
/// in binary along the top.
std::string render(std::uint64_t n) {
    const std::size_t bar = (n * 8) % width;
    std::vector<std::uint8_t> pixels;
    pixels.reserve(width * height * 3);
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            const std::size_t bit = x / 16;
            std::array<std::uint8_t, 3> pixel{};
            if (y < 12 && bit < 16) {
                pixel = ((n >> (15 - bit)) & 1) == 1 ? std::array<std::uint8_t, 3>{255, 255, 255}
                                                     : std::array<std::uint8_t, 3>{20, 20, 20};
            } else if ((x > bar ? x - bar : bar - x) < 6) {
                pixel = {255, 170, 0};
            } else {
                pixel = {static_cast<std::uint8_t>(x * 90 / width),
                         static_cast<std::uint8_t>(y * 120 / height + 40), 140};
            }
            pixels.insert(pixels.end(), pixel.begin(), pixel.end());
        }
    }
    return examples::jpeg::encode(pixels, static_cast<int>(width), static_cast<int>(height), 80);
}

/// A simulated camera with a live preview.
class Camera {
  public:
    /// The live preview, at `/camera/preview`.
    tw::MjpegStream preview;
    /// Frames per second of the preview.
    tw::Prop<double> fps{10.0};

    /// Starts the camera's thread, which feeds the stream.
    void start_camera() {
        thread_ = std::jthread([this, preview = preview](std::stop_token stop) {
            std::mutex mutex;
            std::condition_variable_any wake;
            for (std::uint64_t n = 0; !stop.stop_requested(); ++n) {
                // `add_frame` never waits, however slow the clients.
                try {
                    (void)preview.add_frame(render(n));
                } catch (const std::exception& error) {
                    std::cerr << "the camera failed: " << error.what() << "\n";
                }
                std::unique_lock lock(mutex);
                wake.wait_for(lock, stop, std::chrono::duration<double>(1.0 / fps.get()),
                              [] { return false; });
            }
        });
    }

    /// Stops the thread, and ends the clients' streams.
    void stop_camera() {
        thread_ = {};
        preview.stop();
    }

    /// The size of the next frame, in bytes: sharper images compress less,
    /// so autofocus routines maximise it.
    std::uint64_t sharpness(tw::ActionCtx& ctx) {
        return waiting(ctx, [&](std::stop_token stop) { return preview.next_frame_size(stop); });
    }

    /// Save the next frame.
    tw::Blob<tw::blob::Jpeg> snapshot(tw::ActionCtx& ctx) {
        return tw::Blob<tw::blob::Jpeg>::from_bytes(
            waiting(ctx, [&](std::stop_token stop) { return preview.grab_frame(stop); }));
    }

    static void define(tw::ThingSpec<Camera>& t) {
        t.doc("A simulated camera with a live preview.");
        t.stream("preview", &Camera::preview);
        t.property("fps", &Camera::fps).doc("Frames per second of the preview.").gt(0).le(30);
        t.on_start(&Camera::start_camera);
        t.on_stop(&Camera::stop_camera);
        t.action("sharpness", &Camera::sharpness)
            .doc(
                "The size of the next frame, in bytes: sharper images compress less,\nso autofocus routines "
                "maximise it.");
        t.action("snapshot", &Camera::snapshot).doc("Save the next frame.");
    }

  private:
    // Waits for a frame with `wait`, until the invocation is cancelled.
    template <typename F>
    static auto waiting(tw::ActionCtx& ctx, F wait) {
        std::stop_source cancelled;
        const auto registration = ctx.cancel_token().on_cancel([&] { cancelled.request_stop(); });
        try {
            return wait(cancelled.get_token());
        } catch (const tw::StreamError&) {
            ctx.check_cancelled();
            throw;
        }
    }

    std::jthread thread_;
};

tw::ThingServer server() {
    return tw::ThingServer::builder().thing<Camera>("camera").build();
}

/// Invokes an action and waits for it to finish.
Value invoke(const tw::testing::TestClient& client, const std::string& action) {
    const auto created = client.post_json("/camera/" + action);
    if (created.status != 201) {
        throw std::runtime_error(action + ": " + created.text());
    }
    const auto href = "/action_invocations/" + created.json()["id"].get<std::string>();
    for (;;) {
        auto invocation = client.get(href).json();
        if (invocation["status"] != "pending" && invocation["status"] != "running") {
            return invocation;
        }
        std::this_thread::sleep_for(10ms);
    }
}

void demo() {
    // Read three frames as a browser would.
    const auto client = tw::testing::TestClient::start(server());
    {
        auto stream = client.stream("/camera/preview");
        std::cout << "GET /camera/preview → " << stream.status << " "
                  << tw::http::reason_phrase(stream.status) << " "
                  << stream.header("content-type").value_or("-") << "\n";
        for (int i = 0; i < 3; ++i) {
            const auto chunk = stream.next_chunk();
            if (!chunk) {
                throw std::runtime_error("the stream ended");
            }
            const auto header_end = chunk->find("\r\n\r\n");
            std::cout << "  part: " << tw::json::dump(chunk->substr(0, header_end)) << " + "
                      << chunk->size() - header_end - 6 << " bytes of JPEG\n";
        }
    }  // the client goes away

    const auto sharpness = invoke(client, "sharpness");
    std::cout << "POST /camera/sharpness → " << tw::json::dump(sharpness["output"]) << " bytes\n";
    const auto snapshot = invoke(client, "snapshot");
    std::cout << "POST /camera/snapshot → " << tw::json::dump(snapshot["output"]["href"]) << "\n";
    const auto viewer = client.get("/camera/preview/viewer");
    std::cout << "GET /camera/preview/viewer → " << viewer.text() << "\n";
    const auto td = client.get("/camera/").json();
    std::cout << "The TD's links: " << tw::json::dump(td["links"]) << "\n";
    client.stop();
}

int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string_view(argv[1]) == "--serve") {
            std::uint16_t port = 5000;
            if (argc > 2 && std::string_view(argv[2]) == "--port") {
                if (argc < 4) {
                    throw std::invalid_argument("--port needs a value");
                }
                port = static_cast<std::uint16_t>(std::stoul(argv[3]));
            }
            tw::log::init(false);
            server().serve("127.0.0.1", port, tw::shutdown_signal(), [](std::uint16_t bound) {
                std::cout << "listening on http://127.0.0.1:" << bound << "\n"
                          << "open http://127.0.0.1:" << bound << "/camera/preview/viewer in a browser"
                          << std::endl;
            });
            return EXIT_SUCCESS;
        }
        demo();
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
