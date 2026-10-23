// The OpenAPI 3.1 document: a port of teta-wot-http's `openapi.rs`. Keys are
// written in the order teta-wot writes them, since the document is compared
// byte for byte. Constant parts are JSON text, as `json!` writes them in
// teta-wot; the rest is put together with `obj`.

#include "teta/wot/http/openapi.hpp"

#include <algorithm>
#include <initializer_list>
#include <set>
#include <utility>
#include <vector>

namespace teta::wot::http {

namespace {

using json::Value;

/// The content type of a successful MJPEG response.
constexpr std::string_view mjpeg = "multipart/x-mixed-replace; boundary=frame";

constexpr std::string_view action_notice =
    "## Important note\n\nThis `POST` request starts an action: the server may carry on after answering. The "
    "answer is always a 201 with the invocation, whose `href` can be polled to follow it, and whose `output` "
    "link gives the result when it has completed.";
constexpr std::string_view asynchronous_notice =
    "## Important note\n\nThis `POST` request starts an action: the server may carry on after answering. The "
    "answer is a 201 with the invocation's `ActionStatus`, whose `href` (also in `Location`) can be polled "
    "to "
    "follow it; its `output` is there once it has completed.";
constexpr std::string_view synchronous_notice =
    "## Important note\n\nThis action is synchronous: the answer comes when it has finished, with its "
    "output.";

/// An object with these members, in this order.
Value obj(std::initializer_list<std::pair<std::string_view, Value>> members) {
    Value out = Value::object();
    for (const auto& [key, value] : members) {
        out[std::string(key)] = value;
    }
    return out;
}

/// JSON text, for the constant parts.
Value j(std::string_view text) {
    return json::parse(text);
}

Value reference(std::string_view name) {
    return obj({{"$ref", "#/components/schemas/" + std::string(name)}});
}

Value json_content(Value schema) {
    return obj({{"application/json", obj({{"schema", std::move(schema)}})}});
}

/// A response with a description and content.
Value response(std::string_view description, Value content) {
    return obj({{"description", description}, {"content", std::move(content)}});
}

/// A `200` with a description and content.
Value ok(std::string_view description, Value content) {
    return obj({{"200", response(description, std::move(content))}});
}

/// FastAPI's 422 answer.
Value validation_error() {
    return response("Validation Error", json_content(reference("HTTPValidationError")));
}

/// An error answer in the `wot` profile: problem details.
Value problem(std::string_view description) {
    return response(description,
                    obj({{"application/problem+json", obj({{"schema", reference("Problem")}})}}));
}

/// A success without a body (`wot` profile).
Value no_content(std::string_view description) {
    return obj({{"description", description}});
}

/// A `{uuid}` path parameter.
Value uuid_parameter(std::string_view name) {
    Value parameter = obj({{"name", name}});
    parameter.update(
        j(R"({"in": "path", "required": true, "schema": {"type": "string", "format": "uuid"}})"));
    return Value::array({std::move(parameter)});
}

/// Server-sent events of strings.
Value event_stream(std::string_view description) {
    return ok(description, j(R"({"text/event-stream": {"schema": {"type": "string"}}})"));
}

/// A route's default summary: its name in title case (`readings_csv` is
/// `Readings Csv`). Only ASCII letters change case.
std::string default_summary(std::string_view name) {
    std::string out;
    for (std::size_t start = 0; start <= name.size();) {
        auto end = name.find('_', start);
        if (end == std::string_view::npos) {
            end = name.size();
        }
        const auto word = name.substr(start, end - start);
        if (!word.empty()) {
            if (!out.empty()) {
                out += ' ';
            }
            for (std::size_t i = 0; i < word.size(); ++i) {
                const char c = word[i];
                if (i == 0 && c >= 'a' && c <= 'z') {
                    out += static_cast<char>(c - 'a' + 'A');
                } else if (i > 0 && c >= 'A' && c <= 'Z') {
                    out += static_cast<char>(c - 'A' + 'a');
                } else {
                    out += c;
                }
            }
        }
        start = end + 1;
    }
    return out;
}

/// Whether a JSON Schema allows `null` (an action without parameters).
bool accepts_null(const Value& schema) {
    if (!schema.is_object()) {
        return false;
    }
    if (const auto type = schema.find("type"); type != schema.end() && *type == "null") {
        return true;
    }
    for (const char* key : {"oneOf", "anyOf"}) {
        if (const auto branches = schema.find(key); branches != schema.end() && branches->is_array()) {
            if (std::ranges::any_of(*branches, accepts_null)) {
                return true;
            }
        }
    }
    return false;
}

/// A tuple's `items` array becomes `prefixItems` (at the end of its object,
/// where teta-wot moves it).
void tuples_to_prefix_items(Value& value) {
    if (value.is_object()) {
        if (const auto items = value.find("items"); items != value.end() && items->is_array()) {
            Value moved = std::move(*items);
            value.erase("items");
            value["prefixItems"] = std::move(moved);
        }
        for (auto& [_, child] : value.items()) {
            tuples_to_prefix_items(child);
        }
    } else if (value.is_array()) {
        for (auto& child : value) {
            tuples_to_prefix_items(child);
        }
    }
}

/// A TD DataSchema as a JSON Schema 2020-12 schema, which OpenAPI 3.1 uses:
/// the same terms, except that a tuple's `items` array becomes
/// `prefixItems`.
Value json_schema(const td::DataSchema& schema) {
    Value value = td::to_json(schema);
    tuples_to_prefix_items(value);
    return value;
}

/// The properties every invocation has.
Value summary_properties() {
    return j(R"({
        "status": {"$ref": "#/components/schemas/InvocationStatus"},
        "id": {"type": "string", "format": "uuid"},
        "action": {"type": "string"},
        "href": {"type": "string", "format": "uri"},
        "timeStarted": {"oneOf": [{"type": "string", "format": "date-time"}, {"type": "null"}]},
        "timeRequested": {"oneOf": [{"type": "string", "format": "date-time"}, {"type": "null"}]},
        "timeCompleted": {"oneOf": [{"type": "string", "format": "date-time"}, {"type": "null"}]},
        "links": {"type": "array", "items": {"$ref": "#/components/schemas/LinkElement"}}
    })");
}

Value summary_required() {
    return j(R"(["status", "id", "action", "href", "timeStarted", "timeRequested", "timeCompleted"])");
}

/// One action's invocation: the summary, with its input, output, log and
/// error.
Value invocation_schema(const Value& input, const Value& output) {
    Value properties = summary_properties();
    properties["input"] = input;
    properties["output"] = obj({{"oneOf", Value::array({output, j(R"({"type": "null"})")})}});
    properties["log"] = j(R"({"type": "array", "items": {"$ref": "#/components/schemas/LogRecordModel"}})");
    properties["error"] =
        j(R"({"oneOf": [{"$ref": "#/components/schemas/ProblemDetails"}, {"type": "null"}]})");
    Value required = summary_required();
    required.push_back("input");
    required.push_back("log");
    return obj(
        {{"type", "object"}, {"properties", std::move(properties)}, {"required", std::move(required)}});
}

/// The schemas of the `wot` profile (`Problem` is also in the other, for
/// discovery).
Value wot_schemas() {
    return j(R"({
        "ActionStatus": {
            "type": "object",
            "properties": {
                "status": {"type": "string", "enum": ["pending", "running", "completed", "failed"]},
                "output": {},
                "error": {"$ref": "#/components/schemas/Problem"},
                "href": {"type": "string", "format": "uri"},
                "timeRequested": {"type": "string", "format": "date-time"},
                "timeEnded": {"type": "string", "format": "date-time"}
            },
            "required": ["status", "href", "timeRequested"],
            "description": "An invocation, as the W3C WoT HTTP Basic Profile describes it."
        },
        "Problem": {
            "type": "object",
            "properties": {
                "type": {"type": "string", "format": "uri-reference"},
                "title": {"type": "string"},
                "status": {"type": "integer"},
                "detail": {"type": "string"},
                "instance": {"type": "string"},
                "invalid-params": {"type": "array", "items": {
                    "type": "object",
                    "properties": {
                        "in": {"type": "string"},
                        "name": {"type": "string"},
                        "reason": {"type": "string"},
                        "code": {"type": "string"}
                    }
                }}
            },
            "additionalProperties": true,
            "description": "Problem details (RFC 9457). The framework's types are documented at https://github.com/lucaswewa/wot/blob/main/docs/problems.md."
        }
    })");
}

/// One action's `ActionStatus` (`wot` profile), with its output.
Value action_status_schema(const Value& output) {
    Value schema = wot_schemas()["ActionStatus"];
    schema["properties"]["output"] = output;
    return schema;
}

/// The schemas every document has.
Value shared_schemas() {
    Value schemas = j(R"({
        "InvocationStatus": {
            "type": "string",
            "enum": ["pending", "running", "completed", "cancelled", "error"],
            "description": "Where an invocation is in its life."
        }
    })");
    schemas["InvocationSummary"] = obj({{"type", "object"},
                                        {"properties", summary_properties()},
                                        {"required", summary_required()},
                                        {"description", "An invocation, as lists show it."}});
    schemas["Invocation"] = invocation_schema(Value::object(), Value::object());
    schemas.update(j(R"({
        "LinkElement": {
            "type": "object",
            "properties": {
                "href": {"type": "string"},
                "type": {"oneOf": [{"type": "string"}, {"type": "null"}]},
                "rel": {"oneOf": [{"type": "string"}, {"type": "null"}]},
                "anchor": {"oneOf": [{"type": "string"}, {"type": "null"}]}
            },
            "required": ["href"],
            "additionalProperties": true,
            "description": "A link: see https://www.w3.org/TR/wot-thing-description11/#link."
        },
        "LogRecordModel": {
            "type": "object",
            "properties": {
                "message": {"type": "string"},
                "levelname": {"type": "string"},
                "levelno": {"type": "integer"},
                "lineno": {"type": "integer"},
                "filename": {"type": "string"},
                "created": {"type": "string", "format": "date-time"},
                "exception_type": {"oneOf": [{"type": "string"}, {"type": "null"}]},
                "traceback": {"oneOf": [{"type": "string"}, {"type": "null"}]}
            },
            "required": ["message", "levelname", "levelno", "lineno", "filename", "created"],
            "description": "A message logged during an invocation."
        },
        "ProblemDetails": {
            "type": "object",
            "properties": {
                "detail": {"oneOf": [{"type": "string"}, {"type": "null"}]},
                "type": {"oneOf": [{"type": "string"}, {"type": "null"}]},
                "status": {"oneOf": [{"type": "integer"}, {"type": "null"}]},
                "title": {"oneOf": [{"type": "string"}, {"type": "null"}]},
                "instance": {"oneOf": [{"type": "string"}, {"type": "null"}]}
            },
            "additionalProperties": true,
            "description": "What went wrong in an invocation (RFC 9457 problem details)."
        },
        "HTTPValidationError": {
            "type": "object",
            "properties": {"detail": {"type": "array", "items": {"$ref": "#/components/schemas/ValidationError"}}}
        },
        "ValidationError": {
            "type": "object",
            "properties": {
                "loc": {"type": "array", "items": {"oneOf": [{"type": "string"}, {"type": "integer"}]}},
                "msg": {"type": "string"},
                "type": {"type": "string"},
                "input": {},
                "ctx": {"type": "object"}
            },
            "required": ["loc", "msg", "type"]
        },
        "ThingDescription": {
            "type": "object",
            "description": "A W3C Web of Things Thing Description (TD 1.1).",
            "externalDocs": {"url": "https://www.w3.org/TR/wot-thing-description11/"}
        }
    })"));
    return schemas;
}

class Document {
  public:
    Document(std::string prefix, WireProfile profile) : prefix_(std::move(prefix)), profile_(profile) {}

