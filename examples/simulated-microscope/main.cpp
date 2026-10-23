// The simulated microscope's server.
//
//     example-simulated-microscope                    # a demonstration, in-process, then exit
//     example-simulated-microscope -c examples/simulated-microscope/microscope.json
//
// With arguments it is a Thing server with teta-wot's command line, serving
// configuration files that name the microscope's Things; the scripted demo
// (`demo.py`) drives it with a Python client. The C++ port of teta-wot's
// `simulated-microscope`.

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <teta/wot.hpp>
#include <thread>
#include <vector>

#include "microscope.hpp"

namespace tw = teta::wot;
using tw::json::Value;
using namespace std::chrono_literals;

namespace {

void ensure(bool condition, std::string_view what) {
    if (!condition) {
        throw std::runtime_error(std::string(what));
    }
}

/// Invokes an action and waits for its output.
Value invoke(const tw::testing::TestClient& client, const std::string& path,
             const std::optional<Value>& input = std::nullopt) {
    const auto invocation = client.post_json(path, input).json();
    if (!invocation.contains("href") || !invocation["href"].is_string()) {
        throw std::runtime_error("an invocation: " + tw::json::dump(invocation));
    }
    auto status_path = invocation["href"].get<std::string>();
    status_path.erase(0, std::string_view("http://testserver").size());
    for (;;) {
        const auto status = client.get(status_path).json();
        if (status["status"] == "completed") {
            return status["output"];
        }
        if (status["status"] != "pending" && status["status"] != "running") {
            throw std::runtime_error(path + " failed: " + tw::json::dump(status["error"]));
        }
        std::this_thread::sleep_for(20ms);
    }
}

/// As Rust's `Debug` writes a number event (an `Option<serde_json::Value>`).
std::string debug_number(const std::optional<Value>& event) {
    return event ? "Some(Number(" + tw::json::dump(*event) + "))" : "None";
}

/// A list of names, as Rust's `Debug` writes a `Vec<String>`.
std::string debug_names(const std::vector<std::string>& names) {
    std::string out = "[";
    for (const auto& name : names) {
        out += (out.size() > 1 ? ", " : "") + tw::json::dump(name);
    }
    return out + "]";
}

/// A temporary folder, removed afterwards.
class TemporaryFolder {
  public:
    TemporaryFolder()
        : path_(std::filesystem::temp_directory_path() /
                ("simulated-microscope-" + tw::Uuid::v4().to_string())) {
        std::filesystem::create_directories(path_);
    }
    TemporaryFolder(const TemporaryFolder&) = delete;
    TemporaryFolder& operator=(const TemporaryFolder&) = delete;
    ~TemporaryFolder() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    const std::filesystem::path& path() const { return path_; }
    /// The path as UTF-8 text, for a configuration.
    std::string text() const {
        const auto text = path_.u8string();
        return {text.begin(), text.end()};
    }

  private:
    std::filesystem::path path_;
};

std::size_t count(std::string_view text, std::string_view part) {
    std::size_t n = 0;
    for (auto at = text.find(part); at != std::string_view::npos; at = text.find(part, at + part.size())) {
        ++n;
    }
    return n;
}

/// The demonstration: the same steps as `demo.py`, in-process.
void demo() {
    const TemporaryFolder settings;
    auto config = tw::json::parse(microscope::config);
    config["settings_folder"] = settings.text();
    const auto server =
        tw::ThingServer::from_config(tw::ServerConfig::from_value(config), microscope::registry()).build();
    std::cout << "Things, in start order: " << debug_names(server.runtime()->start_order()) << "\n";
    const auto client = tw::testing::TestClient::start(server);

    // The stage.
    const auto moved = invoke(client, "/stage/move_to", Value{{"x", 400}, {"y", 200}, {"z", 0}});
    std::cout << "stage.move_to(400, 200, 0) -> " << tw::json::dump(moved) << "\n";
    const auto blurred = client.get("/camera/sharpness").json();
    std::cout << std::format("camera.sharpness out of focus: {:.3f}\n", blurred.get<double>());

    // A setting, saved to the settings folder.
    client.put_json("/camera/exposure", 30.0);
    std::ifstream file(settings.path() / "camera" / "Settings-Camera.json", std::ios::binary);
    ensure(file.good(), "the camera's settings file");
    std::string saved{std::istreambuf_iterator<char>(file), {}};
    saved.erase(saved.find_last_not_of(" \t\r\n") + 1);
    std::cout << "camera.exposure = 30, saved: " << saved << "\n";

    // A Blob: a captured image, downloaded.
    const auto capture = invoke(client, "/camera/capture");
    ensure(capture.contains("href") && capture["href"].is_string(), "a Blob");
    auto href = capture["href"].get<std::string>();
    href.erase(0, std::string_view("http://testserver").size());
    const auto image = client.get(href);
    std::cout << "camera.capture() -> " << tw::json::dump(capture["media_type"]) << " (" << image.body.size()
              << " bytes of " << image.header("content-type").value_or("?") << ")\n";
    ensure(image.body.starts_with("\xff\xd8"), "a JPEG");

    // Events, over server-sent events.
    auto focused = client.events("/autofocus/focused");

    // The autofocus: two slots, a sweep, an array.
    const auto focus = invoke(client, "/autofocus/run", Value{{"range", 1000}, {"steps", 21}});
    ensure(focus.contains("curve") && focus["curve"].is_array(), "the curve");
    std::cout << std::format("autofocus.run() -> in focus at z = {}, sharpness {:.3f}, from {} points\n",
                             tw::json::dump(focus["z"]), focus["sharpness"].get<double>(),
                             focus["curve"].size());
    const auto z = focus["z"].get<std::int64_t>();
    ensure(std::abs(z - microscope::focus_z) <= 50, "the focus is found");
    std::cout << "  event `focused`: " << debug_number(focused.next()) << "\n";
    const auto sharp = client.get("/camera/sharpness").json();
    std::cout << std::format("camera.sharpness in focus: {:.3f}\n", sharp.get<double>());
    ensure(sharp.get<double>() > blurred.get<double>(), "focusing sharpens the image");

    // The preview, as MJPEG: two frames.
    {
        auto stream = client.stream("/camera/preview");
        std::cout << "GET /camera/preview: " << stream.header("content-type").value_or("?") << "\n";
        std::string received;
        while (count(received, "--frame") < 3) {
            const auto chunk = stream.next_chunk_within(5s);
            ensure(chunk.has_value(), "the stream ended");
            received += *chunk;
        }
        std::cout << "  two frames received (" << received.size() << " bytes)\n";
    }

    client.stop();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1) {
        return tw::cli::serve_from_cli(argc, argv, microscope::registry());
    }
    try {
        demo();
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
