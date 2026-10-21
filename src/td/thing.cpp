#include "teta/wot/td/thing.hpp"

#include "json_io.hpp"

namespace teta::wot::td {

namespace {

json::Value named_schemas(const NamedSchemas& schemas) {
    json::Value out = json::Value::object();
    for (const auto& [name, schema] : schemas) {
        out[name] = to_json(schema);
    }
    return out;
}

json::Value forms_json(const std::vector<Form>& forms) {
    json::Value out = json::Value::array();
    for (const auto& form : forms) {
        out.push_back(to_json(form));
    }
    return out;
}

void put_schema(json::Value& out, const char* key, const std::optional<DataSchema>& schema) {
    if (schema) {
        out[key] = to_json(*schema);
    }
}

void put_multi(json::Value& out, const char* key, const std::optional<MultiLanguage>& text) {
    if (text) {
        out[key] = detail::pairs_to_json(*text);
    }
}

template <typename T>
bool insert(std::vector<std::pair<std::string, T>>& entries, std::string name, T value) {
    for (auto& [n, v] : entries) {
        if (n == name) {
            v = std::move(value);
            return false;
        }
    }
    entries.emplace_back(std::move(name), std::move(value));
    return true;
}

void check_security_names(const ThingDescription& td, const std::vector<std::string>& names) {
    for (const auto& name : names) {
        bool defined = false;
        for (const auto& [n, _] : td.security_definitions) {
            defined = defined || n == name;
        }
        if (!defined) {
            throw TdError(TdError::Kind::undefined_security,
                          "security scheme `" + name + "` is used but not defined in securityDefinitions");
        }
    }
}

void check_forms(const ThingDescription& td, OperationScope where, const std::string& name,
                 const std::vector<Form>& forms) {
    for (const auto& form : forms) {
        for (const auto op : form.operations()) {
            if (scope(op) != where) {
                throw TdError(TdError::Kind::misplaced_operation, std::string("operation `") + to_string(op) +
                                                                      "` can't be used in the forms of " +
                                                                      to_string(where) + " `" + name + "`");
            }
        }
        if (form.security) {
            check_security_names(td, form.security->values());
        }
    }
}

template <typename Affordance>
void check_affordances(const ThingDescription& td, OperationScope where,
                       const std::vector<std::pair<std::string, Affordance>>& affordances) {
    for (const auto& [name, affordance] : affordances) {
        if (name.empty()) {
            throw TdError(TdError::Kind::empty_name,
                          std::string("a ") + to_string(where) + " affordance has an empty name");
        }
        if (affordance.forms.empty()) {
            throw TdError(TdError::Kind::no_forms,
                          std::string(to_string(where)) + " affordance `" + name + "` has no forms");
        }
        check_forms(td, where, name, affordance.forms);
    }
}

}  // namespace

// ---- JSON -------------------------------------------------------------------

json::Value to_json(const PropertyAffordance& p) {
    // The schema's typed terms, then the property's own, then the schema's
    // extension terms.
    json::Value out = json::Value::object();
    const json::Value schema = to_json(p.schema);
    for (const auto& [key, value] : schema.items()) {
        if (!p.schema.extra.contains(key)) {
            out[key] = value;
        }
    }
    out["forms"] = forms_json(p.forms);
    if (p.uri_variables) {
        out["uriVariables"] = named_schemas(*p.uri_variables);
    }
    detail::put(out, "observable", p.observable);
    detail::put_extra(out, p.schema.extra);
    return out;
}

json::Value to_json(const ActionAffordance& a) {
    using detail::put;
    json::Value out = json::Value::object();
    put(out, "description", a.description);
    put_multi(out, "descriptions", a.descriptions);
    put(out, "title", a.title);
    put_multi(out, "titles", a.titles);
    out["forms"] = forms_json(a.forms);
    if (a.uri_variables) {
        out["uriVariables"] = named_schemas(*a.uri_variables);
    }
    put(out, "@type", a.semantic_type);
    put_schema(out, "input", a.input);
    put_schema(out, "output", a.output);
    put(out, "safe", a.safe);
    put(out, "idempotent", a.idempotent);
    put(out, "synchronous", a.synchronous);
    detail::put_extra(out, a.extra);
    return out;
}

json::Value to_json(const EventAffordance& e) {
    using detail::put;
    json::Value out = json::Value::object();
    put(out, "description", e.description);
    put_multi(out, "descriptions", e.descriptions);
    put(out, "title", e.title);
    put_multi(out, "titles", e.titles);
    out["forms"] = forms_json(e.forms);
    if (e.uri_variables) {
        out["uriVariables"] = named_schemas(*e.uri_variables);
    }
    put(out, "@type", e.semantic_type);
    put_schema(out, "subscription", e.subscription);
    put_schema(out, "data", e.data);
    put_schema(out, "dataResponse", e.data_response);
    put_schema(out, "cancellation", e.cancellation);
    detail::put_extra(out, e.extra);
    return out;
}

json::Value to_json(const ThingDescription& td) {
    using detail::put;
    json::Value out = json::Value::object();
    put(out, "id", td.id);
    out["title"] = td.title;
    put_multi(out, "titles", td.titles);
    json::Value properties = json::Value::object();
    for (const auto& [name, property] : td.properties) {
        properties[name] = to_json(property);
    }
    out["properties"] = std::move(properties);
    json::Value actions = json::Value::object();
    for (const auto& [name, action] : td.actions) {
        actions[name] = to_json(action);
    }
    out["actions"] = std::move(actions);
    if (!td.events.empty()) {
        json::Value events = json::Value::object();
        for (const auto& [name, event] : td.events) {
            events[name] = to_json(event);
        }
        out["events"] = std::move(events);
    }
    put(out, "description", td.description);
    put_multi(out, "descriptions", td.descriptions);
    if (td.version) {
        json::Value version = json::Value::object();
        version["instance"] = td.version->instance;
        put(version, "model", td.version->model);
        detail::put_extra(version, td.version->extra);
        out["version"] = std::move(version);
    }
    if (!td.links.empty()) {
        json::Value links = json::Value::array();
        for (const auto& link : td.links) {
            links.push_back(to_json(link));
        }
        out["links"] = std::move(links);
    }
    if (!td.forms.empty()) {
        out["forms"] = forms_json(td.forms);
    }
    put(out, "base", td.base);
    json::Value definitions = json::Value::object();
    for (const auto& [name, scheme] : td.security_definitions) {
        definitions[name] = to_json(scheme);
    }
    out["securityDefinitions"] = std::move(definitions);
    put(out, "support", td.support);
    put(out, "created", td.created);
    put(out, "modified", td.modified);
    out["security"] = detail::to_json(td.security);
    put(out, "profile", td.profile);
    if (td.schema_definitions) {
        out["schemaDefinitions"] = named_schemas(*td.schema_definitions);
    }
    if (td.uri_variables) {
        out["uriVariables"] = named_schemas(*td.uri_variables);
    }
    put(out, "@type", td.semantic_type);
    out["@context"] = to_json(td.context);
    detail::put_extra(out, td.extra);
    return out;
}

// ---- Checking ---------------------------------------------------------------

void ThingDescription::validate() const {
    context.validate();
    if (security_definitions.empty()) {
        throw TdError(TdError::Kind::no_security_definitions, "the TD has no security definitions");
    }
    check_security_names(*this, security.values());
    for (const auto& [_, scheme] : security_definitions) {
        check_security_names(*this, scheme.referenced_schemes());
    }
    check_affordances(*this, OperationScope::property, properties);
    check_affordances(*this, OperationScope::action, actions);
    check_affordances(*this, OperationScope::event, events);
    check_forms(*this, OperationScope::thing, "", forms);
}

// ---- Affordance builders -----------------------------------------------------

PropertyBuilder::PropertyBuilder(DataSchema schema) {
    property_.schema = std::move(schema);
    property_.schema.read_only = false;
    property_.schema.write_only = false;
}

PropertyBuilder& PropertyBuilder::title(std::string text) {
    property_.schema.title = std::move(text);
    return *this;
}
PropertyBuilder& PropertyBuilder::description(std::string text) {
    property_.schema.description = std::move(text);
    return *this;
}
PropertyBuilder& PropertyBuilder::read_only(bool value) {
    property_.schema.read_only = value;
    return *this;
}
PropertyBuilder& PropertyBuilder::write_only(bool value) {
    property_.schema.write_only = value;
    return *this;
}
PropertyBuilder& PropertyBuilder::observable(bool value) {
    property_.observable = value;
    return *this;
}
PropertyBuilder& PropertyBuilder::default_value(json::Value value) {
    property_.schema.default_value = std::move(value);
    return *this;
}
PropertyBuilder& PropertyBuilder::unit(std::string text) {
    property_.schema.unit = std::move(text);
    return *this;
}
PropertyBuilder& PropertyBuilder::semantic_type(std::string text) {
    push_semantic_type(property_.schema.semantic_type, std::move(text));
    return *this;
}
PropertyBuilder& PropertyBuilder::form(Form f) {
    property_.forms.push_back(std::move(f));
    return *this;
}
PropertyBuilder& PropertyBuilder::uri_variable(std::string name, DataSchema schema) {
    if (!property_.uri_variables) {
        property_.uri_variables.emplace();
    }
    insert(*property_.uri_variables, std::move(name), std::move(schema));
    return *this;
}
PropertyAffordance PropertyBuilder::build() {
    return std::move(property_);
}

ActionBuilder& ActionBuilder::title(std::string text) {
    action_.title = std::move(text);
    return *this;
}
ActionBuilder& ActionBuilder::description(std::string text) {
    action_.description = std::move(text);
    return *this;
}
ActionBuilder& ActionBuilder::input(DataSchema schema) {
    action_.input = std::move(schema);
    return *this;
}
ActionBuilder& ActionBuilder::output(DataSchema schema) {
    action_.output = std::move(schema);
    return *this;
}
ActionBuilder& ActionBuilder::safe(bool value) {
    action_.safe = value;
    return *this;
}
ActionBuilder& ActionBuilder::idempotent(bool value) {
    action_.idempotent = value;
    return *this;
}
ActionBuilder& ActionBuilder::synchronous(bool value) {
    action_.synchronous = value;
    return *this;
}
ActionBuilder& ActionBuilder::semantic_type(std::string text) {
    push_semantic_type(action_.semantic_type, std::move(text));
    return *this;
}
ActionBuilder& ActionBuilder::form(Form f) {
    action_.forms.push_back(std::move(f));
    return *this;
}
ActionBuilder& ActionBuilder::uri_variable(std::string name, DataSchema schema) {
    if (!action_.uri_variables) {
        action_.uri_variables.emplace();
    }
    insert(*action_.uri_variables, std::move(name), std::move(schema));
    return *this;
}
ActionAffordance ActionBuilder::build() {
    return std::move(action_);
}

EventBuilder& EventBuilder::title(std::string text) {
    event_.title = std::move(text);
    return *this;
}
EventBuilder& EventBuilder::description(std::string text) {
    event_.description = std::move(text);
    return *this;
}
EventBuilder& EventBuilder::data(DataSchema schema) {
    event_.data = std::move(schema);
    return *this;
}
EventBuilder& EventBuilder::subscription(DataSchema schema) {
    event_.subscription = std::move(schema);
    return *this;
}
EventBuilder& EventBuilder::cancellation(DataSchema schema) {
    event_.cancellation = std::move(schema);
    return *this;
}
EventBuilder& EventBuilder::semantic_type(std::string text) {
    push_semantic_type(event_.semantic_type, std::move(text));
    return *this;
}
EventBuilder& EventBuilder::form(Form f) {
    event_.forms.push_back(std::move(f));
    return *this;
}
EventAffordance EventBuilder::build() {
    return std::move(event_);
}

// ---- The Thing builder ---------------------------------------------------------

ThingBuilder::ThingBuilder(std::string title) {
    td_.title = std::move(title);
}

ThingBuilder& ThingBuilder::id(std::string value) {
    td_.id = std::move(value);
    return *this;
}
ThingBuilder& ThingBuilder::description(std::string text) {
    td_.description = std::move(text);
    return *this;
}
ThingBuilder& ThingBuilder::base(std::string uri) {
    td_.base = std::move(uri);
    return *this;
}
ThingBuilder& ThingBuilder::profile(OneOrMany<std::string> profiles) {
    td_.profile = std::move(profiles);
    return *this;
}
ThingBuilder& ThingBuilder::version(std::string instance) {
    td_.version = VersionInfo{std::move(instance), std::nullopt, json::Value::object()};
    return *this;
}
ThingBuilder& ThingBuilder::semantic_type(std::string text) {
    push_semantic_type(td_.semantic_type, std::move(text));
    return *this;
}
ThingBuilder& ThingBuilder::context_entry(ContextEntry entry) {
    td_.context.push(std::move(entry));
    return *this;
}
ThingBuilder& ThingBuilder::context_prefix(const std::string& prefix, const std::string& uri) {
    td_.context.add_prefix(prefix, uri);
    return *this;
}
ThingBuilder& ThingBuilder::property(std::string name, PropertyAffordance property) {
    if (!insert(td_.properties, name, std::move(property))) {
        note_duplicate(OperationScope::property, name);
    }
    return *this;
}
ThingBuilder& ThingBuilder::action(std::string name, ActionAffordance action) {
    if (!insert(td_.actions, name, std::move(action))) {
        note_duplicate(OperationScope::action, name);
    }
    return *this;
}
ThingBuilder& ThingBuilder::event(std::string name, EventAffordance event) {
    if (!insert(td_.events, name, std::move(event))) {
        note_duplicate(OperationScope::event, name);
    }
    return *this;
}
ThingBuilder& ThingBuilder::link(Link l) {
    td_.links.push_back(std::move(l));
    return *this;
}
ThingBuilder& ThingBuilder::form(Form f) {
    td_.forms.push_back(std::move(f));
    return *this;
}
ThingBuilder& ThingBuilder::security_definition(std::string name, SecurityScheme scheme) {
    insert(td_.security_definitions, std::move(name), std::move(scheme));
    return *this;
}
ThingBuilder& ThingBuilder::security(OneOrMany<std::string> names) {
    td_.security = std::move(names);
    security_set_ = true;
    return *this;
}
ThingBuilder& ThingBuilder::term(const std::string& name, json::Value value) {
    td_.extra[name] = std::move(value);
    return *this;
}

void ThingBuilder::note_duplicate(OperationScope where, const std::string& name) {
    if (!duplicate_) {
        duplicate_.emplace(TdError::Kind::duplicate_affordance,
                           std::string(to_string(where)) + " affordance `" + name + "` is defined twice");
    }
}

ThingDescription ThingBuilder::build() {
    if (duplicate_) {
        throw *duplicate_;
    }
    if (td_.security_definitions.empty() && !security_set_) {
        td_.security_definitions.emplace_back(no_security_name,
                                              SecurityScheme::nosec().with_description("No security"));
        td_.security = OneOrMany<std::string>(std::string(no_security_name));
    } else if (!security_set_) {
        throw TdError(TdError::Kind::missing_security,
                      "security definitions were added, but `security` wasn't set");
    }
    td_.validate();
    return std::move(td_);
}

}  // namespace teta::wot::td
