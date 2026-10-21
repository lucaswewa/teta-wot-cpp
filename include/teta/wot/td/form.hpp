// Forms (how to perform an operation on an affordance), links, security
// schemes and the `@context`: the smaller parts of a Thing Description.

#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "teta/wot/json.hpp"
#include "teta/wot/td/data_schema.hpp"
#include "teta/wot/td/one_or_many.hpp"

namespace teta::wot::td {

// ---- Forms ------------------------------------------------------------------

/// Where an operation may be used.
enum class OperationScope { property, action, event, thing };

const char* to_string(OperationScope scope);

/// A TD operation type (`op`), TD 1.1 §5.3.4.2.
enum class Operation {
    readproperty,
    writeproperty,
    observeproperty,
    unobserveproperty,
    invokeaction,
    queryaction,
    cancelaction,
    subscribeevent,
    unsubscribeevent,
    readallproperties,
    writeallproperties,
    readmultipleproperties,
    writemultipleproperties,
    observeallproperties,
    unobserveallproperties,
    queryallactions,
    subscribeallevents,
    unsubscribeallevents,
};

/// The operation's name in JSON, such as `"readproperty"`.
const char* to_string(Operation op);

/// Which kind of form may use this operation.
OperationScope scope(Operation op);

/// A TD `ExpectedResponse`.
struct ExpectedResponse {
    std::string content_type;
    json::Value extra = json::Value::object();
    bool operator==(const ExpectedResponse&) const = default;
};

/// A TD `AdditionalExpectedResponse`.
struct AdditionalExpectedResponse {
    std::optional<bool> success;
    std::optional<std::string> content_type;
    std::optional<std::string> schema;
    json::Value extra = json::Value::object();
    bool operator==(const AdditionalExpectedResponse&) const = default;
};

/// A TD `Form` (TD 1.1 §5.3.4.2).
struct Form {
    std::string href;
    std::optional<OneOrMany<Operation>> op;
    std::optional<std::string> content_type;
    std::optional<std::string> content_coding;
    std::optional<std::string> subprotocol;
    std::optional<OneOrMany<std::string>> security;
    std::optional<OneOrMany<std::string>> scopes;
    std::optional<ExpectedResponse> response;
    std::optional<std::vector<AdditionalExpectedResponse>> additional_responses;
    /// Other terms, such as `htv:methodName`.
    json::Value extra = json::Value::object();

    Form() = default;
    explicit Form(std::string target) : href(std::move(target)) {}

    Form& with_op(OneOrMany<Operation> operations) {
        op = std::move(operations);
        return *this;
    }
    Form& with_op(std::initializer_list<Operation> operations) {
        op = OneOrMany<Operation>(std::vector<Operation>(operations));
        return *this;
    }
    Form& with_content_type(std::string type) {
        content_type = std::move(type);
        return *this;
    }
    Form& with_subprotocol(std::string protocol) {
        subprotocol = std::move(protocol);
        return *this;
    }
    Form& with_term(const std::string& name, json::Value value) {
        extra[name] = std::move(value);
        return *this;
    }

    /// The operations of this form, if any are given.
    std::vector<Operation> operations() const { return op ? op->values() : std::vector<Operation>{}; }

    bool operator==(const Form&) const = default;
};

json::Value to_json(const Form& form);

// ---- Links ------------------------------------------------------------------

/// A TD `Link` (TD 1.1 §5.3.4.1).
struct Link {
    std::string href;
    std::optional<std::string> media_type;  // `type`
    std::optional<std::string> rel;
    std::optional<std::string> anchor;
    std::optional<std::string> sizes;
    std::optional<OneOrMany<std::string>> hreflang;
    json::Value extra = json::Value::object();

    Link() = default;
    explicit Link(std::string target) : href(std::move(target)) {}

    Link& with_rel(std::string relation) {
        rel = std::move(relation);
        return *this;
    }
    Link& with_media_type(std::string type) {
        media_type = std::move(type);
        return *this;
    }

