// The runtime: the registry of running Things, their invocations and
// lifecycle. A port of teta-wot-core's `runtime` and `inprocess`.

#pragma once

#include <any>
#include <chrono>
#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <typeindex>
#include <utility>
#include <vector>

#include "teta/wot/action.hpp"
#include "teta/wot/broker.hpp"
#include "teta/wot/context.hpp"
#include "teta/wot/event.hpp"
#include "teta/wot/invocation.hpp"
#include "teta/wot/lock.hpp"
#include "teta/wot/property.hpp"
#include "teta/wot/runtime_shared.hpp"
#include "teta/wot/server.hpp"
#include "teta/wot/td/describe.hpp"
#include "teta/wot/thing.hpp"

namespace teta::wot {

/// A runtime can't be built.
class BuildError : public std::exception {
  public:
    enum class Kind {
        /// A Thing's name has characters that are not allowed.
        invalid_name,
        /// Two Things have the same name.
        duplicate_thing,
        /// Two affordances of one Thing have the same name (they would share
        /// a URL).
        duplicate_affordance,
        /// An affordance is badly defined, or badly named.
        definition,
        /// A Thing's configuration (its `kwargs` or `args`) doesn't fit its
        /// `Config` type.
        invalid_config,
        /// A slot can't be connected (`ThingSlotError`).
        slot,
        /// A Thing needs a service (a `Dep<S>` parameter) that isn't
        /// registered.
        missing_service,
        /// A Thing's settings folder can't be created.
        settings_folder,
    };

    BuildError(Kind kind, std::string thing, std::string name, std::string message);
    explicit BuildError(DefinitionError error);

    Kind kind() const { return kind_; }
    /// The Thing (and, for affordances, the affordance or service) concerned.
    const std::string& thing() const { return thing_; }
    const std::string& name() const { return name_; }
    /// For `definition`.
    const std::optional<DefinitionError>& definition() const { return definition_; }
    const char* what() const noexcept override { return message_.c_str(); }

  private:
    Kind kind_;
    std::string thing_;
    std::string name_;
    std::string message_;
    std::optional<DefinitionError> definition_;
};

/// A Thing failed to start.
class StartupError : public std::exception {
  public:
    StartupError(std::string thing, std::string error)
        : thing_(std::move(thing)),
          error_(std::move(error)),
          message_("Thing `" + thing_ + "` failed to start: " + error_) {}
    const std::string& thing() const { return thing_; }
    /// Why, with its causes (`a: b`).
    const std::string& error() const { return error_; }
    const char* what() const noexcept override { return message_.c_str(); }

  private:
    std::string thing_;
    std::string error_;
    std::string message_;
};

/// One action of a running Thing: what clients (and the HTTP binding) use.
class ActionEntry {
  public:
    ActionEntry(std::string name, std::string thing, ActionMeta meta, std::shared_ptr<ActionHandler> handler,
                std::shared_ptr<const RuntimeShared> shared);

    const std::string& name() const { return name_; }
    const std::string& title() const { return title_; }
    const std::optional<std::string>& description() const { return meta_.description; }
    /// How long finished invocations are kept.
    std::chrono::nanoseconds retention() const { return meta_.retention; }
    const td::DataSchema& input_schema() const { return handler_->input_schema(); }
    const td::DataSchema& output_schema() const { return handler_->output_schema(); }
    /// Whether the output isn't always `null` (as an action returning
    /// nothing's is).
    bool has_output() const;
    bool is_synchronous() const { return meta_.synchronous; }
    const std::vector<std::string>& semantic_types() const { return meta_.semantic_types; }
    const ActionHandler& handler() const { return *handler_; }

    /// Starts an invocation with a client's input and returns it at once,
    /// `pending`. The input is validated first (errors under `body`); a
    /// missing body is `null`. Expired invocations are removed first.
    /// Throws ValidationError.
    std::shared_ptr<Invocation> invoke(const json::Value& input) const;

