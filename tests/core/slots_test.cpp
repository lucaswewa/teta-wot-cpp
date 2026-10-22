// Thing slots: what connects where, teta-wot's messages when it can't, the
// start order, and interfaces.

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "../log_capture.hpp"
#include "teta/wot.hpp"

namespace tw = teta::wot;

namespace slots_test {

/// What a user needs from an imager.
class Imager {
  public:
    virtual ~Imager() = default;
    virtual std::string kind() = 0;
};

class Stage {
  public:
    static void define(tw::ThingSpec<Stage>&) {}
};

/// Implements the interface itself.
class Camera : public Imager {
  public:
    std::string kind() override { return "camera"; }
    static void define(tw::ThingSpec<Camera>& t) { t.provides<Imager>(); }
};

/// Provides the interface through its action.
class Scope {
  public:
    std::string snap() { return "scope"; }

    class AsImager : public Imager {
      public:
        explicit AsImager(tw::ThingRef<Scope> scope) : scope_(std::move(scope)) {}
        std::string kind() override { return scope_.call(&Scope::snap); }

      private:
        tw::ThingRef<Scope> scope_;
    };

    static void define(tw::ThingSpec<Scope>& t) {
        t.action("snap", &Scope::snap);
        t.provides<Imager>(
            [](tw::ThingRef<Scope> scope) { return std::make_shared<AsImager>(std::move(scope)); });
    }
};

class User {
  public:
    tw::Slot<Stage> stage;
    tw::OptSlot<Camera> camera;
    tw::SlotMap<Imager> imagers;

    static void define(tw::ThingSpec<User>& t) {
        t.slot("stage", &User::stage);
        t.slot("camera", &User::camera);
        t.slot("imagers", &User::imagers);
    }
};

class Named {
  public:
    tw::Slot<Imager> imager;
    static void define(tw::ThingSpec<Named>& t) { t.slot("imager", &Named::imager).defaults_to("main"); }
};

class Picky {
  public:
    tw::Slot<Stage> stage;
    static void define(tw::ThingSpec<Picky>& t) { t.slot("stage", &Picky::stage).defaults_to_nothing(); }
};

class Listed {
  public:
    tw::SlotMap<Stage> stages;
    static void define(tw::ThingSpec<Listed>& t) {
        t.slot("stages", &Listed::stages).defaults_to(std::vector<std::string>{"b", "a"});
    }
};

// Things that refer to each other: one way through an interface, as a slot's
// target must be complete where the slot is declared.
class LeftApi {
  public:
    virtual ~LeftApi() = default;
};

class Right {
  public:
    tw::OptSlot<LeftApi> left;
    static void define(tw::ThingSpec<Right>& t) { t.slot("left", &Right::left); }
};

class Left : public LeftApi {
  public:
    tw::OptSlot<Right> right;
    static void define(tw::ThingSpec<Left>& t) {
        t.slot("right", &Left::right);
        t.provides<LeftApi>();
    }
};

// The message of the BuildError building throws.
template <typename F>
std::string build_error(F&& build) {
    try {
        (void)std::forward<F>(build)();
    } catch (const tw::BuildError& error) {
        EXPECT_EQ(error.kind(), tw::BuildError::Kind::slot);
        return error.what();
    }
    ADD_FAILURE() << "built";
    return {};
}

}  // namespace slots_test

using namespace slots_test;

TEST(Slots, ByTypeTheyConnectAndTheirThingsStartFirst) {
    auto runtime = tw::Runtime::builder()
                       .thing<User>("user")
                       .thing<Stage>("stage")
                       .thing<Camera>("camera")
                       .thing<Scope>("scope")
                       .build();
    const auto user = runtime.thing_ref<User>("user")->shared();
    EXPECT_EQ(user->stage->name(), "stage");
    ASSERT_TRUE(user->camera);
    EXPECT_EQ(user->camera.get()->name(), "camera");
    ASSERT_EQ(user->imagers->size(), 2u);
    EXPECT_EQ(user->imagers->begin()->first, "camera");
    runtime.start();
    std::vector<std::string> kinds;
    for (const auto& [name, imager] : *user->imagers) {
        kinds.push_back(imager->kind());
    }
    EXPECT_EQ(kinds, (std::vector<std::string>{"camera", "scope"}));
    EXPECT_EQ(runtime.start_order(), (std::vector<std::string>{"stage", "camera", "scope", "user"}));
    EXPECT_EQ(runtime.thing("user")->slot_names(), (std::vector<std::string>{"stage", "camera", "imagers"}));
    runtime.stop();
}

TEST(Slots, AnOptionalSlotMayStayEmptyAndAMapMayBeEmpty) {
    auto runtime = tw::Runtime::builder().thing<User>("user").thing<Stage>("stage").build();
    const auto user = runtime.thing_ref<User>("user")->shared();
    EXPECT_FALSE(user->camera);
    EXPECT_EQ(user->camera.get(), nullptr);
    EXPECT_TRUE(user->imagers->empty());
}

