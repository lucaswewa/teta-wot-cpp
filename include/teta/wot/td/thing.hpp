// Interaction affordances and the Thing Description document, with builders
// that check the TD 1.1 rules the types can't.

#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "teta/wot/json.hpp"
#include "teta/wot/td/data_schema.hpp"
#include "teta/wot/td/form.hpp"

namespace teta::wot::td {

/// A Thing Description breaks a TD 1.1 rule the types don't enforce.
class TdError : public std::runtime_error {
  public:
    enum class Kind {
        invalid_context,
        no_security_definitions,
        missing_security,
        undefined_security,
        duplicate_affordance,
        empty_name,
        no_forms,
        misplaced_operation,
    };

    TdError(Kind kind, std::string message) : std::runtime_error(std::move(message)), kind_(kind) {}
    Kind kind() const { return kind_; }

  private:
    Kind kind_;
};

/// JSON that doesn't fit the TD model (a TD, or part of one, being read).
class TdParseError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

/// Named DataSchemas, in order (`uriVariables`, `schemaDefinitions`).
using NamedSchemas = std::vector<std::pair<std::string, DataSchema>>;

/// A TD `PropertyAffordance` (TD 1.1 §5.3.1.2). A property affordance *is* a
/// data schema, so its data-schema terms, title and description included,
/// are in `schema`.
struct PropertyAffordance {
    DataSchema schema;
    std::vector<Form> forms;
    std::optional<NamedSchemas> uri_variables;
    std::optional<bool> observable;
    bool operator==(const PropertyAffordance&) const = default;
};

/// A TD `ActionAffordance` (TD 1.1 §5.3.1.3).
struct ActionAffordance {
    std::optional<std::string> description;
    std::optional<MultiLanguage> descriptions;
    std::optional<std::string> title;
    std::optional<MultiLanguage> titles;
    std::vector<Form> forms;
    std::optional<NamedSchemas> uri_variables;
    std::optional<OneOrMany<std::string>> semantic_type;
    std::optional<DataSchema> input;
    std::optional<DataSchema> output;
    std::optional<bool> safe;
    std::optional<bool> idempotent;
    std::optional<bool> synchronous;
    json::Value extra = json::Value::object();
    bool operator==(const ActionAffordance&) const = default;
};

/// A TD `EventAffordance` (TD 1.1 §5.3.1.4).
struct EventAffordance {
    std::optional<std::string> description;
    std::optional<MultiLanguage> descriptions;
    std::optional<std::string> title;
    std::optional<MultiLanguage> titles;
    std::vector<Form> forms;
    std::optional<NamedSchemas> uri_variables;
    std::optional<OneOrMany<std::string>> semantic_type;
    std::optional<DataSchema> subscription;
    std::optional<DataSchema> data;
    std::optional<DataSchema> data_response;
    std::optional<DataSchema> cancellation;
    json::Value extra = json::Value::object();
    bool operator==(const EventAffordance&) const = default;
};

json::Value to_json(const PropertyAffordance& property);
json::Value to_json(const ActionAffordance& action);
json::Value to_json(const EventAffordance& event);

/// Builds a PropertyAffordance. `readOnly` and `writeOnly` are always
/// written; both start as `false`.
class PropertyBuilder {
  public:
    explicit PropertyBuilder(DataSchema schema);

    PropertyBuilder& title(std::string text);
    PropertyBuilder& description(std::string text);
    PropertyBuilder& read_only(bool value);
    PropertyBuilder& write_only(bool value);
    PropertyBuilder& observable(bool value);
    /// A `null` default is not written.
    PropertyBuilder& default_value(json::Value value);
    PropertyBuilder& unit(std::string text);
    PropertyBuilder& semantic_type(std::string text);
    PropertyBuilder& form(Form form);
    PropertyBuilder& uri_variable(std::string name, DataSchema schema);
    PropertyAffordance build();

  private:
    PropertyAffordance property_;
};

/// Builds an ActionAffordance.
class ActionBuilder {
  public:
    ActionBuilder& title(std::string text);
    ActionBuilder& description(std::string text);
    ActionBuilder& input(DataSchema schema);
    ActionBuilder& output(DataSchema schema);
    ActionBuilder& safe(bool value);
    ActionBuilder& idempotent(bool value);
    ActionBuilder& synchronous(bool value);
    ActionBuilder& semantic_type(std::string text);
    ActionBuilder& form(Form form);
    ActionBuilder& uri_variable(std::string name, DataSchema schema);
    ActionAffordance build();