    /// Calls the action in-process, as another Thing or a test would, and
    /// waits for its output: the input is validated as a client's would be;
    /// the global lock is held, if the action uses it, on behalf of the
    /// calling invocation; the action runs in the caller's invocation
    /// context (its cancellation and log), or in a new stand-alone one; no
    /// invocation is recorded. Throws what the validation, the lock or the
    /// action throws.
    json::Value call(const json::Value& input) const;

    /// Validates an input as a client's would be, and returns it coerced (as
    /// an invocation would record it).
    json::Value validate_input(const json::Value& input) const;

    /// Runs `f(ActionCtx&)` as an in-process call of this action: with the
    /// global lock and the caller's invocation context.
    template <typename F>
    decltype(auto) run_in_process(F&& f) const {
        const auto current = InvocationScope::current();
        const InvocationScope scope = current.value_or(InvocationScope::fake());
        ActionCtx ctx(scope, thing_, shared_->lock, shared_->server, shared_->pool);
        std::optional<GlobalLock::Guard> guard;
        if (shared_->lock && meta_.global_lock) {
            guard.emplace(shared_->lock->acquire(scope.lock_owner()));
        }
        if (current) {
            return std::forward<F>(f)(ctx);
        }
        return scope.run([&]() -> decltype(auto) { return std::forward<F>(f)(ctx); });
    }

  private:
    std::string name_;
    std::string thing_;
    std::string title_;
    ActionMeta meta_;
    std::shared_ptr<ActionHandler> handler_;
    std::shared_ptr<const RuntimeShared> shared_;
};

/// A custom endpoint of a running Thing.
struct EndpointEntry {
    /// In upper case.
    std::string method;
    /// Relative to the Thing's path.
    std::string path;
    EndpointMeta meta;
    http::EndpointHandler handler;

    /// The name its OpenAPI `operationId` is made from: the one given, or the
    /// path.
    const std::string& operation_name() const { return meta.name ? *meta.name : path; }
};

/// One running Thing: its instance and the registry entries of its
/// affordances, which the HTTP binding serves.
class ThingHandle : public std::enable_shared_from_this<ThingHandle> {
  public:
    /// Builds the registry entries from `T::define`; throws BuildError.
    template <ThingType T>
    static std::shared_ptr<ThingHandle> instantiate(const std::string& name, std::shared_ptr<T> thing,
                                                    std::shared_ptr<const RuntimeShared> shared);

    const std::string& name() const { return name_; }
    const std::string& title() const { return title_; }
    const std::optional<std::string>& description() const { return description_; }
    const std::vector<std::string>& semantic_types() const { return semantic_types_; }
    /// The class name in the settings file's name.
    const std::string& class_name() const { return class_name_; }
    /// The Thing's settings file, if it has settings and the server has a
    /// settings folder.
    std::optional<std::filesystem::path> settings_file() const;
    /// The names of the Thing's slots, in definition order.
    std::vector<std::string> slot_names() const;
    /// The services its actions' `Dep<S>` parameters need, with their names.
    std::vector<std::pair<std::type_index, std::string>> services() const;

    /// The Thing as the interface `I`, if it declared that it provides it
    /// (`t.provides<I>()`).
    template <typename I>
    std::shared_ptr<I> provided() const {
        for (const auto& [type, build] : interfaces_) {
            if (type == std::type_index(typeid(I))) {
                return std::static_pointer_cast<I>(build(shared_from_this()));
            }
        }
        return nullptr;
    }

    /// A property by name.
    const PropertyEntry* property(std::string_view name) const;
    /// The properties, in definition order.
    const std::vector<PropertyEntry>& properties() const { return properties_; }
    const ActionEntry* action(std::string_view name) const;
    const std::vector<ActionEntry>& actions() const { return actions_; }
    const EventEntry* event(std::string_view name) const;
    const std::vector<EventEntry>& events() const { return events_; }
    /// The custom endpoints, in definition order.
    const std::vector<EndpointEntry>& endpoints() const { return endpoints_; }

