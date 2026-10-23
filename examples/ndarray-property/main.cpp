// Arrays on the wire.
//
// A camera Thing has a dark frame (a 2-D array property) and an action that
// takes an image and returns it with the dark frame subtracted, plus one that
// returns a histogram. On the wire every array is nested lists, one level per
// dimension. The TD describes a fixed rank exactly (`NdArray<double, 2>` is
// an array of arrays of numbers).
//
//     example-ndarray-property                    # in-process, then exit
//     example-ndarray-property --serve            # serve on port 5000 (or --port), for curl
//
// The C++ port of teta-wot's `ndarray-property`.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <teta/wot.hpp>
#include <thread>

namespace tw = teta::wot;
using tw::json::Value;
using namespace std::chrono_literals;

using Image = tw::NdArray<double, 2>;

std::string shape_text(const std::vector<std::size_t>& shape) {
    std::string out = "[";
    for (std::size_t i = 0; i < shape.size(); ++i) {
        out += (i ? ", " : "") + std::to_string(shape[i]);
    }
    return out + "]";
}

/// A small camera, with arrays for images.
class Camera {
  public:
    /// The dark frame: what the sensor reads with the shutter closed.
    tw::Prop<Image> dark_frame{Image::from_shape({2, 3}, {1.0, 2.0, 1.0, 2.0, 3.0, 2.0})};

    /// Subtract the dark frame from an image of the same shape.
    Image correct(Image image) {
        const auto dark = dark_frame.get();
        if (image.shape() != dark.shape()) {
            throw tw::ActionError::handled("the image is " + shape_text(image.shape()) +
                                           ", but the dark frame is " + shape_text(dark.shape()));
        }
        auto out = image;
        for (std::size_t i = 0; i < out.size(); ++i) {
            out.elements()[i] -= dark.elements()[i];
        }
        return out;
    }

    /// Count the pixels of an image in each of `bins` equal bins between its
    /// minimum and maximum. Any number of dimensions.
    tw::NdArray<std::uint64_t, 1> histogram(tw::NdArray<> image, std::uint64_t bins) {
        double low = std::numeric_limits<double>::infinity();
        double high = -std::numeric_limits<double>::infinity();
        for (const double value : image.elements()) {
            low = std::min(low, value);
            high = std::max(high, value);
        }
        auto counts = tw::NdArray<std::uint64_t, 1>::zeros(
            {static_cast<std::size_t>(std::max<std::uint64_t>(bins, 1))});
        const auto size = counts.size();
        const double width = (high - low) / static_cast<double>(size);
        for (const double value : image.elements()) {
            std::size_t bin = 0;
            if (width > 0.0) {
                bin = std::min(static_cast<std::size_t>((value - low) / width), size - 1);
            }
            ++counts.elements()[bin];
        }
        return counts;
    }

    static void define(tw::ThingSpec<Camera>& t) {
        t.doc("A small camera, with arrays for images.");
        t.property("dark_frame", &Camera::dark_frame)
            .doc("The dark frame: what the sensor reads with the shutter closed.");
        t.action<"image">("correct", &Camera::correct)
            .doc("Subtract the dark frame from an image of the same shape.");
        t.action<"image", "bins">("histogram", &Camera::histogram)
            .doc(
                "Count the pixels of an image in each of `bins` equal bins between\nits minimum and maximum. "
                "Any "
                "number of dimensions.");
    }
};

tw::ThingServer server() {
    return tw::ThingServer::builder().thing<Camera>("camera").build();
}

void ensure(bool condition, const char* what) {
    if (!condition) {
        throw std::runtime_error(what);
    }
}

/// Invokes an action and waits for its output.
Value invoke(const tw::testing::TestClient& client, const std::string& action, const Value& input) {
    const auto invocation = client.post_json("/camera/" + action, input).json();
    auto path = invocation["href"].get<std::string>();
    path.erase(0, std::string_view("http://testserver").size());
    for (;;) {
        const auto status = client.get(path).json();
        if (status["status"] == "completed") {
            return status["output"];
        }
        if (status["status"] != "pending" && status["status"] != "running") {
            throw std::runtime_error(action + " failed: " + tw::json::dump(status["error"]));
        }
        std::this_thread::sleep_for(10ms);
    }
}

void demo() {
    const auto client = tw::testing::TestClient::start(server());

    const auto td = client.get("/camera/").json();
    std::cout << "The TD describes the dark frame as:\n";
    const auto& dark = td["properties"]["dark_frame"];
    std::cout << "  type " << tw::json::dump(dark["type"]) << ", items " << tw::json::dump(dark["items"])
              << ", default " << tw::json::dump(dark["default"]) << "\n";
    ensure(dark["items"]["items"]["type"] == "number", "an array of arrays of numbers");
    std::cout << "and `histogram`'s image, with any number of dimensions: "
              << td["actions"]["histogram"]["input"]["properties"]["image"]["oneOf"].size() << " branches\n";

    const auto frame = client.get("/camera/dark_frame").json();
    std::cout << "\nGET /camera/dark_frame\n  " << tw::json::dump(frame) << "\n";
    ensure(frame == tw::json::parse("[[1.0, 2.0, 1.0], [2.0, 3.0, 2.0]]"), "the default dark frame");

    const auto image = tw::json::parse("[[11, 12, 11], [12, 13, 52]]");
    auto output = invoke(client, "correct", Value{{"image", image}});
    std::cout << "POST /camera/correct {\"image\": " << tw::json::dump(image) << "}\n  output "
              << tw::json::dump(output) << "\n";
    ensure(output == tw::json::parse("[[10.0, 10.0, 10.0], [10.0, 10.0, 50.0]]"), "the corrected image");

    output = invoke(client, "histogram",
                    Value{{"image", tw::json::parse("[[[0, 1], [2, 3]], [[4, 5], [6, 99]]]")}, {"bins", 4}});
    std::cout << "POST /camera/histogram, a 2 x 2 x 2 image, 4 bins\n  output " << tw::json::dump(output)
              << "\n";
    ensure(output == tw::json::parse("[7, 0, 0, 1]"), "the histogram");

    // Arrays must be rectangular: numpy's message.
    const auto ragged = client.put_json("/camera/dark_frame", tw::json::parse("[[1.0, 2.0], [3.0]]"));
    std::cout << "\nPUT /camera/dark_frame [[1.0, 2.0], [3.0]]\n  " << ragged.status << " "
              << tw::json::dump(ragged.json()["detail"][0]["msg"]) << "\n";
    ensure(ragged.status == 422, "a ragged array is refused");
    client.stop();
}

int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string_view(argv[1]) == "--serve") {
            std::uint16_t port = 5000;
            if (argc > 3 && std::string_view(argv[2]) == "--port") {
                port = static_cast<std::uint16_t>(std::stoul(argv[3]));
            }
            tw::log::init(false);
            server().serve("127.0.0.1", port, tw::shutdown_signal(), [](std::uint16_t bound) {
                std::cout << "serving on http://127.0.0.1:" << bound << "/camera/" << std::endl;
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