    bool operator==(const Link&) const = default;
};

json::Value to_json(const Link& link);

// ---- Security ---------------------------------------------------------------

/// The `scheme` of a security scheme (TD 1.1 §5.3.3).
enum class SecuritySchemeType { nosec, auto_, combo, basic, digest, apikey, bearer, psk, oauth2 };

const char* to_string(SecuritySchemeType type);

/// Where a credential is sent (`in`).
enum class CredentialLocation { header, query, body, cookie, uri, auto_ };

const char* to_string(CredentialLocation location);

/// A TD security scheme: one flat struct for every scheme, with the
/// scheme-specific terms optional. Use the factories to build a well-formed
/// one.
struct SecurityScheme {
    std::optional<OneOrMany<std::string>> semantic_type;
    std::optional<std::string> description;
    std::optional<MultiLanguage> descriptions;
    std::optional<std::string> proxy;
    SecuritySchemeType scheme = SecuritySchemeType::nosec;
    std::optional<CredentialLocation> location;  // `in`
    std::optional<std::string> name;
    std::optional<std::string> qop;
    std::optional<std::string> authorization;
    std::optional<std::string> alg;
    std::optional<std::string> format;
    std::optional<std::string> identity;
    std::optional<std::string> token;
    std::optional<std::string> refresh;
    std::optional<OneOrMany<std::string>> scopes;
    std::optional<std::string> flow;
    std::optional<std::vector<std::string>> one_of;
    std::optional<std::vector<std::string>> all_of;
    json::Value extra = json::Value::object();

    static SecurityScheme nosec() { return of(SecuritySchemeType::nosec); }
    static SecurityScheme auto_() { return of(SecuritySchemeType::auto_); }
    static SecurityScheme basic() { return of(SecuritySchemeType::basic); }
    static SecurityScheme digest() { return of(SecuritySchemeType::digest); }
    static SecurityScheme bearer() { return of(SecuritySchemeType::bearer); }
    static SecurityScheme psk() { return of(SecuritySchemeType::psk); }
    static SecurityScheme apikey(CredentialLocation in, std::string name);
    static SecurityScheme oauth2(std::string flow);
    static SecurityScheme combo_one_of(std::vector<std::string> names);
    static SecurityScheme combo_all_of(std::vector<std::string> names);

    SecurityScheme& with_description(std::string text) {
        description = std::move(text);
        return *this;
    }

    /// The names of other schemes this one refers to (combo schemes only).
    std::vector<std::string> referenced_schemes() const;

    bool operator==(const SecurityScheme&) const = default;

  private:
    static SecurityScheme of(SecuritySchemeType type) {
        SecurityScheme s;
        s.scheme = type;
        return s;
    }
};

json::Value to_json(const SecurityScheme& scheme);

// ---- @context ---------------------------------------------------------------

/// The context URI of TD 1.1. Every TD built here uses it.
inline constexpr const char* td_context_v1_1 = "https://www.w3.org/2022/wot/td/v1.1";
/// The context URI of TD 1.0, allowed only before the 1.1 URI.
inline constexpr const char* td_context_v1 = "https://www.w3.org/2019/wot/td/v1";

/// One entry of an `@context` array: a URI, or prefix definitions.
struct ContextEntry {
    std::optional<std::string> uri;
    std::vector<std::pair<std::string, std::string>> prefixes;

    static ContextEntry of_uri(std::string uri) { return {std::move(uri), {}}; }
    static ContextEntry of_prefixes(std::vector<std::pair<std::string, std::string>> map) {
        return {std::nullopt, std::move(map)};
    }
    bool operator==(const ContextEntry&) const = default;
};

/// The `@context`: the plain TD 1.1 URI, or an array starting with it once
/// entries (such as prefixes for semantic annotations) are added.
class Context {
  public:
    /// The plain TD 1.1 URI.
    Context() = default;
    /// A single URI.
    static Context of_uri(std::string uri);
    /// Several entries.
    static Context of_entries(std::vector<ContextEntry> entries);

    /// Appends an entry, turning a single URI into an array first.
    void push(ContextEntry entry);
    /// Adds a prefix definition; prefixes are collected in one map entry
    /// after the URIs.
    void add_prefix(const std::string& prefix, const std::string& uri);

    /// Checks the TD 1.1 `@context` rules; throws TdError.
    void validate() const;

    bool operator==(const Context&) const = default;

    friend json::Value to_json(const Context& context);

  private:
    std::optional<std::string> uri_ = td_context_v1_1;
    std::vector<ContextEntry> entries_;
};

json::Value to_json(const Context& context);

}  // namespace teta::wot::td
