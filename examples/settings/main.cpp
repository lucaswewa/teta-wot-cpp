// Settings: properties saved to disk.
//
//     example-settings                       # a demonstration in a temporary folder
//     example-settings --folder settings     # keep the files in the folder `settings`
//
// The C++ port of teta-wot's `settings`. A property becomes a setting with
// `.setting()`: it is saved to `{folder}/{thing}/Settings-{Class}.json`
// after every change, and loaded from it when the server starts.

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <teta/wot.hpp>

namespace tw = teta::wot;
using tw::json::Value;

/// How far one stage step moves the image, in micrometres.
struct Calibration {
    double x = 1.0;
    double y = 1.0;

    static void describe(tw::StructInfo<Calibration>& s) {
        s.description("How far one stage step moves the image, in micrometres.");
    }
    bool operator==(const Calibration&) const = default;
};

/// A microscope camera whose settings survive a restart.
class Microscope {
  public:
    /// The exposure time, in milliseconds.
    tw::Prop<double> exposure{10.0};
    /// The objective in use.
    tw::Prop<std::string> objective{"10x"};
    /// The stage calibration.
    tw::Prop<Calibration> calibration{Calibration{1.0, 1.0}};
    /// Frames captured since the server started: a property, not a setting.
    tw::Prop<std::uint64_t> frames{0};

    static void define(tw::ThingSpec<Microscope>& t) {
        t.doc("A microscope camera whose settings survive a restart.");
        t.property("exposure", &Microscope::exposure)
            .setting()
            .doc("The exposure time, in milliseconds.")
            .gt(0)
            .unit("ms");
        t.property("objective", &Microscope::objective).setting().doc("The objective in use.");
        t.property("calibration", &Microscope::calibration).setting().doc("The stage calibration.");
        t.property("frames", &Microscope::frames)
            .doc("Frames captured since the server started: a property, not a setting.")
            .read_only();
    }
};

tw::testing::TestClient serve(const std::filesystem::path& folder) {
    const auto server =
        tw::ThingServer::builder().thing<Microscope>("microscope").settings_folder(folder).build();
    return tw::testing::TestClient::start(server);
}

void show(const tw::testing::TestClient& client) {
    for (const std::string_view name : {"exposure", "objective", "calibration"}) {
        std::cout << "  " << name << ": " << client.get("/microscope/" + std::string(name)).text() << "\n";
    }
}

std::filesystem::path settings_file(const std::filesystem::path& folder) {
    return folder / "microscope" / "Settings-Microscope.json";
}

std::string read(const std::filesystem::path& file) {
    std::ifstream in(file);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void demo(const std::filesystem::path& folder) {
    std::cout << "1. A new server has its defaults, and no settings file yet:\n";
    auto client = serve(folder);
    show(client);
    std::cout << "  file exists: " << (std::filesystem::exists(settings_file(folder)) ? "true" : "false")
              << "\n";

    std::cout << "\n2. Every change is saved:\n";
    client.put_json("/microscope/exposure", 25);
    client.put_json("/microscope/objective", "40x");
    std::cout << read(settings_file(folder)) << "\n";
    client.stop();

    std::cout << "\n3. A restarted server loads them before starting:\n";
    client = serve(folder);
    show(client);
    if (client.get("/microscope/exposure").json() != 25.0) {
        throw std::runtime_error("the exposure should have been loaded");
    }
    client.stop();

    std::cout << "\n4. A hand-edited file: unknown keys and invalid values only warn.\n";
    std::ofstream(settings_file(folder)) << R"({"exposure": -5, "objective": "100x", "lamp": "on"})";
    client = serve(folder);
    show(client);
    if (client.get("/microscope/objective").json() != "100x" ||
        client.get("/microscope/exposure").json() != 10.0) {
        throw std::runtime_error(
            "the objective should have been loaded, and the exposure left at its default");
    }
    client.stop();

    std::cout << "\nThe settings are in " << tw::detail::path_text(folder) << "\n";
}

int main(int argc, char** argv) {
    std::cout << std::unitbuf;  // each line as it is written, between the log's
    tw::log::init(false);
    std::filesystem::path folder;
    bool temporary = true;
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--folder") {
            if (i + 1 >= argc) {
                std::cerr << "error: --folder needs a path\n";
                return EXIT_FAILURE;
            }
            folder = argv[++i];
            temporary = false;
        }
    }
    if (temporary) {
        folder = std::filesystem::temp_directory_path() / ("settings-example-" + tw::Uuid::v4().to_string());
    }
    int code = EXIT_SUCCESS;
    try {
        demo(folder);
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        code = EXIT_FAILURE;
    }
    if (temporary) {
        std::filesystem::remove_all(folder);
    }
    return code;
}