  private:
    ActionAffordance action_;
};

/// Builds an EventAffordance.
class EventBuilder {
  public:
    EventBuilder& title(std::string text);
    EventBuilder& description(std::string text);
    EventBuilder& data(DataSchema schema);
    EventBuilder& subscription(DataSchema schema);
    EventBuilder& cancellation(DataSchema schema);
    EventBuilder& semantic_type(std::string text);
    EventBuilder& form(Form form);
    EventAffordance build();

  private:
    EventAffordance event_;
};

/// The name of the security scheme a builder adds when none is given.
inline constexpr const char* no_security_name = "no_security";

/// Version information (`version`).
struct VersionInfo {
    std::string instance;
    std::optional<std::string> model;
    json::Value extra = json::Value::object();
    bool operator==(const VersionInfo&) const = default;
};

/// A W3C WoT Thing Description, TD 1.1 (§5.3.1.1).
struct ThingDescription {
    std::optional<std::string> id;
    std::string title;
    std::optional<MultiLanguage> titles;
    std::vector<std::pair<std::string, PropertyAffordance>> properties;
    std::vector<std::pair<std::string, ActionAffordance>> actions;
    std::vector<std::pair<std::string, EventAffordance>> events;
    std::optional<std::string> description;
    std::optional<MultiLanguage> descriptions;
    std::optional<VersionInfo> version;
    std::vector<Link> links;
    std::vector<Form> forms;
    std::optional<std::string> base;
    std::vector<std::pair<std::string, SecurityScheme>> security_definitions;
    std::optional<std::string> support;
    std::optional<std::string> created;
    std::optional<std::string> modified;
    OneOrMany<std::string> security = std::vector<std::string>{};
    std::optional<OneOrMany<std::string>> profile;
    std::optional<NamedSchemas> schema_definitions;
    std::optional<NamedSchemas> uri_variables;
    std::optional<OneOrMany<std::string>> semantic_type;
    Context context;
    json::Value extra = json::Value::object();

    bool operator==(const ThingDescription&) const = default;

    /// Checks the rules the types don't: the `@context` rules, at least one
    /// security definition and every security name defined, every
    /// affordance named and with a form, and every form's operations where
    /// they belong. Throws TdError. (Validation against the W3C JSON Schema
    /// is separate.)
    void validate() const;
};

/// The TD as JSON, in teta-wot's order.
json::Value to_json(const ThingDescription& td);

// Reading the model from JSON, as teta-wot's serde model does: required
// members must be there, `null` reads as absent, and terms the model doesn't
// define are kept in `extra`. They throw TdParseError.
Form form_from_json(const json::Value& value);
Link link_from_json(const json::Value& value);
SecurityScheme security_scheme_from_json(const json::Value& value);
Context context_from_json(const json::Value& value);
PropertyAffordance property_from_json(const json::Value& value);
ActionAffordance action_from_json(const json::Value& value);
EventAffordance event_from_json(const json::Value& value);
ThingDescription thing_description_from_json(const json::Value& value);

/// Builds a ThingDescription and checks it. Without security definitions,
/// the TD gets `"security": "no_security"` with a `nosec` scheme described
/// as "No security".
class ThingBuilder {
  public:
    explicit ThingBuilder(std::string title);

    ThingBuilder& id(std::string value);
    ThingBuilder& description(std::string text);
    ThingBuilder& base(std::string uri);
    ThingBuilder& profile(OneOrMany<std::string> profiles);
    ThingBuilder& version(std::string instance);
    ThingBuilder& semantic_type(std::string text);
    ThingBuilder& context_entry(ContextEntry entry);
    ThingBuilder& context_prefix(const std::string& prefix, const std::string& uri);
    ThingBuilder& property(std::string name, PropertyAffordance property);
    ThingBuilder& property(std::string name, PropertyBuilder property) {
        return this->property(std::move(name), property.build());
    }
    ThingBuilder& action(std::string name, ActionAffordance action);
    ThingBuilder& action(std::string name, ActionBuilder action) {
        return this->action(std::move(name), action.build());
    }
    ThingBuilder& event(std::string name, EventAffordance event);
    ThingBuilder& event(std::string name, EventBuilder event) {
        return this->event(std::move(name), event.build());
    }
    ThingBuilder& link(Link link);
    ThingBuilder& form(Form form);
    ThingBuilder& security_definition(std::string name, SecurityScheme scheme);
    ThingBuilder& security(OneOrMany<std::string> names);
    ThingBuilder& term(const std::string& name, json::Value value);

    /// Finishes the TD and checks it; throws TdError.
    ThingDescription build();

  private:
    void note_duplicate(OperationScope scope, const std::string& name);

    ThingDescription td_;
    bool security_set_ = false;
    std::optional<TdError> duplicate_;
};

}  // namespace teta::wot::td