    /// The Thing itself, if it is a `T`.
    template <typename T>
    std::shared_ptr<T> instance() const {
        if (type_ != std::type_index(typeid(T))) {
            return nullptr;
        }
        return std::static_pointer_cast<T>(instance_);
    }

    /// The Thing's state, for `Server::thing_states`: its `thing_state`, or
    /// `{}`.
    json::Value thing_state() const;

    /// What the Thing Description describes.
    td::ThingInfo info() const;
    /// The Thing Description: see `td::describe`. Throws td::TdError.
    td::ThingDescription thing_description(const td::TdOptions& options) const;

    // For the runtime.
    /// Loads the Thing's settings file, if it has one.
    void load_settings();
    /// Opens the devices, then runs the start hook; on failure closes the
    /// devices opened, and throws.
    void start();
    /// Runs the stop hook, then closes the devices.
    void stop();
    /// The slots, by name.
    const std::vector<std::pair<std::string, std::shared_ptr<detail::SlotControl>>>& slots() const {
        return slots_;
    }

  private:
    struct DeviceHook {
        std::string name;
        std::function<void(const std::string&)> open;
        std::function<void()> close;
    };
    using InterfaceBuild = std::function<std::shared_ptr<void>(const std::shared_ptr<const ThingHandle>&)>;
    void close_devices(std::size_t count);

    std::string name_;
    std::string title_;
    std::optional<std::string> description_;
    std::vector<std::string> semantic_types_;
    std::vector<std::pair<std::string, std::string>> context_prefixes_;
    std::string class_name_;
    std::vector<PropertyEntry> properties_;
    std::vector<ActionEntry> actions_;
    std::vector<EventEntry> events_;
    std::vector<EndpointEntry> endpoints_;
    std::vector<DeviceHook> devices_;
    std::vector<std::pair<std::string, std::shared_ptr<detail::SlotControl>>> slots_;
    std::vector<std::pair<std::type_index, InterfaceBuild>> interfaces_;
    std::shared_ptr<detail::SettingsStore> settings_;
    std::function<void(ThingCtx&)> on_start_;
    std::function<void(ThingCtx&)> on_stop_;
    std::function<json::Value()> thing_state_;
    Server server_{nullptr};
    std::shared_ptr<void> instance_;
    std::type_index type_ = std::type_index(typeid(void));
};

class Runtime;

/// A Thing built from a typed configuration: its `Config` type, read with
/// its codec from the Thing's `kwargs` in the configuration file (or from its
/// `args`, field by field), and a constructor taking it.
template <typename T>
concept ConfiguredThing = ThingType<T> && requires { typename T::Config; } && Decodable<typename T::Config> &&
                          std::constructible_from<T, typename T::Config>;

/// A Thing a configuration file can name: a ConfiguredThing, or a Thing
/// without a `Config` that is default-constructible, whose `kwargs` must then
/// be empty (or `null`).
template <typename T>
concept FromConfig = ConfiguredThing<T> ||
                     (ThingType<T> && !requires { typename T::Config; } && std::default_initializable<T>);

namespace detail {

/// Checks the configuration of a Thing that takes none; throws
/// std::invalid_argument with serde's message.
void check_no_config(const json::Value& value);
/// `args`, as an object of the fields `schema` lists, in order; throws
/// std::invalid_argument if there are more args than fields.
json::Value args_as_object(const json::Value& args, const json::Value& schema);

/// A Thing built from its configuration (an object of kwargs, or an array
/// of args). Throws std::invalid_argument.
template <FromConfig T>
std::shared_ptr<T> make_from_config(const json::Value& value) {
    if constexpr (ConfiguredThing<T>) {
        using C = typename T::Config;
        try {
            return std::make_shared<T>(
                Codec<C>::decode(value.is_array() ? args_as_object(value, Codec<C>::json_schema()) : value));
        } catch (const DecodeError& error) {
            throw std::invalid_argument(error.what());
        }
    } else {
        check_no_config(value);
        return std::make_shared<T>();
    }
}

}  // namespace detail

/// Builds a Runtime.
class RuntimeBuilder {
  public:
    /// Enables the global lock (off by default).
    RuntimeBuilder& global_lock(bool enabled) {
        global_lock_ = enabled;
        return *this;
    }
    /// How long to wait for the global lock (50 ms by default).
    RuntimeBuilder& global_lock_timeout(std::chrono::nanoseconds timeout) {
        lock_timeout_ = timeout;
        return *this;
    }
    /// The level of the "Global lock was busy" log line (INFO by default).
    RuntimeBuilder& global_lock_log_level(log::Level level) {
        lock_log_level_ = level;
        return *this;
    }
    /// The clock retention is measured with (for tests).
    RuntimeBuilder& clock(Clock clock) {
        clock_ = std::move(clock);
        return *this;
    }
    /// The cap on worker threads.
    RuntimeBuilder& max_threads(std::size_t threads) {
        max_threads_ = threads;
        return *this;
    }

