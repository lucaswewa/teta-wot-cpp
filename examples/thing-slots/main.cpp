// Thing slots: an autofocus that uses a stage, a camera through an
// interface, an optional lamp, and every camera on the server. Two
// configurations connect it differently: the second swaps the simulated
// camera for a "real" one, and adds a lamp.
//
// The C++ port of teta-wot's `thing-slots`. A slot is a member (`Slot<T>`,
// `OptSlot<T>`, `SlotMap<T>`) declared with `t.slot`; an interface is an
// abstract class that Things declare they provide with `t.provides<I>`.

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <teta/wot.hpp>

namespace tw = teta::wot;
using tw::json::Value;

/// What an autofocus needs from a camera.
class CameraApi {
  public:
    virtual ~CameraApi() = default;
    /// How sharp the image is with the focus at `z`.
    virtual double sharpness(std::int64_t z) = 0;
};

/// A motorised focus stage.
class Stage {
  public:
    /// The focus position, in steps.
    tw::Prop<std::int64_t> z{0};

    std::int64_t move_to(std::int64_t target) {
        z.set(target);
        return target;
    }

    static void define(tw::ThingSpec<Stage>& t) {
        t.doc("A motorised focus stage.");
        t.property("z", &Stage::z).doc("The focus position, in steps.").read_only();
        t.action<"z">("move_to", &Stage::move_to).doc("Move the focus.");
    }
};

/// CameraApi through a camera Thing's action, called as a client would call
/// it: validated, and under the global lock if the server has one.
template <typename Camera, double (Camera::*Action)(std::int64_t)>
class CameraThroughAction : public CameraApi {
  public:
    explicit CameraThroughAction(tw::ThingRef<Camera> camera) : camera_(std::move(camera)) {}
    double sharpness(std::int64_t z) override { return camera_.call(Action, z); }

  private:
    tw::ThingRef<Camera> camera_;
};

/// A simulated camera, in focus at 3.
class SimCamera {
  public:
    double simulate(std::int64_t z) { return 1.0 / (1.0 + static_cast<double>((z - 3) * (z - 3))); }

    static void define(tw::ThingSpec<SimCamera>& t) {
        t.doc("A simulated camera, in focus at 3.");
        t.action<"z">("simulate", &SimCamera::simulate).doc("Simulate the sharpness of an image.");
        // Calls through the interface go through the action.
        t.provides<CameraApi>([](tw::ThingRef<SimCamera> camera) {
            return std::make_shared<CameraThroughAction<SimCamera, &SimCamera::simulate>>(std::move(camera));
        });
    }
};

/// The "real" camera: another simulation here, in focus at -2, standing in
/// for hardware. It implements the interface itself.
class RealCamera : public CameraApi {
  public:
    double sharpness(std::int64_t z) override { return measure(z); }
    double measure(std::int64_t z) { return 1.0 / (1.0 + static_cast<double>((z + 2) * (z + 2))); }

    static void define(tw::ThingSpec<RealCamera>& t) {
        t.doc("The \"real\" camera, in focus at -2.");
        t.action<"z">("measure", &RealCamera::measure).doc("Measure the sharpness of an image.");
        // Calls through the interface are plain C++ calls of the Thing.
        t.provides<CameraApi>();
    }
};

/// An LED lamp.
class Lamp {
  public:
    std::atomic<bool> on = false;

    void switch_on() { on = true; }

    static void define(tw::ThingSpec<Lamp>& t) {
        t.doc("An LED lamp.");
        t.action("switch_on", &Lamp::switch_on).doc("Switch the lamp on.");
    }
};

/// Focuses the camera by moving the stage.
class Autofocus {
  public:
    /// The stage: exactly one, found by type.
    tw::Slot<Stage> stage;
    /// The camera: any Thing providing CameraApi, by default the one named
    /// `camera`.
    tw::Slot<CameraApi> camera;
    /// A lamp, if there is one.
    tw::OptSlot<Lamp> lamp;
    /// Every camera on the server, by name.
    tw::SlotMap<CameraApi> cameras;

