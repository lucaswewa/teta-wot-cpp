// The twins' Thing Descriptions: the C++ twins (conformance/twins), defined
// with ThingSpec and described by their runtime, equal the reference's
// (teta-wot v0.1.0, `conformance/fixtures/http/tds.json` and
// `tds-wot.json`) byte for byte, after the fixtures' normalisation. This was
// the Phase 1 gate with hand-written descriptions; since Phase 3 it checks
// the definitions themselves: names, docstrings, types, parameters,
// defaults, constraints. `blob_probe` and `ndarray_probe` follow with Blobs
// (Phase 7) and arrays (Phase 10).

#include <gtest/gtest.h>

#include <fstream>
#include <map>
#include <regex>
#include <string>

#include "teta/wot.hpp"
#include "twins.hpp"

namespace {

namespace tw = teta::wot;
namespace td = teta::wot::td;
using tw::json::Value;

const tw::Runtime& runtime() {
    static const tw::Runtime instance = [] {
        tw::RuntimeBuilder builder;
        for (const auto& name : twins::names) {
            twins::add(builder, name);
        }
        return builder.build();
    }();
    return instance;
}

// ---- Serving them as the reference does ----------------------------------------------

// The TD as teta-wot serves it, for a request with `Host: testserver`.
td::ThingDescription served(const std::string& name, bool wot_profile) {
    td::TdOptions options;
    options.path = "/" + name + "/";
    options.base = "http://testserver/";
    options.id = td::thing_id("conformance", name);
    options.observation = true;
    options.websocket = "ws://testserver/" + name + "/ws";
    options.links = true;
    options.top_level = true;
    options.invocations = "/action_invocations/";
    options.synchronous_actions = wot_profile;
    if (wot_profile) {
        options.profiles = {"https://www.w3.org/2022/wot/profile/http-basic/v1",
                            "https://www.w3.org/2022/wot/profile/http-sse/v1"};
    }
    return runtime().thing(name)->thing_description(options);
}

// The fixtures' normalisation of UUIDs, numbered by first appearance.
class Normaliser {
  public:
    std::string text(const std::string& raw) {
        static const std::regex uuid("[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}");
        std::string out;
        auto begin = std::sregex_iterator(raw.begin(), raw.end(), uuid);
        std::size_t last = 0;
        for (auto it = begin; it != std::sregex_iterator(); ++it) {
            out += raw.substr(last, static_cast<std::size_t>(it->position()) - last);
            const std::string found = it->str();
            if (!numbers_.contains(found)) {
                numbers_.emplace(found, numbers_.size());
            }
            out += "<uuid:" + std::to_string(numbers_.at(found)) + ">";
            last = static_cast<std::size_t>(it->position() + it->length());
        }
        return out + raw.substr(last);
    }

  private:
    std::map<std::string, std::size_t> numbers_;
};

void compare_with_fixture(const char* fixture, bool wot_profile) {
    std::ifstream file(std::string(TETA_WOT_FIXTURES) + "/http/" + fixture, std::ios::binary);
    ASSERT_TRUE(file) << fixture;
    const auto data = Value::parse(file);
    Normaliser normaliser;
    int compared = 0;
    for (const auto& step : data["steps"]) {
        const std::string name = step["step"]["path"].get<std::string>().substr(1);
        const std::string thing = name.substr(0, name.size() - 1);
        if (!runtime().thing(thing)) {
            continue;  // blob_probe, ndarray_probe: later phases (they are the last steps, so the
                       // UUID numbering of the others doesn't depend on them)
        }
        const auto td = served(thing, wot_profile);
        EXPECT_EQ(normaliser.text(tw::json::dump(td::to_json(td))),
                  step["response"]["text"].get<std::string>())
            << thing << " in " << fixture;
        ++compared;
    }
    EXPECT_EQ(compared, 7);
}

TEST(TwinTds, EqualTheReferenceInTheTetathingProfile) {
    compare_with_fixture("tds.json", false);
}

TEST(TwinTds, EqualTheReferenceInTheWotProfile) {
    compare_with_fixture("tds-wot.json", true);
}

TEST(TwinTds, IdsAreTetaWotsDerivation) {
    EXPECT_EQ(td::thing_id("conformance", "counter"), "urn:uuid:f46f31e7-a858-5fd5-91b0-829d229ed6cd");
}

}  // namespace