    /// Adds a Thing under `name`. Things start in the order they are added
    /// and stop in reverse.
    template <ThingType T>
    RuntimeBuilder& thing(std::string name, std::shared_ptr<T> thing) {
        things_.push_back({std::move(name), [thing](const std::string& thing_name,
                                                    std::shared_ptr<const RuntimeShared> shared) {
                               return ThingHandle::instantiate(thing_name, thing, std::move(shared));
                           }});
        return *this;
    }
    /// Adds a default-constructed Thing.
    template <ThingType T>
        requires std::default_initializable<T>
    RuntimeBuilder& thing(std::string name) {
        return thing(std::move(name), std::make_shared<T>());
    }

    /// Adds a Thing built from its configuration: `kwargs` (an object, or
    /// `null`) is read into `T::Config` when the runtime is built.
    template <FromConfig T>
    RuntimeBuilder& thing_from_config(std::string name, const json::Value& kwargs) {
        if (kwargs.is_null()) {
            return thing_from_args<T>(std::move(name), {}, json::Value::object());
        }
        if (!kwargs.is_object()) {
            return failing(std::move(name), "kwargs must be an object, not " + json::dump(kwargs));
        }
        return thing_from_args<T>(std::move(name), {}, kwargs);
    }
    /// Adds a Thing built from a configuration file entry's `args` and
    /// `kwargs` (an object). `T::Config` is read from `kwargs`, or from `args`
    /// (field by field) when there are no `kwargs`; giving both is an error.
    template <FromConfig T>
    RuntimeBuilder& thing_from_args(std::string name, std::vector<json::Value> args, json::Value kwargs) {
        things_.push_back(
            {std::move(name),
             [args = std::move(args), kwargs = std::move(kwargs)](
                 const std::string& thing_name, std::shared_ptr<const RuntimeShared> shared) {
                 if (!args.empty() && !kwargs.empty()) {
                     throw invalid_config(thing_name,
                                          "use either args or kwargs, not both: a C++ Thing's "
                                          "configuration is one typed value");
                 }
                 std::shared_ptr<T> thing;
                 try {
                     thing = detail::make_from_config<T>(args.empty() ? kwargs : json::Value(args));
                 } catch (const std::invalid_argument& error) {
                     throw invalid_config(thing_name, error.what());
                 }
                 return ThingHandle::instantiate(thing_name, std::move(thing), std::move(shared));
             }});
        return *this;
    }
    /// Overrides where a Thing's slots connect (the configuration's
    /// `thing_slots` for that Thing), by slot name.
    RuntimeBuilder& thing_slots(const std::string& thing,
                                std::vector<std::pair<std::string, SlotSelection>> slots);