    Value focus(tw::ActionCtx& ctx) {
        if (const auto* l = lamp.get()) {
            l->call(&Lamp::switch_on);
        }
        double best_sharpness = std::numeric_limits<double>::lowest();
        std::int64_t best_z = 0;
        for (std::int64_t z = -5; z <= 5; ++z) {
            ctx.check_cancelled();
            stage->call(&Stage::move_to, z);
            const double sharpness = camera->sharpness(z);
            if (sharpness > best_sharpness) {
                best_sharpness = sharpness;
                best_z = z;
            }
        }
        stage->call(&Stage::move_to, best_z);
        Value names = Value::array();
        for (const auto& [name, _] : *cameras) {
            names.push_back(name);
        }
        const auto* l = lamp.get();
        return Value{{"best_z", best_z}, {"lamp", l ? Value(l->name()) : Value(nullptr)}, {"cameras", names}};
    }

    static void define(tw::ThingSpec<Autofocus>& t) {
        t.doc("Focuses the camera by moving the stage.");
        t.slot("stage", &Autofocus::stage);
        t.slot("camera", &Autofocus::camera).defaults_to("camera");
        t.slot("lamp", &Autofocus::lamp);
        t.slot("cameras", &Autofocus::cameras);
        t.action("focus", &Autofocus::focus).doc("Scan the focus, and stop at the sharpest position.");
    }
};

tw::ThingRegistry registry() {
    tw::ThingRegistry registry;
    registry.add<Stage>("lab.stage:Stage")
        .add<SimCamera>("lab.cameras:SimCamera")
        .add<RealCamera>("lab.cameras:RealCamera")
        .add<Lamp>("lab.lamp:Lamp")
        .add<Autofocus>("lab.autofocus:Autofocus");
    return registry;
}

// A list of names as Rust's `{:?}` writes it.
std::string debug_list(const std::vector<std::string>& names) {
    std::string out = "[";
    for (const auto& name : names) {
        out += (out.size() > 1 ? ", " : "") + tw::json::dump(Value(name));
    }
    return out + "]";
}

// A settings folder of its own (the default is `./settings`).
std::filesystem::path settings_folder(Value& config) {
    const auto folder =
        std::filesystem::temp_directory_path() / ("thing-slots-" + tw::Uuid::v4().to_string());
    config["settings_folder"] = tw::detail::path_text(folder);
    return folder;
}

Value focus_with(const std::string& title, Value config) {
    const auto settings = settings_folder(config);
    std::cout << title << "\n  " << tw::json::dump(config["things"]) << "\n";
    const auto server =
        tw::ThingServer::from_config(tw::ServerConfig::from_value(config), registry()).build();
    std::cout << "  start order: " << debug_list(server.runtime()->start_order()) << "\n";
    const auto client = tw::testing::TestClient::start(server);
    const auto autofocus = *client.runtime()->thing_ref<Autofocus>("autofocus");
    const Value result = autofocus.call(&Autofocus::focus);
    std::cout << "  focus() → " << tw::json::dump(result) << "\n\n";
    client.stop();
    std::filesystem::remove_all(settings);
    return result;
}

void demo() {
    const Value simulated = focus_with("1. The simulated camera, found by its default name; no lamp:",
                                       Value{{"things",
                                              {{"autofocus", "lab.autofocus:Autofocus"},
                                               {"stage", "lab.stage:Stage"},
                                               {"camera", "lab.cameras:SimCamera"}}}});
    if (simulated["best_z"] != 3 || !simulated["lamp"].is_null()) {
        throw std::runtime_error("the simulated camera should focus at 3, without a lamp");
    }

    const Value real = focus_with(
        "2. The configuration swaps in the real camera, and adds a lamp:",
        Value{{"things",
               {{"autofocus", {{"class", "lab.autofocus:Autofocus"}, {"thing_slots", {{"camera", "real"}}}}},
                {"stage", "lab.stage:Stage"},
                {"simulated", "lab.cameras:SimCamera"},
                {"real", "lab.cameras:RealCamera"},
                {"lamp", "lab.lamp:Lamp"}}}});
    if (real["best_z"] != -2 || real["lamp"] != "lamp") {
        throw std::runtime_error("the real camera should focus at -2, with the lamp");
    }

    std::cout << "3. A configuration that can't work is refused:\n";
    Value broken{{"things",
                  {{"autofocus", "lab.autofocus:Autofocus"},
                   {"stage", "lab.stage:Stage"},
                   {"real", "lab.cameras:RealCamera"}}}};
    const auto settings = settings_folder(broken);
    try {
        (void)tw::ThingServer::from_config(tw::ServerConfig::from_value(broken), registry()).build();
        throw std::runtime_error("the broken configuration was accepted");
    } catch (const tw::BuildError& error) {
        std::cout << "  " << error.what() << "\n";
    }
    std::filesystem::remove_all(settings);
}

int main() {
    try {
        demo();
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
