// The C++ twin server: the twins (conformance/twins) served with teta-wot's
// command line, so that conformance/run.py runs its scenarios against it as
// against the reference server (ADR-0011):
//
//     twin-server -c config.json [--host 127.0.0.1] [--port 5000] [--fallback]

#include <teta/wot.hpp>

#include "twins.hpp"

int main(int argc, char** argv) {
    return teta::wot::cli::serve_from_cli(argc, argv, twins::registry());
}
