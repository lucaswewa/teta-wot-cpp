// Builds the Thing Description of an LED illuminator by hand, prints it, and
// validates it: first the TD rules the builder checks, then the W3C TD 1.1
// JSON Schema.

#include <cstdint>
#include <iostream>
#include <teta/wot.hpp>
#include <teta/wot/td/validation.hpp>
#include <vector>

namespace tw = teta::wot;
namespace td = teta::wot::td;

namespace {

// `/{thing}/{name}` with the given operations.
td::Form form(const std::string& name, std::vector<td::Operation> ops) {
    td::Form f("/illuminator/" + name);
    f.with_op(td::OneOrMany<td::Operation>(std::move(ops)));
    return f;
}

void run() {
    // A data schema comes from a type, and constraints narrow it.
    auto brightness = tw::for_type<std::uint8_t>();
    td::Constraints{}.le(100).apply(brightness);

    auto output = tw::for_type<bool>();
    output.with_description("Whether the LED is now on.");
    auto overheat = tw::for_type<double>();
    overheat.with_unit("degree Celsius");

    td::ThingBuilder builder("Illuminator");
    builder.id("urn:uuid:6f1e4bd2-2f3c-5b1a-9d7e-3c1f0a2b4c5d")
        .description("A dimmable LED illuminator for a microscope.")
        .base("http://localhost:5000/")
        // Semantic annotations need a prefix in the @context.
        .context_prefix("saref", "https://saref.etsi.org/core/")
        .semantic_type("saref:Actuator")
        .property("brightness",
                  td::PropertyBuilder(brightness)
                      .title("Brightness")
                      .description("LED brightness, as a percentage of full power.")
                      .unit("percent")
                      .default_value(50)
                      .form(form("brightness", {td::Operation::readproperty, td::Operation::writeproperty})))
        .property("temperature", td::PropertyBuilder(tw::for_type<double>())
                                     .title("LED temperature")
                                     .read_only(true)
                                     .unit("degree Celsius")
                                     .semantic_type("saref:Temperature")
                                     .form(form("temperature", {td::Operation::readproperty})))
        .action("toggle", td::ActionBuilder()
                              .title("Switch the LED on or off.")
                              .output(output)
                              .form(form("toggle", {td::Operation::invokeaction})))
        .event("overheated",
               td::EventBuilder()
                   .title("The LED got too hot and was switched off.")
                   .data(overheat)
                   .form(form("overheated", {td::Operation::subscribeevent, td::Operation::unsubscribeevent})
                             .with_subprotocol("sse")
                             .with_content_type("text/event-stream")))
        .link(td::Link("/camera/mjpeg_stream")
                  .with_rel("alternate")
                  .with_media_type("multipart/x-mixed-replace"));
    const auto description = builder.build();

    std::cout << tw::json::dump_pretty(td::to_json(description)) << '\n';

    // The builder already checked the TD rules; now the W3C JSON Schema.
    td::validation::validate_schema(description);
    std::cerr << "\nThe TD validates against the W3C TD 1.1 JSON Schema.\n";

    // The builder refuses TDs that break the rules, for example a form that
    // uses an action operation on a property.
    try {
        td::ThingBuilder("Broken")
            .property("brightness", td::PropertyBuilder(tw::for_type<std::uint8_t>())
                                        .form(form("brightness", {td::Operation::invokeaction})))
            .build();
        throw std::runtime_error("a property form can't invoke an action, but the TD was built");
    } catch (const td::TdError& error) {
        if (error.kind() != td::TdError::Kind::misplaced_operation) {
            throw;
        }
        std::cerr << "A TD with a misplaced operation is refused: " << error.what() << '\n';
    }
}

}  // namespace

int main() {
    try {
        run();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