    /// Registers a shared service, which actions receive as `Dep<S>`
    /// parameters. `S` may be an abstract interface.
    template <typename S>
    RuntimeBuilder& service(std::shared_ptr<S> service) {
        services_[std::type_index(typeid(S))] = {std::move(service), codec::short_name(typeid(S))};
        return *this;
    }
    /// The `application_config` of the configuration file.
    RuntimeBuilder& application_config(json::Value config) {
        application_config_ = std::move(config);
        return *this;
    }
    /// Persists settings in `folder` (Phase 5): each Thing gets
    /// `{folder}/{name}/`.
    RuntimeBuilder& settings_folder(std::filesystem::path folder) {
        settings_folder_ = std::move(folder);
        return *this;
    }

    /// Checks the names, builds each Thing's registry entries, connects the
    /// slots and works out the start order; throws BuildError.
    Runtime build();

  private:
    struct ThingFactory {
        std::string name;
        std::function<std::shared_ptr<ThingHandle>(const std::string&, std::shared_ptr<const RuntimeShared>)>
            make;
    };
    static BuildError invalid_config(const std::string& thing, const std::string& error);
    RuntimeBuilder& failing(std::string name, std::string error);
    bool global_lock_ = false;
    std::chrono::nanoseconds lock_timeout_ = default_lock_timeout;
    log::Level lock_log_level_ = log::Level::info;
    Clock clock_;
    std::size_t max_threads_ = WorkerPool::default_max_threads;
    std::vector<ThingFactory> things_;
    std::vector<std::pair<std::string, std::vector<std::pair<std::string, SlotSelection>>>> slot_overrides_;
    std::unordered_map<std::type_index, Server::Service> services_;
    std::optional<json::Value> application_config_;
    std::optional<std::filesystem::path> settings_folder_;
};

/// The running Things of a server: their registry entries, invocations,
/// message broker and global lock, and their start and stop.
class Runtime {
  public:
    static RuntimeBuilder builder() { return {}; }

    Runtime(Runtime&&) noexcept = default;
    Runtime& operator=(Runtime&&) noexcept = default;
    ~Runtime();

    /// A Thing by name.
    const ThingHandle* thing(std::string_view name) const;
    /// Every Thing, in the order they were added.
    const std::vector<std::shared_ptr<ThingHandle>>& things() const { return things_; }
    /// A typed reference to a Thing, for in-process calls, if the Thing named
    /// `name` is a `T`.
    template <typename T>
    std::optional<ThingRef<T>> thing_ref(std::string_view name) const;

    InvocationManager& invocations() const { return *shared_->invocations; }
    const std::shared_ptr<MessageBroker>& broker() const { return shared_->broker; }
    /// The global lock, if enabled.
    const std::shared_ptr<GlobalLock>& global_lock() const { return shared_->lock; }
    /// The order the Things start in: the Things a Thing's slots connect to
    /// first, otherwise configuration order. They stop in reverse.
    const std::vector<std::string>& start_order() const { return start_order_; }
    const Server& server() const { return shared_->server; }
    const std::shared_ptr<WorkerPool>& pool() const { return shared_->pool; }

    /// Loads every Thing's settings, then starts the Things in start order:
    /// for each, its devices open and then its start hook runs. If one fails,
    /// the Things already started stop in reverse order and StartupError is
    /// thrown.
    void start();
    /// Stops the started Things in reverse order: for each, its stop hook
    /// runs and then its devices close.
    void stop();
    /// Closes the message broker (ending observation streams), cancels every
    /// unfinished invocation, waits up to `grace` for them to finish, then
    /// stops the Things.
    void shutdown(std::chrono::nanoseconds grace);

  private:
    friend class RuntimeBuilder;
    Runtime() = default;

