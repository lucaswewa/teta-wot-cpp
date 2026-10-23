// Exporting the API description without starting a server: the OpenAPI
// document, which code generators read, and every Thing Description.
//
//     example-openapi-export                                     # to ./openapi-export
//     example-openapi-export out --base http://lab-pc:5000/      # another folder and host
//
// Then generate a TypeScript client from the document:
//
//     npx --yes openapi-typescript@7 openapi-export/openapi.json -o openapi-export/api.d.ts
//
// The C++ port of teta-wot's `openapi-export`.

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <teta/wot.hpp>

namespace tw = teta::wot;

/// A position, in steps.
struct Position {
    std::int64_t x = 0;
    std::int64_t y = 0;
    std::int64_t z = 0;

    static void describe(tw::StructInfo<Position>& s) {
        s.description("A position, in steps.");
        s.field("x").description("Along x.");
        s.field("y").description("Along y.");
        s.field("z").description("Along z (focus).");
    }
};

/// A motorised stage.
class Stage {
  public:
    /// Where the stage is.
    tw::Prop<Position> position;
    /// The stage reached a new position.
    tw::Event<Position> moved;

    /// Move to a position.
    Position move_to(std::int64_t x, std::int64_t y, std::int64_t z) {
        const Position target{x, y, z};
        position.set(target);
        moved.emit(target);
        return target;
    }

    static void define(tw::ThingSpec<Stage>& t) {
        t.doc("A motorised stage.");
        t.property("position", &Stage::position).read_only().doc("Where the stage is.");
        t.event("moved", &Stage::moved).doc("The stage reached a new position.");
        t.action<"x", "y", "z">("move_to", &Stage::move_to)
            .doc("Move to a position.")
            .param("z")
            .default_value(0);
    }
};

/// A camera.
class Camera {
  public:
    /// The exposure time.
    tw::Prop<double> exposure{10.0};
    tw::MjpegStream preview;

    /// Capture an image.
    tw::Blob<tw::blob::Jpeg> capture() { return tw::Blob<tw::blob::Jpeg>::from_bytes("\xff\xd8\xff\xd9"); }

    static void define(tw::ThingSpec<Camera>& t) {
        t.doc("A camera.");
        t.property("exposure", &Camera::exposure).doc("The exposure time.").gt(0).unit("ms");
        t.stream("preview", &Camera::preview);
        t.action("capture", &Camera::capture).doc("Capture an image.");
    }
};

void write(const std::filesystem::path& path, const tw::json::Value& value) {
    std::ofstream file(path, std::ios::binary);
    file << tw::json::dump_pretty(value) << "\n";
    if (!file) {
        throw std::runtime_error("can't write " + path.string());
    }
}

int main(int argc, char** argv) {
    try {
        std::filesystem::path folder = "openapi-export";
        std::string base = "http://localhost:5000/";
        for (int i = 1; i < argc; ++i) {
            if (std::string_view(argv[i]) == "--base") {
                if (++i == argc) {
                    throw std::invalid_argument("--base needs a URL");
                }
                base = argv[i];
            } else {
                folder = argv[i];
            }
        }

        // The runtime is built, not started: nothing runs, nothing listens.
        const auto runtime = tw::Runtime::builder().thing<Stage>("stage").thing<Camera>("camera").build();
        tw::http::HttpOptions options;
        options.api_title = "Microscope";
        options.api_version = "1.0.0";

        std::filesystem::create_directories(folder);
        const auto document = tw::http::openapi(runtime, options);
        write(folder / "openapi.json", document);
        std::size_t operations = 0;
        for (const auto& [path, item] : document["paths"].items()) {
            operations += item.size();
        }
        std::cout << "wrote " << (folder / "openapi.json").string() << " (" << operations << " operations)\n";

        for (const auto& thing : runtime.things()) {
            const auto path = folder / (thing->name() + ".td.json");
            write(path, tw::http::thing_description(runtime, thing->name(), base, options));
            std::cout << "wrote " << path.string() << " (base " << base << ")\n";
        }

        for (const auto& [path, method] :
             {std::pair{"/stage/move_to", "post"}, std::pair{"/camera/exposure", "put"}}) {
            std::string upper = method;
            for (auto& c : upper) {
                c = static_cast<char>(c - 'a' + 'A');
            }
            std::cout << "  " << upper << " " << path << ": "
                      << tw::json::dump(document["paths"][path][method]["operationId"]) << "\n";
        }
        if (document["paths"]["/stage/move_to"]["post"]["operationId"] != "start_action_stage_move_to_post") {
            throw std::runtime_error("unexpected operationId");
        }
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
