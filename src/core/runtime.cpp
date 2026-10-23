#include "teta/wot/runtime.hpp"

#include <algorithm>
#include <iterator>
#include <set>
#include <variant>

namespace teta::wot {

// ---- Definitions ---------------------------------------------------------------------

DefinitionError::DefinitionError(std::string thing, std::string affordance, std::string message)
    : thing_(std::move(thing)),
      affordance_(std::move(affordance)),
      message_(std::move(message)),
      text_("`" + thing_ + "." + affordance_ + "`: " + message_) {}

std::optional<std::string> affordance_name_problem(std::string_view name) {
    if (name.empty()) {
        return "an affordance name can't be empty";
    }
    // Letters and digits are Unicode's in teta-wot; any code point beyond
    // ASCII is accepted here.
    const bool usable = std::ranges::all_of(name, [](char c) {
        const auto u = static_cast<unsigned char>(c);
        return u >= 0x80 || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '_' || c == '-';
    });
    if (!usable) {
        return "`" + std::string(name) +
               "` can't be an affordance name: names may only contain letters, digits, `_` and `-`";
    }
    if (std::ranges::find(reserved_affordance_names, name) != reserved_affordance_names.end()) {
        return "`" + std::string(name) + "` is reserved: the server uses `/{thing}/" + std::string(name) +
               "` for something else";
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> ThingCtx::settings_folder() const {
    if (const auto& folder = server_.settings_folder()) {
        return *folder / name_;
    }
    return std::nullopt;
}

// ---- Slots ----------------------------------------------------------------------------

std::string SlotSelection::to_string() const {
    switch (kind_) {
        case Kind::by_type:
            return "...";
        case Kind::name:
            return names_.front();
        case Kind::names: {
            std::string out = "[";
            for (std::size_t i = 0; i < names_.size(); ++i) {
                out += (i == 0 ? "'" : ", '") + names_[i] + "'";
            }
            return out + "]";
        }
        case Kind::nothing:
            break;
    }
    return "None";
}

namespace detail {

void throw_unconnected(const std::string& message) {
    throw std::logic_error(message);
}

void check_no_config(const json::Value& value) {
    if (value.is_object() && !value.empty()) {
        throw std::invalid_argument("unknown field `" + value.begin().key() + "`, there are no fields");
    }
    if (value.is_array() && !value.empty()) {
        throw std::invalid_argument("invalid length " + std::to_string(value.size()) +
                                    ", expected fewer elements in array");
    }
}

json::Value args_as_object(const json::Value& args, const json::Value& schema) {
    std::vector<std::string> fields;
    if (const auto properties = schema.find("properties");
        properties != schema.end() && properties->is_object()) {
        for (const auto& [field, _] : properties->items()) {
            fields.push_back(field);
        }
    }
    if (args.size() > fields.size()) {
        throw std::invalid_argument("invalid length " + std::to_string(args.size()) +
                                    ", expected fewer elements in array");
    }
    json::Value object = json::Value::object();
    for (std::size_t i = 0; i < args.size(); ++i) {
        object[fields[i]] = args[i];
    }
    return object;
}

td::DataSchema parameters_schema(const std::vector<codec::Field>& fields) {
    return td::from_json_schema(codec::object_schema(fields, std::nullopt, true));
}

void check_affordance_names(const std::string& thing, const std::vector<std::string>& names) {
    std::set<std::string> seen;
    for (const auto& name : names) {
        if (const auto problem = affordance_name_problem(name)) {
            throw BuildError(DefinitionError(thing, name, *problem));
        }
        if (!seen.insert(name).second) {
            throw BuildError(BuildError::Kind::duplicate_affordance, thing, name,
                             "Thing `" + thing + "` has two affordances named `" + name + "`");
        }
    }
}

std::optional<std::string> endpoint_path_problem(const std::string& path) {
    if (path.empty() || path.front() == '/') {
        return "endpoint path `" + path + "` must be relative to the Thing, without a leading `/`";
    }
    for (std::size_t start = 0;;) {
        const auto end = path.find('/', start);
        const auto segment = std::string_view(path).substr(start, end - start);
        if (segment.empty() || segment == "." || segment == "..") {
            return "endpoint path `" + path + "` has an empty, `.` or `..` segment";
        }
        if (end == std::string::npos) {
            return std::nullopt;
        }
        start = end + 1;
    }
}

}  // namespace detail

BuildError::BuildError(Kind kind, std::string thing, std::string name, std::string message)
    : kind_(kind), thing_(std::move(thing)), name_(std::move(name)), message_(std::move(message)) {}

BuildError::BuildError(DefinitionError error)
    : kind_(Kind::definition),
      thing_(error.thing()),
      name_(error.affordance()),
      message_(error.what()),
      definition_(std::move(error)) {}

// ---- Server -------------------------------------------------------------------------

Server Server::detached() {
    return Server(std::make_shared<Shared>());
}

std::vector<std::pair<std::string, std::shared_ptr<ThingHandle>>> Server::handles() const {
    std::vector<std::pair<std::string, std::shared_ptr<ThingHandle>>> out;
    for (const auto& [name, weak] : shared_->things) {
        if (auto handle = weak.lock()) {
            out.emplace_back(name, std::move(handle));
        }
    }
    return out;
}

std::vector<std::string> Server::thing_names() const {
    std::vector<std::string> out;
    for (const auto& [name, handle] : handles()) {
        out.push_back(name);
    }
    return out;
}

OrderedMap<json::Value> Server::thing_states() const {
    OrderedMap<json::Value> out;
    for (const auto& [name, handle] : handles()) {
        out[name] = handle->thing_state();
    }
    return out;
}

// ---- Actions --------------------------------------------------------------------------

namespace {

/// Whether a schema allows `null`.
bool accepts_null(const td::DataSchema& schema) {
    if (schema.data_type) {
        return *schema.data_type == td::DataType::null;
    }
    if (schema.one_of) {
        return std::ranges::any_of(*schema.one_of, accepts_null);
    }
    return !schema.enumeration && !schema.constant;
}

/// Whether a schema admits only `null`.
bool is_null_schema(const td::DataSchema& schema) {
    return schema.data_type == td::DataType::null && !schema.one_of;
}

void log_at(log::Level level, std::string message) {
    log::emit({level, std::move(message), __FILE__, __LINE__, std::chrono::system_clock::now(), std::nullopt,
               std::nullopt});
}

/// Runs an invocation to the end, in its scope.
void run_invocation(const std::shared_ptr<Invocation>& invocation,
                    const std::function<json::Value(ActionCtx&)>& run, ActionCtx& ctx,
                    const std::shared_ptr<GlobalLock>& lock, const Uuid& owner, log::Level lock_log_level) {
    std::optional<GlobalLock::Guard> guard;
    if (lock) {
        try {
            guard.emplace(lock->acquire(owner));
        } catch (const GlobalLockBusy&) {
            log_at(lock_log_level, "Global lock was busy: didn't run " + invocation->action() + ".");
            const auto error = ActionError::from(std::current_exception());
            invocation->finish(InvocationStatus::error, std::nullopt, error.problem());
            return;
        }
    }
    invocation->set_running();
    json::Value output;
    // The output keeps the data of the Blobs in it.
    blob::Capture blobs;
    try {
        output = run(ctx);
    } catch (...) {
        guard.reset();
        const auto error = ActionError::from(std::current_exception());
        InvocationStatus status = InvocationStatus::error;
        if (error.is_cancelled()) {
            log_at(log::Level::info, "Invocation " + invocation->id().to_string() + " was cancelled.");
            status = InvocationStatus::cancelled;
        } else if (error.is_handled()) {
            log_at(log::Level::error, error.what());
        } else {
            log::emit({log::Level::error, error.what(), __FILE__, __LINE__, std::chrono::system_clock::now(),
                       error.title(), error.traceback()});
        }
        invocation->finish(status, std::nullopt, error.problem());
        return;
    }
    guard.reset();
    invocation->finish(InvocationStatus::completed, std::move(output), std::nullopt, blobs.take());
}

}  // namespace

ActionEntry::ActionEntry(std::string name, std::string thing, ActionMeta meta,
                         std::shared_ptr<ActionHandler> handler, std::shared_ptr<const RuntimeShared> shared)
    : name_(std::move(name)),
      thing_(std::move(thing)),
      title_(meta.title.value_or(name_)),
      meta_(std::move(meta)),
      handler_(std::move(handler)),
      shared_(std::move(shared)) {}

bool ActionEntry::has_output() const {
    return !is_null_schema(handler_->output_schema());
}

std::shared_ptr<Invocation> ActionEntry::invoke(const json::Value& input) const {
    shared_->invocations->expire();
    // A `null` body is a missing one, which only inputs that may be `null`
    // (such as NoInput) accept.
    if (input.is_null() && !accepts_null(handler_->input_schema())) {
        throw ValidationError::missing({"body"}, nullptr);
    }
    const Uuid id = Uuid::v4();
    // In-process calls from another invocation share its lock ownership.
    const auto current = InvocationScope::current();
    const Uuid owner = current ? current->lock_owner() : id;
    CancelToken cancel;
    auto logs = std::make_shared<LogBuffer>(id);
    InvocationScope scope(id, owner, cancel, logs);
    blob::Capture input_blobs;
    auto prepared = handler_->prepare(input);

    auto invocation =
        std::make_shared<Invocation>(id, thing_, name_, std::move(prepared.echo), meta_.retention, cancel,
                                     logs, shared_->broker, shared_->invocations->clock());
    invocation->hold_input_blobs(input_blobs.take());
    shared_->invocations->insert(invocation);
    invocation->announce();

    std::shared_ptr<GlobalLock> lock = meta_.global_lock ? shared_->lock : nullptr;
    ActionCtx ctx(scope, thing_, shared_->lock, shared_->server, shared_->pool);
    shared_->pool->submit([invocation, scope, ctx, run = std::move(prepared.run), lock, owner,
                           level = shared_->lock_log_level]() mutable {
        scope.run([&] { run_invocation(invocation, run, ctx, lock, owner, level); });
    });
    return invocation;
}

json::Value ActionEntry::call(const json::Value& input) const {
    if (input.is_null() && !accepts_null(handler_->input_schema())) {
        throw ValidationError::missing({"body"}, nullptr);
    }
    auto prepared = handler_->prepare(input);
    return run_in_process([&](ActionCtx& ctx) { return prepared.run(ctx); });
}

json::Value ActionEntry::validate_input(const json::Value& input) const {
    return handler_->prepare(input).echo;
}

// ---- Things ---------------------------------------------------------------------------

const PropertyEntry* ThingHandle::property(std::string_view name) const {
    for (const auto& p : properties_) {
        if (p.name() == name) {
            return &p;
        }
    }
    return nullptr;
}

const ActionEntry* ThingHandle::action(std::string_view name) const {
    for (const auto& a : actions_) {
        if (a.name() == name) {
            return &a;
        }
    }
    return nullptr;
}

const EventEntry* ThingHandle::event(std::string_view name) const {
    for (const auto& e : events_) {
        if (e.name == name) {
            return &e;
        }
    }
    return nullptr;
}

const MjpegStream* ThingHandle::stream(std::string_view name) const {
    for (const auto& [stream_name, stream] : streams_) {
        if (stream_name == name) {
            return &stream;
        }
    }
    return nullptr;
}

json::Value ThingHandle::thing_state() const {
    if (thing_state_) {
        return thing_state_();
    }
    return json::Value::object();
}

td::ThingInfo ThingHandle::info() const {
    td::ThingInfo info;
    info.title = title_;
    info.description = description_;
    info.semantic_types = semantic_types_;
    info.context_prefixes = context_prefixes_;
    for (const auto& p : properties_) {
        info.properties.push_back({p.name(), p.title(), p.description(), p.data_schema(), p.is_read_only(),
                                   p.is_observable(), p.default_value(), p.unit(), p.semantic_types()});
    }
    for (const auto& a : actions_) {
        info.actions.push_back({a.name(), a.title(), a.description(), a.input_schema(), a.output_schema(),
                                a.is_synchronous(), a.semantic_types()});
    }
    for (const auto& e : events_) {
        info.events.push_back({e.name, e.title, e.description, e.data, e.semantic_types});
    }
    for (const auto& [name, stream] : streams_) {
        info.streams.push_back(name);
    }
    for (const auto& e : endpoints_) {
        if (e.meta.link) {
            info.endpoint_links.push_back({e.path, e.meta.link->rel, e.meta.link->media_type});
        }
    }
    return info;
}

td::ThingDescription ThingHandle::thing_description(const td::TdOptions& options) const {
    return td::describe(info(), options);
}

std::optional<std::filesystem::path> ThingHandle::settings_file() const {
    if (settings_) {
        return settings_->path();
    }
    return std::nullopt;
}

std::vector<std::string> ThingHandle::slot_names() const {
    std::vector<std::string> out;
    for (const auto& [name, slot] : slots_) {
        out.push_back(name);
    }
    return out;
}

std::vector<std::pair<std::type_index, std::string>> ThingHandle::services() const {
    std::vector<std::pair<std::type_index, std::string>> out;
    for (const auto& action : actions_) {
        for (auto& service : action.handler().services()) {
            if (std::ranges::find(out, service.first, [](const auto& s) { return s.first; }) == out.end()) {
                out.push_back(std::move(service));
            }
        }
    }
    return out;
}

void ThingHandle::load_settings() {
    if (settings_) {
        settings_->load();
    }
}

void ThingHandle::close_devices(std::size_t count) {
    for (std::size_t i = count; i-- > 0;) {
        try {
            devices_[i].close();
        } catch (const std::exception& error) {
            log::warn("closing a device failed: {}", error.what());
        }
    }
}

void ThingHandle::start() {
    std::size_t opened = 0;
    try {
        for (const auto& device : devices_) {
            device.open(name_ + ":" + device.name);
            ++opened;
        }
        if (on_start_) {
            ThingCtx ctx(name_, server_);
            on_start_(ctx);
        }
    } catch (...) {
        close_devices(opened);
        throw;
    }
}

void ThingHandle::stop() {
    if (on_stop_) {
        try {
            ThingCtx ctx(name_, server_);
            on_stop_(ctx);
        } catch (const std::exception& error) {
            log::warn("stopping Thing `{}` failed: {}", name_, error.what());
        }
    }
    close_devices(devices_.size());
}

// ---- The runtime ----------------------------------------------------------------------

namespace {

using Things = std::vector<std::shared_ptr<ThingHandle>>;

const std::shared_ptr<ThingHandle>* find_thing(const Things& things, std::string_view name) {
    for (const auto& thing : things) {
        if (thing->name() == name) {
            return &thing;
        }
    }
    return nullptr;
}

// The Things `selection` picks for a slot, or why it can't.
std::variant<detail::PickedThings, std::string> pick(const detail::SlotControl& slot, const Things& things,
                                                     const SlotSelection& selection) {
    detail::PickedThings picked;
    auto named = [&](const std::string& name) -> std::optional<std::string> {
        const auto* thing = find_thing(things, name);
        if (!thing) {
            return name + " is not the name of a Thing";
        }
        if (!slot.accepts(*thing)) {
            return name + " is the wrong type";
        }
        picked.emplace_back(name, *thing);
        return std::nullopt;
    };
    switch (selection.kind()) {
        case SlotSelection::Kind::nothing:
            break;
        case SlotSelection::Kind::by_type:
            for (const auto& thing : things) {
                if (slot.accepts(thing)) {
                    picked.emplace_back(thing->name(), thing);
                }
            }
            break;
        case SlotSelection::Kind::name:
        case SlotSelection::Kind::names:
            for (const auto& name : selection.targets()) {
                if (auto problem = named(name)) {
                    return *problem;
                }
            }
            break;
    }
    return picked;
}

// Connects one slot; returns the names of the Things it connected to.
// Throws BuildError.
std::vector<std::string> connect(const std::string& host, const std::string& slot_name,
                                 const detail::SlotControl& slot, const Things& things,
                                 const SlotSelection* configured) {
    const SlotSelection& used = configured ? *configured : slot.default_selection;
    auto result = pick(slot, things, used);
    if (auto* picked = std::get_if<detail::PickedThings>(&result)) {
        if (used.kind() == SlotSelection::Kind::nothing && slot.kind == SlotKind::single) {
            result = std::string("it must be set in configuration");
        } else if (slot.kind == SlotKind::single && picked->empty()) {
            result = std::string("no matching Thing was found");
        } else if (slot.kind != SlotKind::map && picked->size() > 1) {
            result = std::string("it can't connect to multiple Things");
        }
    }
    if (auto* picked = std::get_if<detail::PickedThings>(&result)) {
        slot.fill(*picked);
        std::vector<std::string> names;
        for (const auto& [name, thing] : *picked) {
            names.push_back(name);
        }
        return names;
    }
    std::string message =
        "Can't connect '" + host + "." + slot_name + "' because " + std::get<std::string>(result) + ". ";
    if (configured) {
        message += "It was configured to connect to '" + configured->to_string() + "'. ";
    } else {
        message += "It was not configured, and used the default. ";
    }
    if (slot.default_selection.kind() == SlotSelection::Kind::by_type) {
        message += "The default searches for Things by type: '" + slot.target_name + "'.";
    } else {
        message += "The default is '" + slot.default_selection.to_string() + "'.";
    }
    throw BuildError(BuildError::Kind::slot, host, slot_name, message);
}

// The start order: dependencies (the Things a Thing's slots connect to)
// before the Things that use them, otherwise in configuration order. With a
// dependency cycle, the Things left in the cycle.
std::variant<std::vector<std::string>, std::vector<std::string>> start_order(
    const std::vector<std::string>& names,
    const std::vector<std::pair<std::string, std::vector<std::string>>>& dependencies) {
    std::vector<std::string> order;
    std::vector<std::string> remaining = names;
    auto depends_on = [&](const std::string& name) -> const std::vector<std::string>* {
        for (const auto& [thing, deps] : dependencies) {
            if (thing == name) {
                return &deps;
            }
        }
        return nullptr;
    };
    auto contains = [](const std::vector<std::string>& list, const std::string& name) {
        return std::ranges::find(list, name) != list.end();
    };
    while (!remaining.empty()) {
        const auto ready = std::ranges::find_if(remaining, [&](const std::string& name) {
            const auto* deps = depends_on(name);
            return !deps || std::ranges::all_of(*deps, [&](const std::string& dep) {
                return dep == name || contains(order, dep) || !contains(names, dep);
            });
        });
        if (ready == remaining.end()) {
            return std::variant<std::vector<std::string>, std::vector<std::string>>(std::in_place_index<1>,
                                                                                    remaining);
        }
        order.push_back(*ready);
        remaining.erase(ready);
    }
    return std::variant<std::vector<std::string>, std::vector<std::string>>(std::in_place_index<0>, order);
}

// A list of names as Rust's `{:?}` writes it: `["a", "b"]`.
std::string debug_list(const std::vector<std::string>& names) {
    std::string out = "[";
    for (std::size_t i = 0; i < names.size(); ++i) {
        out += (i == 0 ? "" : ", ") + json::dump(json::Value(names[i]));
    }
    return out + "]";
}

}  // namespace

BuildError RuntimeBuilder::invalid_config(const std::string& thing, const std::string& error) {
    return BuildError(BuildError::Kind::invalid_config, thing, {},
                      "the configuration of Thing `" + thing + "` is invalid: " + error);
}

RuntimeBuilder& RuntimeBuilder::failing(std::string name, std::string error) {
    things_.push_back(
        {std::move(name),
         [error = std::move(error)](const std::string& thing, std::shared_ptr<const RuntimeShared>)
             -> std::shared_ptr<ThingHandle> { throw invalid_config(thing, error); }});
    return *this;
}

RuntimeBuilder& RuntimeBuilder::thing_slots(const std::string& thing,
                                            std::vector<std::pair<std::string, SlotSelection>> slots) {
    auto it = std::ranges::find(slot_overrides_, thing, [](const auto& entry) { return entry.first; });
    if (it == slot_overrides_.end()) {
        slot_overrides_.emplace_back(thing, std::vector<std::pair<std::string, SlotSelection>>{});
        it = std::prev(slot_overrides_.end());
    }
    for (auto& [slot, selection] : slots) {
        auto existing = std::ranges::find(it->second, slot, [](const auto& entry) { return entry.first; });
        if (existing != it->second.end()) {
            existing->second = std::move(selection);
        } else {
            it->second.emplace_back(std::move(slot), std::move(selection));
        }
    }
    return *this;
}

Runtime RuntimeBuilder::build() {
    auto server_shared = std::make_shared<Server::Shared>();
    server_shared->services = services_;
    server_shared->application_config = application_config_;
    server_shared->settings_folder = settings_folder_;

    auto shared = std::make_shared<RuntimeShared>(
        RuntimeShared{global_lock_ ? GlobalLock::create(lock_timeout_) : nullptr, lock_log_level_,
                      std::make_shared<MessageBroker>(), std::make_shared<InvocationManager>(clock_),
                      Server(server_shared), std::make_shared<WorkerPool>(max_threads_)});

    Runtime runtime;
    runtime.shared_ = shared;
    for (const auto& factory : things_) {
        const auto& name = factory.name;
        const bool valid = !name.empty() && std::ranges::all_of(name, [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
                   c == '_';
        });
        if (!valid) {
            throw BuildError(
                BuildError::Kind::invalid_name, name, {},
                "invalid Thing name `" + name + "`: names may only contain letters, digits, `-` and `_`");
        }
        if (runtime.thing(name)) {
            throw BuildError(BuildError::Kind::duplicate_thing, name, {},
                             "two Things are named `" + name + "`");
        }
        auto handle = factory.make(name, shared);
        for (const auto& [service, type_name] : handle->services()) {
            if (!services_.contains(service)) {
                throw BuildError(BuildError::Kind::missing_service, name, type_name,
                                 "Thing `" + name + "` needs the service `" + type_name +
                                     "`, which isn't registered with the server");
            }
        }
        // A settings folder for every Thing.
        if (settings_folder_) {
            const auto folder = *settings_folder_ / name;
            std::error_code error;
            std::filesystem::create_directories(folder, error);
            if (error) {
                throw BuildError(BuildError::Kind::settings_folder, name, {},
                                 "couldn't create the settings folder " + detail::path_text(folder) + ": " +
                                     error.message() + " (os error " + std::to_string(error.value()) + ")");
            }
        }
        runtime.things_.push_back(std::move(handle));
    }

    std::vector<std::pair<std::string, std::vector<std::string>>> dependencies;
    for (const auto& thing : runtime.things_) {
        const auto& name = thing->name();
        const auto overrides =
            std::ranges::find(slot_overrides_, name, [](const auto& entry) { return entry.first; });
        const auto* configured_slots = overrides == slot_overrides_.end() ? nullptr : &overrides->second;
        const auto& slots = thing->slots();
        if (configured_slots) {
            for (const auto& [configured, selection] : *configured_slots) {
                if (std::ranges::find(slots, configured, [](const auto& slot) { return slot.first; }) ==
                    slots.end()) {
                    log::warn("`thing_slots` of `{}` configures `{}`, which isn't one of its slots", name,
                              configured);
                }
            }
        }
        std::vector<std::string> connected;
        for (const auto& [slot_name, slot] : slots) {
            const SlotSelection* configured = nullptr;
            if (configured_slots) {
                const auto it = std::ranges::find(*configured_slots, slot_name,
                                                  [](const auto& entry) { return entry.first; });
                if (it != configured_slots->end()) {
                    configured = &it->second;
                }
            }
            for (auto& target : connect(name, slot_name, *slot, runtime.things_, configured)) {
                connected.push_back(std::move(target));
            }
        }
        dependencies.emplace_back(name, std::move(connected));
    }
    std::vector<std::string> names;
    for (const auto& thing : runtime.things_) {
        names.push_back(thing->name());
    }
    auto order = start_order(names, dependencies);
    if (order.index() == 0) {
        runtime.start_order_ = std::move(std::get<0>(order));
    } else {
        log::warn(
            "the slots of {} depend on each other in a cycle: starting every Thing in configuration order",
            debug_list(std::get<1>(order)));
        runtime.start_order_ = names;
    }

    for (const auto& handle : runtime.things_) {
        server_shared->things.emplace_back(handle->name(), handle);
    }
    return runtime;
}

Runtime::~Runtime() {
    // Slots hold the Things they connect to: empty them, so that Things that
    // refer to each other don't keep each other alive.
    for (const auto& thing : things_) {
        for (const auto& [name, slot] : thing->slots()) {
            slot->clear();
        }
    }
}

const ThingHandle* Runtime::thing(std::string_view name) const {
    for (const auto& handle : things_) {
        if (handle->name() == name) {
            return handle.get();
        }
    }
    return nullptr;
}

void Runtime::start() {
    for (const auto& thing : things_) {
        thing->load_settings();
    }
    for (const auto& name : start_order_) {
        const auto it = std::ranges::find_if(things_, [&](const auto& h) { return h->name() == name; });
        const auto& handle = *it;
        try {
            handle->start();
        } catch (...) {
            std::string error;
            try {
                throw;
            } catch (const std::exception& e) {
                error = detail::message_chain(e);
            } catch (...) {
                error = "unknown error";
            }
            stop();
            throw StartupError(name, error);
        }
        std::lock_guard lock(*started_mutex_);
        started_.push_back(handle);
    }
}

void Runtime::stop() {
    std::vector<std::shared_ptr<ThingHandle>> started;
    {
        std::lock_guard lock(*started_mutex_);
        started.swap(started_);
    }
    for (auto it = started.rbegin(); it != started.rend(); ++it) {
        (*it)->stop();
    }
}

void Runtime::shutdown(std::chrono::nanoseconds grace) {
    shared_->broker->close();
    shared_->invocations->cancel_all();
    shared_->invocations->wait_idle(grace);
    stop();
}

}  // namespace teta::wot
