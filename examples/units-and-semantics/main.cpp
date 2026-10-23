// Units and semantic annotations: `unit` on properties, `@type` on the Thing
// and its affordances, and the `@context` prefixes that make compact terms
// like `saref:Sensor` resolvable.
//
//     example-units-and-semantics
//
// The C++ port of teta-wot's `units-and-semantics`.

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <teta/wot.hpp>
#ifdef TETA_WOT_TD_VALIDATION
#include <teta/wot/td/validation.hpp>
#endif

namespace tw = teta::wot;
using tw::json::Value;

/// A thermometer, described with SAREF and OM terms.
class Thermometer {
  public:
    /// The measured temperature.
    tw::Prop<double> temperature{21.0};
    /// How often to measure.
    tw::Prop<double> interval{1.0};

    /// Take a measurement now.
    double measure() { return temperature.get(); }

    static void define(tw::ThingSpec<Thermometer>& t) {
        t.doc("A thermometer, described with SAREF and OM terms.");
        t.semantic_type("saref:Sensor");
        t.context("saref", "https://saref.etsi.org/core/");
        t.context("om", "http://www.ontology-of-units-of-measure.org/resource/om-2/");
        t.property("temperature", &Thermometer::temperature)
            .doc("The measured temperature.")
            .read_only()
            .unit("om:degreeCelsius")
            .semantic_type("saref:Temperature");
        t.property("interval", &Thermometer::interval).doc("How often to measure.").gt(0).unit("om:second");
        t.action("measure", &Thermometer::measure)
            .doc("Take a measurement now.")
            .semantic_type("saref:GetCommand");
    }
};

int main() {
    try {
        const auto client = tw::testing::TestClient::start(
            tw::ThingServer::builder().thing<Thermometer>("thermometer").build());
        const auto td = client.get("/thermometer/").json();

        std::cout << "@context: " << tw::json::dump_pretty(td["@context"]) << "\n";
        std::cout << "@type:    " << tw::json::dump(td["@type"]) << "\n";
        for (const char* name : {"temperature", "interval"}) {
            const auto& property = td["properties"][name];
            std::cout << name << ": unit " << tw::json::dump(property["unit"]) << ", @type "
                      << (property.contains("@type") ? tw::json::dump(property["@type"])
                                                     : std::string("\"-\""))
                      << "\n";
        }
        std::cout << "measure: @type " << tw::json::dump(td["actions"]["measure"]["@type"]) << "\n";

        // The TD, with its additions, is still valid TD 1.1.
        tw::td::thing_description_from_json(td).validate();
#ifdef TETA_WOT_TD_VALIDATION
        tw::td::validation::validate_json(td);
        std::cout << "The TD validates against the W3C TD 1.1 JSON Schema.\n";
#else
        std::cout << "The TD follows TD 1.1's rules (built without teta::wot-td-validation, for the JSON "
                     "Schema).\n";
#endif
        if (td["properties"]["temperature"]["unit"] != "om:degreeCelsius") {
            throw std::runtime_error("the temperature's unit is missing");
        }
        client.stop();
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