    bool wot() const { return profile_ == WireProfile::wot; }

    Value paths = Value::object();
    Value schemas = Value::object();

    /// The routes of the action manager, the Blob download and the lists of
    /// Things.
    void framework_routes() {
        const std::string invocations = prefix_ + "/action_invocations";
        const char* summary = wot() ? "ActionStatus" : "InvocationSummary";
        const char* full = wot() ? "ActionStatus" : "Invocation";
        operation(
            invocations, "GET", "list_all_invocations",
            obj({{"summary", "List All Invocations"},
                 {"description", "Every invocation the server is keeping, of every action."},
                 {"responses", ok("Successful Response",
                                  json_content(obj({{"type", "array"}, {"items", reference(summary)}})))}}));
        const std::string one = invocations + "/{id}";
        Value responses = ok("Successful Response", json_content(reference(full)));
        unknown_id(responses, "Invocation ID not found");
        operation(one, "GET", "action_invocation",
                  obj({{"summary", "Action Invocation"},
                       {"description", wot() ? "One invocation's `ActionStatus` (`queryaction`)."
                                             : "One invocation: its status, input, output, log and error."},
                       {"parameters", uuid_parameter("id")},
                       {"responses", std::move(responses)}}));
        if (wot()) {
            responses = obj({{"204", no_content("Cancel request sent")},
                             {"409", problem("Invocation may not be cancelled")}});
        } else {
            responses = obj({{"200", response("Cancel request sent", json_content(Value::object()))},
                             {"503", obj({{"description", "Invocation may not be cancelled"}})}});
        }
        unknown_id(responses, "Invocation ID not found");
        operation(
            one, "DELETE", "delete_invocation",
            obj({{"summary", "Delete Invocation"},
                 {"description",
                  "Cancel an invocation (`cancelaction`). The action stops at its next cancellation check."},
                 {"parameters", uuid_parameter("id")},
                 {"responses", std::move(responses)}}));
        responses = ok("Action invocation output", j(R"({"application/json": {"schema": {}}, "*/*": {}})"));
        if (wot()) {
            responses["404"] = problem("Invocation ID not found, or no output is available");
        } else {
            responses["503"] = obj({{"description", "No result is available for this invocation"}});
            unknown_id(responses, "Invocation ID not found");
        }
        operation(one + "/output", "GET", "action_invocation_output",
                  obj({{"summary", "Action Invocation Output"},
                       {"description",
                        "The output of a completed invocation. When the output is a Blob, this is its data."},
                       {"parameters", uuid_parameter("id")},
                       {"responses", std::move(responses)}}));
        responses = ok("Successful Response", j(R"({"*/*": {}})"));
        unknown_id(responses, "Blob not found");
        operation(prefix_ + "/blob/{blob_id}", "GET", "download_blob",
                  obj({{"summary", "Download Blob"},
                       {"description", "A Blob's data, while an invocation or the Thing still holds it."},
                       {"parameters", uuid_parameter("blob_id")},
                       {"responses", std::move(responses)}}));
        operation(prefix_ + "/thing_descriptions/", "GET", "thing_descriptions",
                  obj({{"summary", "Thing Descriptions"},
                       {"description", "The Thing Description of every Thing, by name."},
                       {"responses",
                        ok("Successful Response",
                           json_content(j(R"({"type": "object", "additionalProperties": )"
                                          R"({"$ref": "#/components/schemas/ThingDescription"}})")))}}));
        operation(prefix_ + "/things/", "GET", "thing_paths",
                  obj({{"summary", "Thing Paths"},
                       {"description", "The URL of every Thing's Thing Description, by name."},
                       {"responses", ok("Successful Response",
                                        json_content(j(R"({"type": "object", "additionalProperties": )"
                                                       R"({"type": "string"}})")))}}));
    }

    /// Discovery: the well-known URL and the directory.
    void discovery_routes() {
        const Value td_content =
            j(R"({"application/td+json": {"schema": {"$ref": "#/components/schemas/ThingDescription"}}})");
        const auto problems = [](std::string_view description) {
            Value out = j(
                R"({"content": {"application/problem+json": {"schema": {"$ref": "#/components/schemas/Problem"}}}})");
            out["description"] = description;
            return out;
        };
        operation(
            "/.well-known/wot", "GET", "well_known_wot",
            obj({{"summary", "Thing Description Directory"},
                 {"description", "The TD of this server's Thing Description Directory (W3C WoT Discovery)."},
                 {"responses", ok("Successful Response", td_content)}}));
        const std::string things = prefix_ + "/directory/things";
        Value responses = ok("Successful Response", j(R"({"application/ld+json": {"schema": {"oneOf": [
            {"type": "array", "items": {"$ref": "#/components/schemas/ThingDescription"}},
            {"type": "object", "description": "A ThingCollection"}
        ]}}})"));
        responses["400"] = problems("Invalid query arguments");
        operation(things, "GET", "directory_things",
                  obj({{"summary", "Thing Descriptions"},
                       {"description",
                        "Every Thing's TD, sorted by `id`. With `limit`, a page, with `next` and `canonical` "
                        "links."},
                       {"parameters", j(R"json([
                     {"name": "offset", "in": "query", "required": false, "description": "How many TDs to skip", "schema": {"type": "integer", "minimum": 0}},
                     {"name": "limit", "in": "query", "required": false, "description": "How many TDs in a page (at least 1)", "schema": {"type": "integer", "minimum": 0}},
                     {"name": "format", "in": "query", "required": false, "schema": {"type": "string", "enum": ["array", "collection"], "default": "array"}}
                 ])json")},
                       {"responses", std::move(responses)}}));
        responses = ok("Successful Response", td_content);
        responses["404"] = problems("No TD has this ID");
        operation(things + "/{id}", "GET", "directory_thing",
                  obj({{"summary", "Thing Description"},
                       {"description", "One TD, by its `id`."},
                       {"parameters", j(R"([{"name": "id", "in": "path", "required": true, "schema": )"
                                        R"({"type": "string", "format": "iri-reference"}}])")},
                       {"responses", std::move(responses)}}));
    }

    /// A Thing's routes: its affordances, streams and endpoints by name, its
    /// top-level resources, then its TD.
    void thing(const ThingHandle& thing) {
        const std::string& name = thing.name();
        const std::string base = prefix_ + "/" + name + "/";
        struct Item {
            std::string name;
            const PropertyEntry* property = nullptr;
            const ActionEntry* action = nullptr;
            const EventEntry* event = nullptr;
            bool stream = false;
            const EndpointEntry* endpoint = nullptr;
        };
        std::vector<Item> items;
        for (const auto& property : thing.properties()) {
            items.push_back({property.name(), &property});
        }
        for (const auto& action : thing.actions()) {
            items.push_back({action.name(), nullptr, &action});
        }
        for (const auto& event : thing.events()) {
            items.push_back({event.name, nullptr, nullptr, &event});
        }
        for (const auto& [stream, _] : thing.streams()) {
            items.push_back({stream, nullptr, nullptr, nullptr, true});
        }
        for (const auto& endpoint : thing.endpoints()) {
            items.push_back({endpoint.operation_name(), nullptr, nullptr, nullptr, false, &endpoint});
        }
        std::ranges::stable_sort(items, {}, &Item::name);
        for (const auto& item : items) {
            if (item.property) {
                property(name, base, *item.property);
            } else if (item.action) {
                action(name, base, *item.action);
            } else if (item.event) {
                event(name, base, *item.event);
            } else if (item.stream) {
                stream(base, item.name);
            } else {
                endpoint(base, *item.endpoint);
            }
        }
        top_level(thing, base);
        operation(
            base, "GET", "things." + name,
            obj({{"summary", "Thing Description"},
                 {"description", "The W3C Thing Description of `" + name +
                                     "`: its properties, actions and events, and how to use them."},
                 {"responses", ok("Successful Response", json_content(reference("ThingDescription")))}}));
    }

  private:
    /// How server-sent events are named, in the profile.
    std::string named_events() const {
        return wot() ? ", in an event named after the affordance, with its time as its `id`" : "";
    }

    /// The answer to invalid input: FastAPI's 422, or the `wot` profile's
    /// 400.
    void invalid(Value& responses) const {
        if (wot()) {
            responses["400"] = problem("Validation Error");
        } else {
            responses["422"] = validation_error();
        }
    }

    /// The answer to an unknown or malformed invocation ID.
    void unknown_id(Value& responses, std::string_view description) const {
        if (wot()) {
            responses["404"] = problem(description);
        } else {
            responses["404"] = obj({{"description", description}});
            responses["422"] = validation_error();
        }
    }

    /// Adds an operation, named as FastAPI names it. A duplicate
    /// `operationId` (possible with FastAPI's rule) gets `_2`, `_3`, … so
    /// that the document stays valid.
    void operation(const std::string& path, std::string_view method, const std::string& name,
                   Value operation) {
        const std::string base = operation_id(name, path, method);
        std::string id = base;
        for (int n = 2; !ids_.insert(id).second; ++n) {
            id = base + "_" + std::to_string(n);
        }
        operation["operationId"] = id;
        Value ordered = Value::object();
        for (const char* key :
             {"summary", "description", "operationId", "parameters", "requestBody", "responses"}) {
            if (const auto found = operation.find(key); found != operation.end()) {
                ordered[key] = std::move(*found);
                operation.erase(key);
            }
        }
        for (auto& [key, value] : operation.items()) {
            ordered[key] = std::move(value);
        }
        std::string lower(method);
        std::ranges::transform(lower, lower.begin(), [](char c) {
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        });
        if (!paths.contains(path)) {
            paths[path] = Value::object();
        }
        paths[path][lower] = std::move(ordered);
    }

    Value schema(const std::string& name, Value schema) {
        schemas[name] = std::move(schema);
        return reference(name);
    }

    /// A Thing's top-level resources, where its TD has their forms.
    void top_level(const ThingHandle& thing, const std::string& base) {
        const std::string& name = thing.name();
        const std::string path = base + "properties";
        if (!thing.properties().empty()) {
            Value content =
                json_content(j(R"({"type": "object", "description": "Each property's value, by name."})"));
            content["text/event-stream"] = j(R"({"schema": {
                "type": "string",
                "description": "Server-sent events: each observable property's new values, as events named after it. Ask for them with `Accept: text/event-stream`."
            }})");
            operation(path, "GET", "read_all_properties",
                      obj({{"summary", "Read All Properties"},
                           {"description",
                            "Every property of `" + name +
                                "` (`readallproperties`), or their changes (`observeallproperties`)."},
                           {"responses", ok("Successful Response", std::move(content))}}));
        }
        if (std::ranges::any_of(thing.properties(), [](const auto& p) { return !p.is_read_only(); })) {
            Value responses = obj({{"204", no_content("Properties written")}});
            invalid(responses);
            operation(
                path, "PUT", "write_multiple_properties",
                obj({{"summary", "Write Multiple Properties"},
                     {"description", "Writes several properties of `" + name +
                                         "` (`writemultipleproperties`): nothing is written unless every "
                                         "value is valid."},
                     {"requestBody",
                      obj({{"content", json_content(j(R"({"type": "object"})"))}, {"required", true}})},
                     {"responses", std::move(responses)}}));
        }
        if (!thing.actions().empty()) {
            const char* item = wot() ? "ActionStatus" : "InvocationSummary";
            operation(
                base + "actions", "GET", "query_all_actions",
                obj({{"summary", "Query All Actions"},
                     {"description",
                      "The invocations of each action of `" + name + "`, newest first (`queryallactions`)."},
                     {"responses",
                      ok("Successful Response",
                         json_content(obj({{"type", "object"},
                                           {"additionalProperties",
                                            obj({{"type", "array"}, {"items", reference(item)}})}})))}}));
        }
        if (!thing.events().empty()) {
            operation(base + "events", "GET", "subscribe_all_events",
                      obj({{"summary", "Subscribe All Events"},
                           {"description", "Server-sent events: every event of `" + name +
                                               "`, named after it (`subscribeallevents`)."},
                           {"responses", event_stream("Server-sent events")}}));
        }
    }

    void property(const std::string& thing, const std::string& base, const PropertyEntry& property) {
        const std::string& name = property.name();
        const std::string& title = property.title();
        const std::string path = base + name;
        const std::string description = "## " + title + "\n\n" + property.description().value_or("");
        const Value value = schema(thing + "_" + name + "_value", json_schema(property.data_schema()));
        Value content = json_content(value);
        if (property.is_observable()) {
            content["text/event-stream"] =
                obj({{"schema",
                      obj({{"type", "string"},
                           {"description", "Server-sent events: each new value as JSON in a `data:` line" +
                                               named_events() +
                                               ". Ask for them with `Accept: text/event-stream`."}})}});
        }
        operation(path, "GET", "get_property",
                  obj({{"summary", title},
                       {"description", description},
                       {"responses", ok("Value of " + name, std::move(content))}}));
        if (!property.is_read_only()) {
            Value responses =
                wot() ? obj({{"204", no_content("Property set successfully")}})
                      : obj({{"201", response("Property set successfully", json_content(Value::object()))}});
            invalid(responses);
            operation(path, "PUT", "set_property",
                      obj({{"summary", "Set " + title},
                           {"description", description},
                           {"requestBody", obj({{"content", json_content(value)}, {"required", true}})},
                           {"responses", std::move(responses)}}));
        }
        if (property.is_resettable()) {
            Value responses = wot() ? obj({{"204", no_content("Successful Response")}})
                                    : ok("Successful Response", json_content(Value::object()));
            operation(
                path + "/reset", "POST", "reset",
                obj({{"summary", "Reset " + title + "."},
                     {"description", "## Reset " + title +
                                         "\n\nResets the property to its default value, which the Thing "
                                         "Description gives."},
                     {"responses", std::move(responses)}}));
        }
    }

    void action(const std::string& thing, const std::string& base, const ActionEntry& action) {
        const std::string& name = action.name();
        const std::string& title = action.title();
        const std::string path = base + name;
        Value input_schema = json_schema(action.input_schema());
        const bool takes_nothing = accepts_null(input_schema);
        const Value input = schema(thing + "_" + name + "_input", std::move(input_schema));
        const Value output = schema(thing + "_" + name + "_output", json_schema(action.output_schema()));
        const Value invocation =
            wot() ? schema(thing + "_" + name + "_status", action_status_schema(output))
                  : schema(thing + "_" + name + "_invocation", invocation_schema(input, output));
        const char* item = wot() ? "ActionStatus" : "InvocationSummary";
        operation(
            path, "GET", "list_invocations",
            obj({{"summary", "All invocations of " + name + "."},
                 {"description", "List all the invocations of " + name +
                                     " that the server is keeping, with their times and links."},
                 {"responses", ok("A list of every invocation of " + name + ".",
                                  json_content(obj({{"type", "array"}, {"items", reference(item)}})))}}));
        Value body = obj({{"content", json_content(input)}});
        if (!takes_nothing) {
            body["required"] = true;
        }
        const Value started =
            response("Action has been invoked (and may still be running).", json_content(invocation));
        std::string_view notice;
        Value responses;
        if (!wot()) {
            notice = action_notice;
            responses = obj({{"201", started}, {"200", response("Action completed.", json_content(output))}});
        } else if (!action.is_synchronous()) {
            notice = asynchronous_notice;
            responses = obj({{"201", started}});
        } else {
            notice = synchronous_notice;
            responses = action.has_output() ? ok("Action completed", json_content(output))
                                            : obj({{"204", no_content("Action completed")}});
            responses["500"] = problem("Action failed");
            responses["503"] = problem("Action unavailable");
        }
        invalid(responses);
        operation(path, "POST", "start_action",
                  obj({{"summary", title},
                       {"description", "## " + title + "\n\n" + action.description().value_or("") + "\n\n" +
                                           std::string(notice)},
                       {"requestBody", std::move(body)},
                       {"responses", std::move(responses)}}));
    }

    void event(const std::string& thing, const std::string& base, const EventEntry& event) {
        const std::string& name = event.name;
        const std::string& title = event.title;
        const Value data = event.data ? schema(thing + "_" + name + "_data", json_schema(*event.data))
                                      : j(R"({"type": "null"})");
        operation(
            base + name, "GET", "subscribe_event",
            obj({{"summary", title},
                 {"description", "## " + title + "\n\n" + event.description.value_or("") +
                                     "\n\nServer-sent events: each event's data, as JSON in a `data:` line" +
                                     named_events() + ", following this schema:\n\n```json\n" +
                                     json::dump_pretty(data) + "\n```"},
                 {"responses", event_stream("Server-sent events")}}));
    }

    void stream(const std::string& base, const std::string& name) {
        const std::string path = base + name;
        operation(path, "GET", "mjpeg_stream_response",
                  obj({{"summary", "Mjpeg Stream Response"},
                       {"description",
                        "An MJPEG stream: each frame as a part of a `multipart/x-mixed-replace` "
                        "response, which an `<img>` plays."},
                       {"responses",
                        ok("Successful Response", obj({{mjpeg, j(R"({"schema": {"type": "string"}})")}}))}}));
        operation(path + "/viewer", "GET", "viewer_page",
                  obj({{"summary", "Viewer Page"},
                       {"description", "A page that shows the stream."},
                       {"responses",
                        ok("Successful Response", j(R"({"text/html": {"schema": {"type": "string"}}})"))}}));
    }

    /// A custom endpoint: only its description and success can be described.
    void endpoint(const std::string& base, const EndpointEntry& endpoint) {
        Value success = obj({{"description", "Successful Response"}});
        if (endpoint.meta.link && endpoint.meta.link->media_type) {
            success["content"] = obj({{*endpoint.meta.link->media_type, Value::object()}});
        }
        Value described = obj({{"summary", default_summary(endpoint.operation_name())},
                               {"responses", obj({{"200", success}})}});
        if (endpoint.meta.description) {
            described["description"] = *endpoint.meta.description;
        }
        operation(base + endpoint.path, endpoint.method, endpoint.operation_name(), std::move(described));
    }

    std::string prefix_;
    WireProfile profile_;
    std::set<std::string> ids_;
};

}  // namespace

std::string operation_id(std::string_view name, std::string_view path, std::string_view method) {
    // Rust's `char::is_alphanumeric` keeps letters and digits of any script;
    // here, every character beyond ASCII counts as one (as API prefixes do).
    std::string id;
    for (const std::string_view part : {name, path}) {
        for (const char c : part) {
            const auto b = static_cast<unsigned char>(c);
            const bool kept = b >= 0x80 || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                              (c >= 'A' && c <= 'Z') || c == '_';
            id += kept ? c : '_';
        }
    }
    id += '_';
    for (const char c : method) {
        id += c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    }
    return id;
}

json::Value openapi(const Runtime& runtime, const HttpOptions& options) {
    Document document(options.api_prefix, options.profile);
    document.framework_routes();
    document.discovery_routes();
    for (const auto& thing : runtime.things()) {
        if (std::ranges::find(reserved_thing_names, thing->name()) == reserved_thing_names.end()) {
            document.thing(*thing);
        }
    }
    Value schemas = shared_schemas();
    const Value wot = wot_schemas();
    // Discovery's errors are problems in both profiles.
    schemas["Problem"] = wot["Problem"];
    if (document.wot()) {
        for (const auto& [name, schema] : wot.items()) {
            schemas[name] = schema;
        }
    }
    for (auto& [name, schema] : document.schemas.items()) {
        schemas[name] = std::move(schema);
    }
    return obj({{"openapi", "3.1.0"},
                {"info", obj({{"title", options.api_title}, {"version", options.api_version}})},
                {"paths", std::move(document.paths)},
                {"components", obj({{"schemas", std::move(schemas)}})}});
}

}  // namespace teta::wot::http