    std::shared_ptr<RuntimeShared> shared_;
    std::vector<std::shared_ptr<ThingHandle>> things_;
    std::vector<std::string> start_order_;
    std::unique_ptr<std::mutex> started_mutex_ = std::make_unique<std::mutex>();
    std::vector<std::shared_ptr<ThingHandle>> started_;
};

/// A typed reference to a running Thing, for in-process calls. Calling its
/// affordances through it applies what a client's request would get:
/// validation, the global lock and read-only rules. Calls run in the
/// caller's invocation context, so cancelling the caller cancels them, and
/// their log lines go to the caller's invocation.
template <typename T>
class ThingRef {
  public:
    ThingRef(std::shared_ptr<const ThingHandle> handle, std::shared_ptr<T> thing)
        : handle_(std::move(handle)), thing_(std::move(thing)) {}

    /// The name the Thing is served under.
    const std::string& name() const { return handle_->name(); }
    /// The Thing itself: calling its members directly is a plain C++ call,
    /// without validation or the global lock.
    T& thing() const { return *thing_; }
    const std::shared_ptr<T>& shared() const { return thing_; }
    const ThingHandle& handle() const { return *handle_; }

    /// Calls the action whose function is `function` (the member function
    /// pointer it was defined with), with its data parameters, and returns
    /// its output: `stage.call(&Stage::move_to, 0, 0, 250)`.
    template <typename F, typename... Args>
        requires detail::FunctionOf<T, F>
    auto call(F function, Args&&... args) const;

    /// Calls an action by name, with any input, and returns its output as
    /// JSON.
    json::Value call_action(std::string_view action, const json::Value& input) const {
        return entry(action).call(input);
    }
    /// Reads a property by name, as a client would.
    json::Value read_property(std::string_view property) const { return property_entry(property).read(); }
    /// Writes a property by name, as a client would: the value is validated,
    /// the global lock is taken, and read-only properties refuse.
    void write_property(std::string_view property, const json::Value& value) const {
        property_entry(property).write(value);
    }

  private:
    const ActionEntry& entry(std::string_view action) const {
        if (const auto* entry = handle_->action(action)) {
            return *entry;
        }
        throw std::invalid_argument("Thing `" + handle_->name() + "` has no action named `" +
                                    std::string(action) + "`");
    }
    const PropertyEntry& property_entry(std::string_view property) const {
        if (const auto* entry = handle_->property(property)) {
            return *entry;
        }
        throw PropertyError::failed("Thing `" + handle_->name() + "` has no property named `" +
                                    std::string(property) + "`");
    }

