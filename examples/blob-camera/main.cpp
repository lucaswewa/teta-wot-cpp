// Blobs: a camera's captures are returned as JPEG data, from memory and from
// a file in a temporary directory, downloaded from `/blob/{id}`, and passed
// back as the input of another action.
//
//     example-blob-camera                         # an in-process demo
//     example-blob-camera --serve                 # serve on http://127.0.0.1:5000
//     example-blob-camera --serve --port 0        # any free port
//
// The C++ port of teta-wot's `blob-camera`.

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <teta/wot.hpp>
#include <thread>
#include <utility>
#include <vector>

#include "../common/jpeg.hpp"

namespace tw = teta::wot;
using tw::json::Value;
using namespace std::chrono_literals;

constexpr std::uint16_t width = 160;
constexpr std::uint16_t height = 120;

/// What `describe` finds in a JPEG.
struct ImageInfo {
    std::uint64_t bytes = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;

    static void describe(tw::StructInfo<ImageInfo>& s) {
        s.description("What `describe` finds in a JPEG.");
        s.field("bytes").description("The size of the file, in bytes.");
        s.field("width").description("The width, in pixels.");
        s.field("height").description("The height, in pixels.");
    }
};

/// The width and height in a JPEG's frame header.
std::optional<std::pair<std::uint16_t, std::uint16_t>> jpeg_size(const std::string& data) {
    const auto byte = [&](std::size_t i) { return static_cast<std::uint8_t>(data[i]); };
    const auto u16 = [&](std::size_t i) { return static_cast<std::uint16_t>((byte(i) << 8) | byte(i + 1)); };
    std::size_t i = 2;
    while (i + 9 < data.size()) {
        if (byte(i) != 0xff) {
            return std::nullopt;
        }
        if (byte(i + 1) >= 0xc0 && byte(i + 1) <= 0xc3) {
            return std::pair{u16(i + 7), u16(i + 5)};
        }
        i += 2 + u16(i + 2);
    }
    return std::nullopt;
}

/// A simulated camera.
class Camera {
  public:
    /// How many images have been captured.
    tw::Prop<std::int64_t> captures{0};

    /// Capture an image, returned from memory.
    tw::Blob<tw::blob::Jpeg> capture() { return tw::Blob<tw::blob::Jpeg>::from_bytes(take_picture()); }

    /// Capture an image, saved to a file in a temporary directory, which is
    /// deleted with the Blob, when the invocation expires.
    tw::Blob<tw::blob::Jpeg> capture_to_file() {
        const auto jpeg = take_picture();
        tw::blob::TemporaryDirectory folder;
        std::ofstream(folder.path() / "capture.jpg", std::ios::binary) << jpeg;
        return tw::Blob<tw::blob::Jpeg>::from_temporary_directory(std::move(folder), "capture.jpg");
    }

    /// Describe an image: its size in bytes and in pixels.
    ImageInfo describe(tw::Blob<tw::blob::Jpeg> image) {
        const auto data = image.bytes();
        const auto size = jpeg_size(data);
        if (!size) {
            throw tw::ActionError::handled("not a baseline JPEG");
        }
        return {static_cast<std::uint64_t>(data.size()), size->first, size->second};
    }

    static void define(tw::ThingSpec<Camera>& t) {
        t.doc("A simulated camera.");
        t.property("captures", &Camera::captures).read_only().doc("How many images have been captured.");
        t.action("capture", &Camera::capture).doc("Capture an image, returned from memory.");
        t.action("capture_to_file", &Camera::capture_to_file)
            .doc(
                "Capture an image, saved to a file in a temporary directory.\n\nThe directory is deleted "
                "with "
                "the Blob, when the invocation expires.");
        t.action<"image">("describe", &Camera::describe)
            .doc("Describe an image: its size in bytes and in pixels.");
    }

  private:
    /// Takes a picture: a colour gradient with a band that moves each time.
    std::string take_picture() {
        const auto n = captures.get();
        captures.set(n + 1);
        std::vector<std::uint8_t> pixels;
        pixels.reserve(std::size_t{width} * height * 3);
        for (std::size_t y = 0; y < height; ++y) {
            for (std::size_t x = 0; x < width; ++x) {
                const bool band = (x + static_cast<std::size_t>(n) * 16) % width < 20;
                pixels.push_back(static_cast<std::uint8_t>(x * 255 / width));
                pixels.push_back(static_cast<std::uint8_t>(y * 255 / height));
                pixels.push_back(band ? 255 : 60);
            }
        }
        return examples::jpeg::encode(pixels, width, height, 85);
    }
};

tw::ThingServer server() {
    return tw::ThingServer::builder().thing<Camera>("camera").build();
}

/// Invokes an action and waits for it to finish.
Value invoke(const tw::testing::TestClient& client, const std::string& action,
             const std::optional<Value>& input = std::nullopt) {
    const auto created = client.post_json("/camera/" + action, input);
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
    // What `blob_client.py` does.
    const auto client = tw::testing::TestClient::start(server());
    for (const std::string action : {"capture", "capture_to_file"}) {
        const auto invocation = invoke(client, action);
        const auto& output = invocation["output"];
        std::cout << "POST /camera/" << action << " → output " << tw::json::dump(output) << "\n";

        // The link downloads the data; so does the invocation's `/output`.
        auto path = output["href"].get<std::string>();
        if (path.starts_with("http://testserver")) {
            path.erase(0, std::string_view("http://testserver").size());
        }
        const auto image = client.get(path);
        std::cout << "GET " << path << " → " << image.status << " " << tw::http::reason_phrase(image.status)
                  << " " << image.header("content-type").value_or("-") << ", " << image.body.size()
                  << " bytes\n";
        if (!image.body.starts_with("\xff\xd8")) {
            throw std::runtime_error("not a JPEG");
        }

        // Passing the Blob back: its JSON form (the `href` is what counts).
        const auto info = invoke(client, "describe", Value{{"image", output}});
        std::cout << "POST /camera/describe {\"image\": …} → " << tw::json::dump(info["output"]) << "\n";
        if (info["output"]["width"] != width || info["output"]["height"] != height ||
            info["output"]["bytes"] != image.body.size()) {
            throw std::runtime_error("describe found the wrong size");
        }
    }
    std::cout << "captures: " << client.get("/camera/captures").text() << "\n";
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
                std::cout << "listening on http://127.0.0.1:" << bound << std::endl;
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
