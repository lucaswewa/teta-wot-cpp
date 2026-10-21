#include "teta/wot/td/describe.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

namespace teta::wot::td {

namespace {

// ---- SHA-1, for UUIDv5 (RFC 3174) -------------------------------------------

std::array<unsigned char, 20> sha1(std::string_view data) {
    std::uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    std::string message(data);
    const std::uint64_t bit_length = static_cast<std::uint64_t>(data.size()) * 8;
    message += static_cast<char>(0x80);
    while (message.size() % 64 != 56) {
        message += '\0';
    }
    for (int i = 7; i >= 0; --i) {
        message += static_cast<char>((bit_length >> (i * 8)) & 0xFF);
    }
    auto rotl = [](std::uint32_t x, int n) { return (x << n) | (x >> (32 - n)); };
    for (std::size_t chunk = 0; chunk < message.size(); chunk += 64) {
        std::uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            const auto* p = reinterpret_cast<const unsigned char*>(message.data() + chunk +
                                                                   static_cast<std::size_t>(i) * 4);
            w[i] =
                (std::uint32_t{p[0]} << 24) | (std::uint32_t{p[1]} << 16) | (std::uint32_t{p[2]} << 8) | p[3];
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            std::uint32_t f, k;
            if (i < 20) {
                f = (b & c) | (~b & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            const std::uint32_t temp = rotl(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rotl(b, 30);
            b = a;
            a = temp;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }
    std::array<unsigned char, 20> digest{};
    for (int i = 0; i < 5; ++i) {
        for (int j = 0; j < 4; ++j) {
            digest[static_cast<std::size_t>(i * 4 + j)] =
                static_cast<unsigned char>((h[i] >> (24 - j * 8)) & 0xFF);
        }
    }
    return digest;
}

std::array<unsigned char, 16> uuid_v5_bytes(const std::array<unsigned char, 16>& ns, std::string_view name) {
    std::string data(reinterpret_cast<const char*>(ns.data()), ns.size());
    data += name;
    const auto digest = sha1(data);
    std::array<unsigned char, 16> uuid{};
    std::memcpy(uuid.data(), digest.data(), 16);
    uuid[6] = static_cast<unsigned char>((uuid[6] & 0x0F) | 0x50);
    uuid[8] = static_cast<unsigned char>((uuid[8] & 0x3F) | 0x80);
    return uuid;
}

std::string uuid_text(const std::array<unsigned char, 16>& uuid) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string out;
    for (std::size_t i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            out += '-';
        }
        out += hex[uuid[i] >> 4];
        out += hex[uuid[i] & 0xF];
    }
    return out;
}

// The URL namespace of RFC 4122: 6ba7b811-9dad-11d1-80b4-00c04fd430c8.
constexpr std::array<unsigned char, 16> kUrlNamespace = {0x6b, 0xa7, 0xb8, 0x11, 0x9d, 0xad, 0x11, 0xd1,
                                                         0x80, 0xb4, 0x00, 0xc0, 0x4f, 0xd4, 0x30, 0xc8};

std::string trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n\f\v");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n\f\v");
    return std::string(text.substr(first, last - first + 1));
}

template <typename T>
std::vector<const T*> sorted_by_name(const std::vector<T>& items) {
    std::vector<const T*> out;
    for (const auto& item : items) {
        out.push_back(&item);
    }
    std::ranges::stable_sort(out, [](const T* a, const T* b) { return a->name < b->name; });
    return out;
}

void top_level_forms(ThingBuilder& td, const ThingInfo& thing, const TdOptions& options) {
    const std::string properties = options.path + "properties";
    std::vector<Operation> ops;
    if (!thing.properties.empty()) {
        ops.push_back(Operation::readallproperties);
    }
    if (std::ranges::any_of(thing.properties, [](const auto& p) { return !p.read_only; })) {
        ops.push_back(Operation::writemultipleproperties);
    }
    if (!ops.empty()) {
        td.form(Form(properties).with_op(OneOrMany<Operation>(ops)));
    }
    if (options.observation &&
        std::ranges::any_of(thing.properties, [](const auto& p) { return p.observable; })) {
        td.form(Form(properties)
                    .with_op({Operation::observeallproperties, Operation::unobserveallproperties})
                    .with_subprotocol("sse"));
    }
    if (!thing.actions.empty()) {
        td.form(Form(options.path + "actions").with_op({Operation::queryallactions}));
    }
    if (options.observation && !thing.events.empty()) {
        td.form(Form(options.path + "events")
                    .with_op({Operation::subscribeallevents, Operation::unsubscribeallevents})
                    .with_subprotocol("sse"));
    }
}

}  // namespace