    std::shared_ptr<const ThingHandle> handle_;
    std::shared_ptr<T> thing_;
};

// ---- Template definitions -------------------------------------------------------------

namespace detail {

/// Builds the entries of one Thing (runtime.cpp).
void check_affordance_names(const std::string& thing, const std::vector<std::string>& names);
/// Why a custom endpoint's path can't be used, if it can't: it must be
/// relative, with segments that aren't empty, `.` or `..`.
std::optional<std::string> endpoint_path_problem(const std::string& path);

}  // namespace detail

template <ThingType T>
std::shared_ptr<ThingHandle> ThingHandle::instantiate(const std::string& name, std::shared_ptr<T> thing,
                                                      std::shared_ptr<const RuntimeShared> shared) {
    ThingSpec<T> spec;
    T::define(spec);
    std::vector<std::string> names;
    for (const auto& p : spec.properties()) {
        names.push_back(p.name);
    }
    for (const auto& a : spec.actions()) {
        names.push_back(a.name);
    }
    for (const auto& e : spec.events()) {
        names.push_back(e.name);
    }
    detail::check_affordance_names(name, names);

    auto handle = std::make_shared<ThingHandle>();
    handle->name_ = name;
    handle->title_ = spec.title_text();
    handle->description_ = spec.description_text();
    handle->semantic_types_ = spec.semantic_types();
    handle->context_prefixes_ = spec.context_prefixes();
    handle->class_name_ = spec.class_name_text().value_or(spec.title_text());
    handle->server_ = shared->server;
    handle->instance_ = thing;
    handle->type_ = std::type_index(typeid(T));

    auto definition_error = [&](const std::string& affordance, const std::string& message) {
        return BuildError(DefinitionError(name, affordance, message));
    };
    std::shared_ptr<detail::SettingsStore> settings;
    if (const auto& folder = shared->server.settings_folder()) {
        settings = std::make_shared<detail::SettingsStore>(*folder, name, handle->class_name_);
    }
    for (auto& slot : spec.properties()) {
        std::shared_ptr<PropertyHandler> handler;
        try {
            handler = slot.build(thing, name, slot.name, shared, settings);
        } catch (const PropertyError& error) {
            throw definition_error(slot.name, error.what());
        } catch (const std::exception& error) {
            throw definition_error(slot.name, error.what());
        }
        PropertyMeta meta = slot.meta();
        handle->properties_.emplace_back(slot.name, name, meta.title.value_or(slot.name), meta.description,
                                         meta.read_only.value_or(slot.default_read_only()),
                                         meta.global_lock.value_or(true), meta.unit, meta.semantic_types,
                                         std::move(handler), shared);
    }
    for (auto& slot : spec.actions()) {
        std::shared_ptr<ActionHandler> handler;
        try {
            handler = slot.build(thing);
        } catch (const std::exception& error) {
            throw definition_error(slot.name, error.what());
        }
        handle->actions_.emplace_back(slot.name, name, slot.options->meta(), std::move(handler), shared);
    }
    for (auto& slot : spec.events()) {
        std::optional<td::DataSchema> data;
        try {
            data = slot.build(*thing, name, shared->broker);
        } catch (const std::exception& error) {
            throw definition_error(slot.name, error.what());
        }
        handle->events_.push_back({slot.name, slot.options->title_text().value_or(slot.name),
                                   slot.options->description_text(), slot.options->semantic_types(),
                                   std::move(data)});
    }
    for (auto& slot : spec.endpoints()) {
        if (auto problem = detail::endpoint_path_problem(slot.path)) {
            throw definition_error(slot.path, *problem);
        }
        for (const auto& existing : handle->endpoints_) {
            if (existing.method == slot.method && existing.path == slot.path) {
                throw definition_error(slot.path, "two endpoints answer " + slot.method + " " + slot.path);
            }
        }
        handle->endpoints_.push_back({slot.method, slot.path, slot.options->meta(), slot.build(thing)});
    }
    for (auto& slot : spec.devices()) {
        handle->devices_.push_back(
            {slot.name,
             [thing, open = slot.open](const std::string& device_name) { open(*thing, device_name); },
             [thing, close = slot.close] { close(*thing); }});
    }
    for (auto& slot : spec.slots()) {
        handle->slots_.emplace_back(slot.name, slot.build(thing));
    }
    handle->interfaces_ = std::move(spec.interfaces());
    if (settings && settings->has_settings()) {
        handle->settings_ = std::move(settings);
    }
    if (const auto& hook = spec.start_hook()) {
        handle->on_start_ = [thing, hook](ThingCtx& ctx) { hook(*thing, ctx); };
    }
    if (const auto& hook = spec.stop_hook()) {
        handle->on_stop_ = [thing, hook](ThingCtx& ctx) { hook(*thing, ctx); };
    }
    if (const auto& state = spec.state_function()) {
        handle->thing_state_ = [thing, state] { return state(*thing); };
    }
    return handle;
}

namespace detail {

template <typename T>
std::optional<ThingRef<T>> thing_ref_of(const std::shared_ptr<const ThingHandle>& handle) {
    if (auto instance = handle->instance<T>()) {
        return ThingRef<T>(handle, std::move(instance));
    }
    return std::nullopt;
}

/// What a slot of `T` holds: a ThingRef<T> for a Thing type, a
/// std::shared_ptr<T> for an interface.
template <typename T>
struct SlotTarget {
    static_assert(
        requires { sizeof(T); },
        "a slot's Thing type or interface must be complete where the slot is declared: Things that "
        "refer to each other connect one way through an interface (t.provides<I>())");
    static constexpr bool is_thing = ThingType<T>;
    using Handle = std::conditional_t<is_thing, ThingRef<T>, std::shared_ptr<T>>;

