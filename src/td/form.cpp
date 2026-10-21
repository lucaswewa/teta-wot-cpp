#include "teta/wot/td/form.hpp"

#include "json_io.hpp"
#include "teta/wot/td/thing.hpp"

namespace teta::wot::td {

namespace {

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

}  // namespace

const char* to_string(OperationScope scope) {
    switch (scope) {
        case OperationScope::property:
            return "Property";
        case OperationScope::action:
            return "Action";
        case OperationScope::event:
            return "Event";
        case OperationScope::thing:
            return "Thing";
    }
    return "?";
}

const char* to_string(Operation op) {
    for (const auto& [o, name] : kOperations) {
        if (o == op) {
            return name;
        }
    }
    return "?";
}

OperationScope scope(Operation op) {
    switch (op) {
        case Operation::readproperty:
        case Operation::writeproperty:
        case Operation::observeproperty:
        case Operation::unobserveproperty:
            return OperationScope::property;
        case Operation::invokeaction:
        case Operation::queryaction:
        case Operation::cancelaction:
            return OperationScope::action;
        case Operation::subscribeevent:
        case Operation::unsubscribeevent:
            return OperationScope::event;
        default:
            return OperationScope::thing;
    }
}

json::Value to_json(const Form& form) {
    using detail::put;
    json::Value out = json::Value::object();
    out["href"] = form.href;
    if (form.op) {
        if (form.op->is_many()) {
            json::Value ops = json::Value::array();
            for (const auto op : form.op->many()) {
                ops.push_back(to_string(op));
            }
            out["op"] = std::move(ops);
        } else {
            out["op"] = to_string(form.op->one());
        }
    }
    put(out, "contentType", form.content_type);
    put(out, "contentCoding", form.content_coding);
    put(out, "subprotocol", form.subprotocol);
    put(out, "security", form.security);
    put(out, "scopes", form.scopes);
    if (form.response) {
        json::Value response = json::Value::object();
        response["contentType"] = form.response->content_type;
        detail::put_extra(response, form.response->extra);
        out["response"] = std::move(response);
    }
    if (form.additional_responses) {
        json::Value responses = json::Value::array();
        for (const auto& r : *form.additional_responses) {
            json::Value response = json::Value::object();
            put(response, "success", r.success);
            put(response, "contentType", r.content_type);
            put(response, "schema", r.schema);
            detail::put_extra(response, r.extra);
            responses.push_back(std::move(response));
        }
        out["additionalResponses"] = std::move(responses);
    }
    detail::put_extra(out, form.extra);
    return out;
}

json::Value to_json(const Link& link) {
    using detail::put;
    json::Value out = json::Value::object();
    out["href"] = link.href;
    put(out, "type", link.media_type);
    put(out, "rel", link.rel);
    put(out, "anchor", link.anchor);
    put(out, "sizes", link.sizes);
    put(out, "hreflang", link.hreflang);
    detail::put_extra(out, link.extra);
    return out;
}

const char* to_string(SecuritySchemeType type) {
    switch (type) {
        case SecuritySchemeType::nosec:
            return "nosec";
        case SecuritySchemeType::auto_:
            return "auto";
        case SecuritySchemeType::combo:
            return "combo";
        case SecuritySchemeType::basic:
            return "basic";
        case SecuritySchemeType::digest:
            return "digest";
        case SecuritySchemeType::apikey:
            return "apikey";
        case SecuritySchemeType::bearer:
            return "bearer";
        case SecuritySchemeType::psk:
            return "psk";
        case SecuritySchemeType::oauth2:
            return "oauth2";
    }
    return "?";
}

const char* to_string(CredentialLocation location) {
    switch (location) {
        case CredentialLocation::header:
            return "header";
        case CredentialLocation::query:
            return "query";
        case CredentialLocation::body:
            return "body";
        case CredentialLocation::cookie:
            return "cookie";
        case CredentialLocation::uri:
            return "uri";
        case CredentialLocation::auto_:
            return "auto";
    }
    return "?";
}

SecurityScheme SecurityScheme::apikey(CredentialLocation in, std::string key_name) {
    auto s = of(SecuritySchemeType::apikey);
    s.location = in;
    s.name = std::move(key_name);
    return s;
}

SecurityScheme SecurityScheme::oauth2(std::string oauth_flow) {
    auto s = of(SecuritySchemeType::oauth2);
    s.flow = std::move(oauth_flow);
    return s;
}

SecurityScheme SecurityScheme::combo_one_of(std::vector<std::string> names) {
    auto s = of(SecuritySchemeType::combo);
    s.one_of = std::move(names);
    return s;
}

SecurityScheme SecurityScheme::combo_all_of(std::vector<std::string> names) {
    auto s = of(SecuritySchemeType::combo);
    s.all_of = std::move(names);
    return s;
}

std::vector<std::string> SecurityScheme::referenced_schemes() const {
    std::vector<std::string> names;
    if (one_of) {
        names.insert(names.end(), one_of->begin(), one_of->end());
    }
    if (all_of) {
        names.insert(names.end(), all_of->begin(), all_of->end());
    }
    return names;
}

json::Value to_json(const SecurityScheme& s) {
    using detail::put;
    json::Value out = json::Value::object();
    put(out, "@type", s.semantic_type);
    put(out, "description", s.description);
    if (s.descriptions) {
        out["descriptions"] = detail::pairs_to_json(*s.descriptions);
    }
    put(out, "proxy", s.proxy);
    out["scheme"] = to_string(s.scheme);
    if (s.location) {
        out["in"] = to_string(*s.location);
    }
    put(out, "name", s.name);
    put(out, "qop", s.qop);
    put(out, "authorization", s.authorization);
    put(out, "alg", s.alg);
    put(out, "format", s.format);
    put(out, "identity", s.identity);
    put(out, "token", s.token);
    put(out, "refresh", s.refresh);
    put(out, "scopes", s.scopes);
    put(out, "flow", s.flow);
    if (s.one_of) {
        out["oneOf"] = json::Value(*s.one_of);
    }
    if (s.all_of) {
        out["allOf"] = json::Value(*s.all_of);
    }
    detail::put_extra(out, s.extra);
    return out;
}

Context Context::of_uri(std::string uri) {
    Context c;
    c.uri_ = std::move(uri);
    return c;
}

Context Context::of_entries(std::vector<ContextEntry> entries) {
    Context c;
    c.uri_.reset();
    c.entries_ = std::move(entries);
    return c;
}

void Context::push(ContextEntry entry) {
    if (uri_) {
        entries_.push_back(ContextEntry::of_uri(std::move(*uri_)));
        uri_.reset();
    }
    entries_.push_back(std::move(entry));
}

void Context::add_prefix(const std::string& prefix, const std::string& uri) {
    if (!uri_ && !entries_.empty() && !entries_.back().uri) {
        auto& map = entries_.back().prefixes;
        for (auto& [k, v] : map) {
            if (k == prefix) {
                v = uri;
                return;
            }
        }
        map.emplace_back(prefix, uri);
        return;
    }
    push(ContextEntry::of_prefixes({{prefix, uri}}));
}

void Context::validate() const {
    auto invalid = [](const std::string& reason) {
        return TdError(TdError::Kind::invalid_context, "invalid @context: " + reason);
    };
    if (uri_) {
        if (*uri_ == td_context_v1_1) {
            return;
        }
        throw invalid(*uri_ + " must be " + td_context_v1_1);
    }
    auto is = [&](std::size_t i, const char* uri) {
        return i < entries_.size() && entries_[i].uri && *entries_[i].uri == uri;
    };
    auto v1_after = [&](std::size_t from) {
        for (std::size_t i = from; i < entries_.size(); ++i) {
            if (is(i, td_context_v1)) {
                return true;
            }
        }
        return false;
    };
    if (entries_.empty()) {
        throw invalid("the context can't be an empty array");
    }
    if (is(0, td_context_v1_1)) {
        if (v1_after(1)) {
            throw invalid("the TD 1.0 context URI is given after the TD 1.1 one");
        }
        return;
    }
    if (is(0, td_context_v1)) {
        if (!is(1, td_context_v1_1)) {
            throw invalid("the TD 1.0 context URI must be followed by the TD 1.1 one");
        }
        if (v1_after(2)) {
            throw invalid("the TD 1.0 context URI is given after the TD 1.1 one");
        }
        return;
    }
    throw invalid(std::string("the context must start with ") + td_context_v1_1 + ", or with " +
                  td_context_v1 + " followed by it");
}

json::Value to_json(const Context& context) {
    if (context.uri_) {
        return *context.uri_;
    }
    json::Value out = json::Value::array();
    for (const auto& entry : context.entries_) {
        if (entry.uri) {
            out.push_back(*entry.uri);
        } else {
            out.push_back(detail::pairs_to_json(entry.prefixes));
        }
    }
    return out;
}

}  // namespace teta::wot::td
