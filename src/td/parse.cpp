// Reading the TD model from JSON.

#include "json_io.hpp"
#include "teta/wot/td/thing.hpp"

namespace teta::wot::td {

namespace {

using detail::fail;
using detail::ObjectReader;
using detail::ReadError;

template <typename E, std::size_t N>
E enum_from(const json::Value& value, std::string_view key, const std::pair<E, const char*> (&names)[N]) {
    if (value.is_string()) {
        for (const auto& [e, name] : names) {
            if (value.get<std::string>() == name) {
                return e;
            }
        }
    }
    fail(key, "a known name", value);
}

constexpr std::pair<Operation, const char*> kOperations[] = {
    {Operation::readproperty, "readproperty"},
    {Operation::writeproperty, "writeproperty"},
    {Operation::observeproperty, "observeproperty"},
    {Operation::unobserveproperty, "unobserveproperty"},
    {Operation::invokeaction, "invokeaction"},
    {Operation::queryaction, "queryaction"},
    {Operation::cancelaction, "cancelaction"},
    {Operation::subscribeevent, "subscribeevent"},
    {Operation::unsubscribeevent, "unsubscribeevent"},
    {Operation::readallproperties, "readallproperties"},
    {Operation::writeallproperties, "writeallproperties"},
    {Operation::readmultipleproperties, "readmultipleproperties"},
    {Operation::writemultipleproperties, "writemultipleproperties"},
    {Operation::observeallproperties, "observeallproperties"},
    {Operation::unobserveallproperties, "unobserveallproperties"},
    {Operation::queryallactions, "queryallactions"},
    {Operation::subscribeallevents, "subscribeallevents"},
    {Operation::unsubscribeallevents, "unsubscribeallevents"},
};

constexpr std::pair<SecuritySchemeType, const char*> kSchemes[] = {
    {SecuritySchemeType::nosec, "nosec"},   {SecuritySchemeType::auto_, "auto"},
    {SecuritySchemeType::combo, "combo"},   {SecuritySchemeType::basic, "basic"},
    {SecuritySchemeType::digest, "digest"}, {SecuritySchemeType::apikey, "apikey"},
    {SecuritySchemeType::bearer, "bearer"}, {SecuritySchemeType::psk, "psk"},
    {SecuritySchemeType::oauth2, "oauth2"},
};

constexpr std::pair<CredentialLocation, const char*> kLocations[] = {
    {CredentialLocation::header, "header"}, {CredentialLocation::query, "query"},
    {CredentialLocation::body, "body"},     {CredentialLocation::cookie, "cookie"},
    {CredentialLocation::uri, "uri"},       {CredentialLocation::auto_, "auto"},
};

DataSchema schema(const json::Value& value) {
    try {
        return data_schema_from_json(value);
    } catch (const InvalidDataSchema& error) {
        throw ReadError{error.what()};
    }
}

std::optional<NamedSchemas> named_schemas(ObjectReader& r, std::string_view key) {
    const auto* v = r.get(key);
    if (!v) {
        return std::nullopt;
    }
    if (!v->is_object()) {
        fail(key, "an object of schemas", *v);
    }
    NamedSchemas out;
    for (const auto& [name, s] : v->items()) {
        out.emplace_back(name, schema(s));
    }
    return out;
}

std::optional<DataSchema> optional_schema(ObjectReader& r, std::string_view key) {
    const auto* v = r.get(key);
    return v ? std::optional<DataSchema>(schema(*v)) : std::nullopt;
}

Form read_form(const json::Value& value) {
    ObjectReader r(value, "a Form");
    Form form;
    const auto* href = r.get_required("href", "a Form");
    if (!href->is_string()) {
        fail("href", "a string", *href);
    }
    form.href = href->get<std::string>();
    if (const auto* op = r.get("op")) {
        if (op->is_array()) {
            std::vector<Operation> ops;
            for (const auto& o : *op) {
                ops.push_back(enum_from(o, "op", kOperations));
            }
            form.op.emplace(std::move(ops));
        } else {
            form.op.emplace(enum_from(*op, "op", kOperations));
        }
    }
    form.content_type = r.string("contentType");
    form.content_coding = r.string("contentCoding");
    form.subprotocol = r.string("subprotocol");
    form.security = r.one_or_many_strings("security");
    form.scopes = r.one_or_many_strings("scopes");
    if (const auto* response = r.get("response")) {
        ObjectReader rr(*response, "an ExpectedResponse");
        const auto* type = rr.get_required("contentType", "an ExpectedResponse");
        if (!type->is_string()) {
            fail("contentType", "a string", *type);
        }
        form.response = ExpectedResponse{type->get<std::string>(), rr.rest()};
    }
    if (const auto* responses = r.get("additionalResponses")) {
        if (!responses->is_array()) {
            fail("additionalResponses", "an array", *responses);
        }
        form.additional_responses.emplace();
        for (const auto& item : *responses) {
            ObjectReader rr(item, "an AdditionalExpectedResponse");
            AdditionalExpectedResponse response;
            response.success = rr.boolean("success");
            response.content_type = rr.string("contentType");
            response.schema = rr.string("schema");
            response.extra = rr.rest();
            form.additional_responses->push_back(std::move(response));
        }
    }
    form.extra = r.rest();
    return form;
}

std::vector<Form> read_forms(ObjectReader& r, std::string_view what) {
    const auto* forms = r.get_required("forms", what);
    if (!forms->is_array()) {
        fail("forms", "an array of forms", *forms);
    }
    std::vector<Form> out;
    for (const auto& form : *forms) {
        out.push_back(read_form(form));
    }
    return out;
}

Link read_link(const json::Value& value) {
    ObjectReader r(value, "a Link");
    Link link;
    const auto* href = r.get_required("href", "a Link");
    if (!href->is_string()) {
        fail("href", "a string", *href);
    }
    link.href = href->get<std::string>();
    link.media_type = r.string("type");
    link.rel = r.string("rel");
    link.anchor = r.string("anchor");
    link.sizes = r.string("sizes");
    link.hreflang = r.one_or_many_strings("hreflang");
    link.extra = r.rest();
    return link;
}

SecurityScheme read_scheme(const json::Value& value) {
    ObjectReader r(value, "a SecurityScheme");
    SecurityScheme s;
    s.semantic_type = r.one_or_many_strings("@type");
    s.description = r.string("description");
    s.descriptions = r.string_map("descriptions");
    s.proxy = r.string("proxy");
    s.scheme = enum_from(*r.get_required("scheme", "a SecurityScheme"), "scheme", kSchemes);
    if (const auto* in = r.get("in")) {
        s.location = enum_from(*in, "in", kLocations);
    }
    s.name = r.string("name");
    s.qop = r.string("qop");
    s.authorization = r.string("authorization");
    s.alg = r.string("alg");
    s.format = r.string("format");
    s.identity = r.string("identity");
    s.token = r.string("token");
    s.refresh = r.string("refresh");
    s.scopes = r.one_or_many_strings("scopes");
    s.flow = r.string("flow");
    s.one_of = r.strings("oneOf");
    s.all_of = r.strings("allOf");
    s.extra = r.rest();
    return s;
}

Context read_context(const json::Value& value) {
    if (value.is_string()) {
        return Context::of_uri(value.get<std::string>());
    }
    if (!value.is_array()) {
        fail("@context", "a URI or an array", value);
    }
    std::vector<ContextEntry> entries;
    for (const auto& entry : value) {
        if (entry.is_string()) {
            entries.push_back(ContextEntry::of_uri(entry.get<std::string>()));
        } else if (entry.is_object()) {
            std::vector<std::pair<std::string, std::string>> map;
            for (const auto& [k, v] : entry.items()) {
                if (!v.is_string()) {
                    fail("@context", "prefixes mapped to strings", entry);
                }
                map.emplace_back(k, v.get<std::string>());
            }
            entries.push_back(ContextEntry::of_prefixes(std::move(map)));
        } else {
            fail("@context", "URIs and prefix maps", value);
        }
    }
    return Context::of_entries(std::move(entries));
}

PropertyAffordance read_property(const json::Value& value) {
    ObjectReader r(value, "a PropertyAffordance");
    PropertyAffordance p;
    p.forms = read_forms(r, "a PropertyAffordance");
    p.uri_variables = named_schemas(r, "uriVariables");
    p.observable = r.boolean("observable");
    // The rest is the property's data schema (serde's `flatten`).
    p.schema = schema(r.rest());
    return p;
}

ActionAffordance read_action(const json::Value& value) {
    ObjectReader r(value, "an ActionAffordance");
    ActionAffordance a;
    a.description = r.string("description");
    a.descriptions = r.string_map("descriptions");
    a.title = r.string("title");
    a.titles = r.string_map("titles");
    a.forms = read_forms(r, "an ActionAffordance");
    a.uri_variables = named_schemas(r, "uriVariables");
    a.semantic_type = r.one_or_many_strings("@type");
    a.input = optional_schema(r, "input");
    a.output = optional_schema(r, "output");
    a.safe = r.boolean("safe");
    a.idempotent = r.boolean("idempotent");
    a.synchronous = r.boolean("synchronous");
    a.extra = r.rest();
    return a;
}

EventAffordance read_event(const json::Value& value) {
    ObjectReader r(value, "an EventAffordance");
    EventAffordance e;
    e.description = r.string("description");
    e.descriptions = r.string_map("descriptions");
    e.title = r.string("title");
    e.titles = r.string_map("titles");
    e.forms = read_forms(r, "an EventAffordance");
    e.uri_variables = named_schemas(r, "uriVariables");
    e.semantic_type = r.one_or_many_strings("@type");
    e.subscription = optional_schema(r, "subscription");
    e.data = optional_schema(r, "data");
    e.data_response = optional_schema(r, "dataResponse");
    e.cancellation = optional_schema(r, "cancellation");
    e.extra = r.rest();
    return e;
}

OneOrMany<std::string> strings_or_string(const json::Value& value, std::string_view key) {
    if (value.is_string()) {
        return value.get<std::string>();
    }
    if (value.is_array()) {
        std::vector<std::string> out;
        for (const auto& item : value) {
            if (!item.is_string()) {
                fail(key, "a string or strings", value);
            }
            out.push_back(item.get<std::string>());
        }
        return out;
    }
    fail(key, "a string or strings", value);
}

template <typename T, typename Read>
std::vector<std::pair<std::string, T>> named(ObjectReader& r, std::string_view key, Read read) {
    std::vector<std::pair<std::string, T>> out;
    const auto* v = r.get(key);
    if (!v) {
        return out;
    }
    if (!v->is_object()) {
        fail(key, "an object", *v);
    }
    for (const auto& [name, item] : v->items()) {
        out.emplace_back(name, read(item));
    }
    return out;
}

ThingDescription read_td(const json::Value& value) {
    ObjectReader r(value, "a Thing Description");
    ThingDescription td;
    td.id = r.string("id");
    const auto* title = r.get_required("title", "a Thing Description");
    if (!title->is_string()) {
        fail("title", "a string", *title);
    }
    td.title = title->get<std::string>();
    td.titles = r.string_map("titles");
    td.properties = named<PropertyAffordance>(r, "properties", read_property);
    td.actions = named<ActionAffordance>(r, "actions", read_action);
    td.events = named<EventAffordance>(r, "events", read_event);
    td.description = r.string("description");
    td.descriptions = r.string_map("descriptions");
    if (const auto* version = r.get("version")) {
        ObjectReader vr(*version, "version information");
        const auto* instance = vr.get_required("instance", "version information");
        if (!instance->is_string()) {
            fail("instance", "a string", *instance);
        }
        td.version = VersionInfo{instance->get<std::string>(), vr.string("model"), json::Value::object()};
        td.version->extra = vr.rest();
    }
    if (const auto* links = r.get("links")) {
        if (!links->is_array()) {
            fail("links", "an array", *links);
        }
        for (const auto& link : *links) {
            td.links.push_back(read_link(link));
        }
    }
    if (const auto* forms = r.get("forms")) {
        if (!forms->is_array()) {
            fail("forms", "an array", *forms);
        }
        for (const auto& form : *forms) {
            td.forms.push_back(read_form(form));
        }
    }
    td.base = r.string("base");
    const auto* definitions = r.get_required("securityDefinitions", "a Thing Description");
    if (!definitions->is_object()) {
        fail("securityDefinitions", "an object", *definitions);
    }
    for (const auto& [name, scheme] : definitions->items()) {
        td.security_definitions.emplace_back(name, read_scheme(scheme));
    }
    td.support = r.string("support");
    td.created = r.string("created");
    td.modified = r.string("modified");
    td.security = strings_or_string(*r.get_required("security", "a Thing Description"), "security");
    td.profile = r.one_or_many_strings("profile");
    td.schema_definitions = named_schemas(r, "schemaDefinitions");
    td.uri_variables = named_schemas(r, "uriVariables");
    td.semantic_type = r.one_or_many_strings("@type");
    td.context = read_context(*r.get_required("@context", "a Thing Description"));
    td.extra = r.rest();
    return td;
}

template <typename F>
auto parse(F read, const json::Value& value) {
    try {
        return read(value);
    } catch (const ReadError& error) {
        throw TdParseError(error.message);
    }
}

}  // namespace

Form form_from_json(const json::Value& value) {
    return parse(read_form, value);
}
Link link_from_json(const json::Value& value) {
    return parse(read_link, value);
}
SecurityScheme security_scheme_from_json(const json::Value& value) {
    return parse(read_scheme, value);
}
Context context_from_json(const json::Value& value) {
    return parse(read_context, value);
}
PropertyAffordance property_from_json(const json::Value& value) {
    return parse(read_property, value);
}
ActionAffordance action_from_json(const json::Value& value) {
    return parse(read_action, value);
}
EventAffordance event_from_json(const json::Value& value) {
    return parse(read_event, value);
}
ThingDescription thing_description_from_json(const json::Value& value) {
    return parse(read_td, value);
}

}  // namespace teta::wot::td
