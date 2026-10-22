#include "teta/wot/runtime.hpp"

#include <algorithm>
#include <set>

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

namespace detail {

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
    invocation->finish(InvocationStatus::completed, std::move(output), std::nullopt);
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
    auto prepared = handler_->prepare(input);

    auto invocation =
        std::make_shared<Invocation>(id, thing_, name_, std::move(prepared.echo), meta_.retention, cancel,
                                     logs, shared_->broker, shared_->invocations->clock());
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
    return info;
}

td::ThingDescription ThingHandle::thing_description(const td::TdOptions& options) const {
    return td::describe(info(), options);
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
        runtime.things_.push_back(factory.make(name, shared));
        if (settings_folder_) {
            const auto folder = *settings_folder_ / name;
            std::error_code error;
            std::filesystem::create_directories(folder, error);
            if (error) {
                throw BuildError(
                    BuildError::Kind::settings_folder, name, {},
                    "couldn't create the settings folder " + folder.string() + ": " + error.message());
            }
        }
        runtime.start_order_.push_back(name);
    }
    for (const auto& handle : runtime.things_) {
        server_shared->things.emplace_back(handle->name(), handle);
    }
    return runtime;
}

Runtime::~Runtime() = default;

const ThingHandle* Runtime::thing(std::string_view name) const {
    for (const auto& handle : things_) {
        if (handle->name() == name) {
            return handle.get();
        }
    }
    return nullptr;
}

void Runtime::start() {
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