TEST(Slots, WhenTheyCantConnectTheMessageIsTetaWots) {
    EXPECT_EQ(
        build_error([] { return tw::Runtime::builder().thing<User>("user").build(); }),
        "Can't connect 'user.stage' because no matching Thing was found. It was not configured, and used "
        "the default. The default searches for Things by type: 'Stage'.");
    EXPECT_EQ(
        build_error([] {
            return tw::Runtime::builder().thing<User>("user").thing<Stage>("a").thing<Stage>("b").build();
        }),
        "Can't connect 'user.stage' because it can't connect to multiple Things. It was not configured, and "
        "used the default. The default searches for Things by type: 'Stage'.");
    EXPECT_EQ(
        build_error([] {
            return tw::Runtime::builder()
                .thing<User>("user")
                .thing<Stage>("s")
                .thing<Camera>("a")
                .thing<Camera>("b")
                .build();
        }),
        "Can't connect 'user.camera' because it can't connect to multiple Things. It was not configured, "
        "and used the default. The default searches for Things by type: 'Camera'.");
    EXPECT_EQ(
        build_error([] { return tw::Runtime::builder().thing<Named>("named").build(); }),
        "Can't connect 'named.imager' because main is not the name of a Thing. It was not configured, and "
        "used the default. The default is 'main'.");
    EXPECT_EQ(build_error([] {
                  return tw::Runtime::builder()
                      .thing<Named>("named")
                      .thing<Stage>("stage")
                      .thing_slots("named", {{"imager", tw::SlotSelection::name("stage")}})
                      .build();
              }),
              "Can't connect 'named.imager' because stage is the wrong type. It was configured to connect to "
              "'stage'. The default is 'main'.");
    EXPECT_EQ(
        build_error([] { return tw::Runtime::builder().thing<Picky>("picky").thing<Stage>("s").build(); }),
        "Can't connect 'picky.stage' because it must be set in configuration. It was not configured, and "
        "used the default. The default is 'None'.");
    EXPECT_EQ(
        build_error([] {
            return tw::Runtime::builder()
                .thing<Listed>("listed")
                .thing<Stage>("a")
                .thing_slots("listed", {{"stages", tw::SlotSelection::names({"a", "c"})}})
                .build();
        }),
        "Can't connect 'listed.stages' because c is not the name of a Thing. It was configured to connect "
        "to '['a', 'c']'. The default is '['b', 'a']'.");
}

TEST(Slots, ConfigurationOverridesTheDefault) {
    auto runtime = tw::Runtime::builder()
                       .thing<Named>("named")
                       .thing<Scope>("scope")
                       .thing<Camera>("main")
                       .thing_slots("named", {{"imager", tw::SlotSelection::name("scope")}})
                       .build();
    runtime.start();
    EXPECT_EQ(runtime.thing_ref<Named>("named")->thing().imager->kind(), "scope");
    runtime.stop();

    // Nothing, for a slot that may be empty.
    auto empty = tw::Runtime::builder()
                     .thing<User>("user")
                     .thing<Stage>("stage")
                     .thing<Camera>("camera")
                     .thing_slots("user", {{"camera", tw::SlotSelection::nothing()}})
                     .build();
    EXPECT_FALSE(empty.thing_ref<User>("user")->thing().camera);
}

TEST(Slots, AMapKeepsItsNamesOrder) {
    auto runtime = tw::Runtime::builder()
                       .thing<Listed>("listed")
                       .thing<Stage>("a")
                       .thing<Stage>("b")
                       .thing<Stage>("c")
                       .build();
    std::vector<std::string> names;
    for (const auto& [name, stage] : *runtime.thing_ref<Listed>("listed")->thing().stages) {
        names.push_back(name);
    }
    EXPECT_EQ(names, (std::vector<std::string>{"b", "a"}));
}

TEST(Slots, AnUnknownConfiguredSlotAndACycleWarn) {
    testing_support::LogCapture log;
    auto runtime = tw::Runtime::builder()
                       .thing<Left>("left")
                       .thing<Right>("right")
                       .thing_slots("left", {{"middle", tw::SlotSelection::nothing()}})
                       .build();
    EXPECT_EQ(runtime.start_order(), (std::vector<std::string>{"left", "right"}));
    EXPECT_EQ(
        log.messages(),
        (std::vector<std::string>{
            "`thing_slots` of `left` configures `middle`, which isn't one of its slots",
            "the slots of [\"left\", \"right\"] depend on each other in a cycle: starting every Thing in "
            "configuration order"}));
}

TEST(Slots, ThingsThatReferToEachOtherGoWithTheRuntime) {
    std::weak_ptr<Left> left;
    std::weak_ptr<Right> right;
    {
        auto runtime = tw::Runtime::builder().thing<Left>("left").thing<Right>("right").build();
        left = runtime.thing_ref<Left>("left")->shared();
        right = runtime.thing_ref<Right>("right")->shared();
        EXPECT_EQ(left.lock()->right.get()->name(), "right");
    }
    EXPECT_TRUE(left.expired());
    EXPECT_TRUE(right.expired());
}

TEST(Slots, UnconnectedSlotsSaySo) {
    const User user;
    try {
        (void)user.stage.get();
        ADD_FAILURE() << "an unconnected slot gave a Thing";
    } catch (const std::logic_error& error) {
        EXPECT_STREQ(error.what(), "a slot for Stage has not been connected to a Thing yet");
    }
    EXPECT_EQ(user.camera.get(), nullptr);
    EXPECT_THROW((void)user.imagers.get(), std::logic_error);
}

TEST(Slots, ThingsProvideTheInterfacesTheyDeclare) {
    auto runtime = tw::Runtime::builder().thing<Camera>("camera").thing<Stage>("stage").build();
    EXPECT_NE(runtime.thing("camera")->provided<Imager>(), nullptr);
    EXPECT_EQ(runtime.thing("stage")->provided<Imager>(), nullptr);
}

TEST(Slots, SelectionsAreWrittenAsTetaWotWritesThem) {
    EXPECT_EQ(tw::SlotSelection::by_type().to_string(), "...");
    EXPECT_EQ(tw::SlotSelection::name("camera").to_string(), "camera");
    EXPECT_EQ(tw::SlotSelection::names({"a", "b"}).to_string(), "['a', 'b']");
    EXPECT_EQ(tw::SlotSelection::names({}).to_string(), "[]");
    EXPECT_EQ(tw::SlotSelection::nothing().to_string(), "None");
}