std::pair<std::string, std::string> split_docstring(std::string_view doc) {
    const auto first_line_end = doc.find('\n');
    std::string title = trim(doc.substr(0, first_line_end));
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    while (true) {
        const auto end = doc.find('\n', start);
        lines.push_back(
            doc.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    if (lines.size() > 2 && trim(lines[1]).empty()) {
        std::string description;
        for (std::size_t i = 2; i < lines.size(); ++i) {
            if (i > 2) {
                description += '\n';
            }
            description += lines[i];
        }
        return {std::move(title), std::move(description)};
    }
    return {std::move(title), std::string(doc)};
}

std::string uuid_v5(const std::array<unsigned char, 16>& ns, std::string_view name) {
    return uuid_text(uuid_v5_bytes(ns, name));
}

std::string thing_id(std::string_view server_id, std::string_view thing) {
    const auto ns = uuid_v5_bytes(kUrlNamespace, "https://github.com/lucaswewa/wot#td-id");
    std::string name(server_id);
    name += '/';
    name += thing;
    return "urn:uuid:" + uuid_v5(ns, name);
}

std::optional<std::string> blob_media_type(const DataSchema& schema) {
    if (!schema.properties) {
        return std::nullopt;
    }
    const DataSchema* href = nullptr;
    const DataSchema* rel = nullptr;
    const DataSchema* media_type = nullptr;
    for (const auto& [name, member] : *schema.properties) {
        if (name == "href") {
            href = &member;
        } else if (name == "rel") {
            rel = &member;
        } else if (name == "media_type") {
            media_type = &member;
        }
    }
    if (!href || !rel || !rel->constant || *rel->constant != "output" || !media_type ||
        !media_type->default_value || !media_type->default_value->is_string()) {
        return std::nullopt;
    }
    return media_type->default_value->get<std::string>();
}

ThingDescription describe(const ThingInfo& thing, const TdOptions& options) {
    ThingBuilder td(thing.title);
    if (options.id) {
        td.id(*options.id);
    }
    if (thing.description) {
        td.description(*thing.description);
    }
    if (options.base) {
        td.base(*options.base);
    }
    if (!options.profiles.empty()) {
        td.profile(OneOrMany<std::string>(options.profiles));
    }
    if (options.security) {
        td.security_definition(options.security->first, options.security->second)
            .security(OneOrMany<std::string>(options.security->first));
    }
    for (const auto& [prefix, iri] : thing.context_prefixes) {
        td.context_prefix(prefix, iri);
    }
    for (const auto& type : thing.semantic_types) {
        td.semantic_type(type);
    }

    for (const PropertyInfo* p : sorted_by_name(thing.properties)) {
        const std::string href = options.path + p->name;
        auto ops = p->read_only ? std::vector<Operation>{Operation::readproperty}
                                : std::vector<Operation>{Operation::readproperty, Operation::writeproperty};
        PropertyBuilder affordance = PropertyBuilder(p->schema)
                                         .title(p->title)
                                         .read_only(p->read_only)
                                         .form(Form(href).with_op(OneOrMany<Operation>(ops)));
        if (options.observation && p->observable) {
            affordance.observable(true).form(
                Form(href)
                    .with_op({Operation::observeproperty, Operation::unobserveproperty})
                    .with_subprotocol("sse"));
            if (options.websocket) {
                affordance.form(Form(*options.websocket).with_op({Operation::observeproperty}));
            }
        }
        if (p->description) {
            affordance.description(*p->description);
        }
        if (p->default_value) {
            affordance.default_value(*p->default_value);
        }
        if (p->unit) {
            affordance.unit(*p->unit);
        }
        for (const auto& type : p->semantic_types) {
            affordance.semantic_type(type);
        }
        td.property(p->name, affordance.build());
    }

    for (const ActionInfo* a : sorted_by_name(thing.actions)) {
        ActionBuilder affordance = ActionBuilder()
                                       .title(a->title)
                                       .input(DataSchema(a->input).with_title(a->name + "_input"))
                                       .output(DataSchema(a->output).with_title(a->name + "_output"))
                                       .form(Form(options.path + a->name).with_op({Operation::invokeaction}));
        if (options.invocations) {
            const bool synchronous = options.synchronous_actions && a->synchronous;
            affordance.synchronous(synchronous);
            if (!synchronous) {
                affordance
                    .uri_variable(
                        "id",
                        DataSchema::of(DataType::string).with_title("Invocation ID").with_format("uuid"))
                    .form(Form(*options.invocations + "{id}")
                              .with_op({Operation::queryaction, Operation::cancelaction}));
                if (auto media_type = blob_media_type(a->output)) {
                    Form form = Form(*options.invocations + "{id}/output").with_op({Operation::queryaction});
                    form.response = ExpectedResponse{*media_type, json::Value::object()};
                    affordance.form(std::move(form));
                }
            }
        }
        if (a->description) {
            affordance.description(*a->description);
        }
        for (const auto& type : a->semantic_types) {
            affordance.semantic_type(type);
        }
        td.action(a->name, affordance.build());
    }

    if (options.observation) {
        for (const EventInfo* e : sorted_by_name(thing.events)) {
            EventBuilder affordance = EventBuilder().title(e->title).form(
                Form(options.path + e->name)
                    .with_op({Operation::subscribeevent, Operation::unsubscribeevent})
                    .with_subprotocol("sse"));
            if (options.websocket) {
                affordance.form(Form(*options.websocket).with_op({Operation::subscribeevent}));
            }
            if (e->description) {
                affordance.description(*e->description);
            }
            if (e->data) {
                affordance.data(*e->data);
            }
            for (const auto& type : e->semantic_types) {
                affordance.semantic_type(type);
            }
            td.event(e->name, affordance.build());
        }
    }

    if (options.links) {
        for (const auto& name : thing.streams) {
            const std::string href = options.path + name;
            td.link(Link(href).with_rel("alternate").with_media_type(mjpeg_media_type))
                .link(Link(href + "/viewer").with_rel("alternate").with_media_type("text/html"));
        }
        for (const auto& endpoint : thing.endpoint_links) {
            Link link = Link(options.path + endpoint.path).with_rel(endpoint.rel);
            if (endpoint.media_type) {
                link.with_media_type(*endpoint.media_type);
            }
            td.link(std::move(link));
        }
    }
    if (options.top_level) {
        top_level_forms(td, thing, options);
    }
    return td.build();
}

}  // namespace teta::wot::td
