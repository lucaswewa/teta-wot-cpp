// Harness: one Thing under test, stand-ins for its slots, a temporary
// settings folder.

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>

#include "teta/wot.hpp"

namespace tw = teta::wot;

namespace harness_test {

class StageApi {
  public:
    virtual ~StageApi() = default;
    virtual std::int64_t position() = 0;
};

class FakeStage : public StageApi {
  public:
    std::int64_t position() override { return 42; }
    static void define(tw::ThingSpec<FakeStage>& t) { t.provides<StageApi>(); }
};

class Autofocus {
  public:
    tw::Slot<StageApi> stage;
    tw::Prop<std::int64_t> best{0};

    std::int64_t focus() {
        best.set(stage->position());
        return best.get();
    }

    static void define(tw::ThingSpec<Autofocus>& t) {
        t.slot("stage", &Autofocus::stage);
        t.property("best", &Autofocus::best).setting();
        t.action("focus", &Autofocus::focus);
    }
};

}  // namespace harness_test

using namespace harness_test;

TEST(Harness, AThingRunsWithStandInsAndATemporarySettingsFolder) {
    std::filesystem::path folder;
    {
        const auto harness =
            tw::testing::Harness<Autofocus>::builder("autofocus").thing<FakeStage>("stage").start();
        EXPECT_EQ(harness.thing().call(&Autofocus::focus), 42);
        EXPECT_EQ(harness.client().get("/autofocus/best").json(), 42);
        ASSERT_TRUE(harness.settings_folder());
        folder = *harness.settings_folder();
        EXPECT_TRUE(std::filesystem::exists(folder / "autofocus" / "Settings-Autofocus.json"));
        harness.stop();
    }
    EXPECT_FALSE(std::filesystem::exists(folder));
}

TEST(Harness, ASlotWithoutAStandInCantBeBuilt) {
    EXPECT_THROW((void)tw::testing::Harness<Autofocus>::builder("autofocus").start(), tw::BuildError);
}
