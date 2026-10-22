// The command line, in-process: what doesn't serve (Drogon serves once per
// process). conformance/scenarios/cli runs the twin server's command line,
// serving and with `--fallback`, against the reference's answers.

#include <gtest/gtest.h>

#include <iostream>
#include <sstream>
#include <stop_token>
#include <string>
#include <vector>

#include "../temp_folder.hpp"
#include "teta/wot.hpp"
#include "twins.hpp"

namespace tw = teta::wot;
using tw::json::Value;

namespace cli_test {

struct Run {
    int code;
    std::string out;
    std::string err;
};

Run run(std::vector<std::string> args) {
    args.insert(args.begin(), "prog");
    std::ostringstream out;
    std::ostringstream err;
    auto* const old_out = std::cout.rdbuf(out.rdbuf());
    auto* const old_err = std::cerr.rdbuf(err.rdbuf());
    std::stop_source stop;
    const int code = tw::cli::serve_until(args, twins::registry(), {}, stop.get_token());
    std::cout.rdbuf(old_out);
    std::cerr.rdbuf(old_err);
    tw::log::set_sink(nullptr);
    return {code, out.str(), err.str()};
}

// A configuration with a settings folder of its own.
std::string config(const testing_support::TemporaryFolder& folder, Value things) {
    return tw::json::dump(Value{{"things", std::move(things)}, {"settings_folder", folder.text()}});
}

}  // namespace cli_test

using namespace cli_test;

TEST(Cli, HelpIsClaps) {
    const auto help = run({"--help"});
    EXPECT_EQ(help.code, 0);
    EXPECT_EQ(help.out,
              "Serve Things over HTTP, from a configuration file.\n\n"
              "Usage: prog [OPTIONS]\n\n"
              "Options:\n"
              "  -c, --config <CONFIG>  Path to configuration file\n"
              "  -j, --json <JSON>      Configuration as JSON string\n"
              "      --fallback         Serve an error page instead of exiting, if we can't start\n"
              "      --host <HOST>      Bind socket to this host [default: 127.0.0.1]\n"
              "      --port <PORT>      Bind socket to this port. If 0, an available port will be picked "
              "[default: 5000]\n"
              "      --debug            Enable debug logging\n"
              "  -h, --help             Print help\n");
    EXPECT_EQ(run({"-h"}).out, help.out);
}

TEST(Cli, AWrongCommandLineExitsWith2) {
    auto r = run({"--foo"});
    EXPECT_EQ(r.code, 2);
    EXPECT_EQ(
        r.err,
        "error: unexpected argument '--foo' found\n\nUsage: prog [OPTIONS]\n\nFor more information, try "
        "'--help'.\n");
    r = run({"-c"});
    EXPECT_EQ(r.code, 2);
    EXPECT_EQ(
        r.err,
        "error: a value is required for '--config <CONFIG>' but none was supplied\n\nFor more information, "
        "try '--help'.\n");
    r = run({"--port", "x"});
    EXPECT_EQ(r.err,
              "error: invalid value 'x' for '--port <PORT>': invalid digit found in string\n\nFor more "
              "information, try '--help'.\n");
    r = run({"--port=70000"});
    EXPECT_EQ(r.err,
              "error: invalid value '70000' for '--port <PORT>': 70000 is not in 0..=65535\n\nFor more "
              "information, try '--help'.\n");
    r = run({"-j", "{}", "--json", "{}"});
    EXPECT_EQ(
        r.err,
        "error: the argument '--json <JSON>' cannot be used multiple times\n\nUsage: prog [OPTIONS]\n\nFor "
        "more information, try '--help'.\n");
    EXPECT_EQ(run({"extra"}).code, 2);
}

TEST(Cli, NoConfigurationOrAMissingFileExitsWith1) {
    auto r = run({});
    EXPECT_EQ(r.code, 1);
    EXPECT_EQ(r.err, "Error: No configuration (or empty configuration) provided\n");
    r = run({"-c", "no-such-file.json"});
    EXPECT_EQ(r.code, 1);
    EXPECT_TRUE(r.err.starts_with("Error: Could not find configuration file no-such-file.json (")) << r.err;
    EXPECT_TRUE(r.err.ends_with(" (os error 2))\n")) << r.err;
    r = run({"-c", "a.json", "-j", "{}"});
    EXPECT_EQ(r.err, "Error: Can't use both --config and --json simultaneously.\n");
}

TEST(Cli, AnInvalidConfigurationExitsWith3) {
    auto r = run({"-j", "{"});
    EXPECT_EQ(r.code, 3);
    EXPECT_EQ(
        r.out,
        "Error reading configuration:\n1 validation error for ThingServerConfig\n  Invalid JSON: EOF while "
        "parsing an object at line 1 column 1\n");
    const testing_support::TemporaryFolder folder;
    r = run({"-j", config(folder, Value{{"x", "nope:Thing"}})});
    EXPECT_EQ(r.code, 3);
    EXPECT_TRUE(r.out.starts_with(
        "Error reading configuration:\n1 validation error for ThingServerConfig\nthings.x.cls\n"
        "  No Thing type is registered as 'nope:Thing'. Registered: "
        "labthings_fastapi.example_things.MyThing, "))
        << r.out;
}

TEST(Cli, AThingThatCantBeBuiltExitsWith1) {
    const testing_support::TemporaryFolder folder;
    const auto r =
        run({"-j", config(folder, Value{{"a", {{"cls", "twins:MyThing"}, {"kwargs", {{"x", 1}}}}}})});
    EXPECT_EQ(r.code, 1);
    EXPECT_EQ(r.err,
              "Error: the configuration of Thing `a` is invalid: unknown field `x`, there are no fields\n");
}

TEST(Cli, AThingThatCantStartExitsWith3) {
    const testing_support::TemporaryFolder folder;
    const auto r =
        run({"-j",
             config(folder, Value{{"counter", "twins:TestThing"},
                                  {"stage", "labthings_fastapi.example_things:ThingThatCantStart"}}),
             "--port", "0"});
    EXPECT_EQ(r.code, 3);
    EXPECT_EQ(r.out, "");
    EXPECT_EQ(r.err, "Error: Failed to enter 'stage' Thing: This thing can't start\n");
}