    /// The handle for a Thing, if the Thing is (or provides) this target.
    static std::optional<Handle> resolve(const std::shared_ptr<ThingHandle>& thing) {
        if constexpr (is_thing) {
            return thing_ref_of<T>(thing);
        } else {
            if (auto provided = thing->template provided<T>()) {
                return provided;
            }
            return std::nullopt;
        }
    }
    /// The target's name, for messages.
    static std::string target_name() { return codec::short_name(typeid(T)); }
};

}  // namespace detail

template <typename T>
std::optional<ThingRef<T>> Runtime::thing_ref(std::string_view name) const {
    for (const auto& handle : things_) {
        if (handle->name() == name) {
            if (auto instance = handle->instance<T>()) {
                return ThingRef<T>(handle, std::move(instance));
            }
            return std::nullopt;
        }
    }
    return std::nullopt;
}

template <typename T>
std::optional<ThingRef<T>> Server::thing_ref(std::string_view name) const {
    for (const auto& [thing_name, handle] : handles()) {
        if (thing_name == name) {
            if (auto instance = handle->template instance<T>()) {
                return ThingRef<T>(handle, std::move(instance));
            }
            return std::nullopt;
        }
    }
    return std::nullopt;
}

template <typename T>
std::vector<ThingRef<T>> Server::things_of_type() const {
    std::vector<ThingRef<T>> out;
    for (const auto& [thing_name, handle] : handles()) {
        if (auto instance = handle->template instance<T>()) {
            out.emplace_back(handle, std::move(instance));
        }
    }
    return out;
}

template <typename T>
template <typename F, typename... Args>
    requires detail::FunctionOf<T, F>
auto ThingRef<T>::call(F function, Args&&... args) const {
    using Function = detail::ThingFunction<T, F>;
    using Info = detail::ParamInfo<typename Function::args>;
    using Output = typename detail::OutputOf<typename Function::result>::type;
    using DataTuple = typename Info::data_tuple;
    static_assert(sizeof...(Args) == Info::data_count, "pass the action's data parameters, in order");

    const ActionEntry* entry = nullptr;
    for (const auto& action : handle_->actions()) {
        if (action.handler().runs(typeid(F), &function)) {
            entry = &action;
        }
    }
    if (!entry) {
        throw std::invalid_argument("Thing `" + handle_->name() + "` has no action running this function");
    }
    // The input as a client would send it.
    json::Value input;
    const auto& names = entry->handler().parameter_names();
    auto as_object = [&] {
        json::Value object = json::Value::object();
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            ((object[names[I]] = encode(std::tuple_element_t<I, DataTuple>(std::forward<Args>(args)))), ...);
        }(std::make_index_sequence<sizeof...(Args)>{});
        return object;
    };
    if constexpr (Info::data_count == 0) {
        input = nullptr;
    } else if constexpr (sizeof...(Args) == 1) {
        // The whole input, or one named parameter.
        input = names.empty() ? encode(std::tuple_element_t<0, DataTuple>(std::forward<Args>(args)...))
                              : as_object();
    } else {
        input = as_object();
    }
    auto prepared = entry->handler().prepare(input);
    std::any output = entry->run_in_process([&](ActionCtx& ctx) { return prepared.run_typed(ctx); });
    using Result = std::remove_cvref_t<typename Function::result>;
    if constexpr (std::is_same_v<Output, std::monostate> && !std::is_same_v<Result, std::monostate>) {
        return;  // the action returns nothing
    } else {
        return std::any_cast<Output>(std::move(output));
    }
}

}  // namespace teta::wot
