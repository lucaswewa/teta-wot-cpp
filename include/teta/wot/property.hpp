// Properties: a port of teta-wot-core's `property`.
//
// Data properties are `Prop<T>` cells, members of the Thing; functional
// properties are a getter (and optionally a setter and a resetter) of the
// Thing. Clients (and the HTTP binding) reach either through a
// PropertyEntry, which validates their values as pydantic would, takes the
// global lock for writes, and refuses writes to read-only properties.

#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

#include "teta/wot/broker.hpp"
#include "teta/wot/codec.hpp"
#include "teta/wot/runtime_shared.hpp"
#include "teta/wot/td/constraints.hpp"
#include "teta/wot/validate.hpp"

namespace teta::wot {

/// A property operation failed.
class PropertyError : public std::exception {
  public:
    enum class Kind {
        /// The value doesn't validate (HTTP 422).
        invalid,
        /// Clients may not write the property.
        read_only,
        /// The property can't be reset.
        not_resettable,
        /// The global lock is held by someone else (HTTP 409).
        global_lock_busy,
        /// A device call failed.
        device,
        /// The operation was cancelled.
        cancelled,
        /// The property is defined in a way that can't work, for example a
        /// constraint that doesn't suit its type.
        definition,
        /// A getter, setter or resetter failed.
        failed,
    };

    static PropertyError invalid(ValidationError error);
    static PropertyError read_only(const std::string& name);
    static PropertyError not_resettable(const std::string& name);
    static PropertyError definition(const std::string& message);
    static PropertyError failed(const std::string& message);
    /// What an exception thrown by a getter, setter or resetter (or by the
    /// runtime) means: a PropertyError stays as it is.
    static PropertyError from(std::exception_ptr error);

    Kind kind() const { return kind_; }
    /// The validation error, for `invalid`.
    const ValidationError* validation() const { return validation_ ? &*validation_ : nullptr; }
    const char* what() const noexcept override { return message_.c_str(); }
    /// The problem details for this error.
    ProblemDetails problem() const;

  private:
    PropertyError(Kind kind, std::string message) : kind_(kind), message_(std::move(message)) {}
    Kind kind_;
    std::string message_;
    std::optional<ValidationError> validation_;
};

namespace detail {

/// Where a bound Prop publishes its changes.
struct PropBinding {
    std::string thing;
    std::string name;
    std::shared_ptr<MessageBroker> broker;
    /// Saves the Thing's settings file, if the property is a setting.
    std::function<void()> save;
};

/// A client's value, validated (errors under `body`) and decoded.
template <Decodable T>
T from_client(const SchemaValidator& validator, const json::Value& value) {
    const Loc loc{"body"};
    json::Value coerced = validator.validate(value, loc);
    try {
        return Codec<T>::decode(coerced);
    } catch (const DecodeError& error) {
        throw ValidationError::decode_error(loc, error.what(), std::move(coerced));
    }
}

/// The DataSchema of `T` with `constraints`, prepared for validation.
template <Encodable T>
SchemaValidator build_validator(const td::Constraints& constraints) {
    td::DataSchema schema = for_type<T>();
    constraints.apply(schema);
    return SchemaValidator(std::move(schema));
}

}  // namespace detail

/// What a property value needs: a codec that reads and writes it, and
/// copying.
template <typename T>
concept PropertyValue = Decodable<T> && std::copyable<T>;

template <PropertyValue T>
class Watcher;

/// The value of a data property: a typed cell with validation and change
/// notification.
///
/// The Thing owns its Props and reads and writes them from C++ with `get` and
/// `set`. `set` checks the property's constraints (the type is checked by the
/// compiler), stores the value and notifies observers: C++ code through
/// `subscribe`, and clients through the server's message broker. Read-only
/// only restricts clients; the Thing can always set it.
template <PropertyValue T>
class Prop {
  public:
    /// A cell holding `T{}`.
    Prop()
        requires std::default_initializable<T>
        : Prop(T{}) {}
    /// A cell holding `default_value`, which is also what it resets to.
    Prop(T default_value) : state_(std::make_shared<State>(std::move(default_value))) {}
    Prop(Prop&&) noexcept = default;
    Prop& operator=(Prop&&) noexcept = default;

    /// Adds value constraints (`ge`, `lt`, `pattern`, …): checked on every
    /// write, and shown in the Thing Description.
    Prop&& with_constraints(td::Constraints constraints) && {
        set_constraints(std::move(constraints));
        return std::move(*this);
    }
    void set_constraints(td::Constraints constraints) {
        std::lock_guard lock(state_->mutex);
        state_->constraints = std::move(constraints);
    }
    const td::Constraints& constraints() const { return state_->constraints; }
    const T& default_value() const { return state_->default_value; }

    /// A copy of the current value.
    T get() const {
        std::lock_guard lock(state_->mutex);
        return state_->value;
    }
    /// Reads the current value without copying it: `f(const T&)`, with the
    /// cell locked (don't hold on to it, or write it from `f`).
    template <typename F>
    decltype(auto) read(F&& f) const {
        std::lock_guard lock(state_->mutex);
        return std::forward<F>(f)(std::as_const(state_->value));
    }

    /// Checks the constraints, stores `value` and notifies observers; a
    /// setting is then saved. Throws PropertyError.
    void set(T value) {
        if (!state_->constraints.empty()) {
            try {
                validator().validate(encode(value), {});
            } catch (const ValidationError& error) {
                throw PropertyError::invalid(error);
            }
        }
        store(std::move(value));
        save();
    }
    /// Changes a copy of the value with `f(T&)`, then checks and stores it
    /// like `set`. If the new value is invalid nothing changes.
    template <typename F>
    void update(F&& f) {
        T value = get();
        std::forward<F>(f)(value);
        set(std::move(value));
    }
    /// Sets the default value again.
    void reset() { set(state_->default_value); }

    /// Watches the value.
    Watcher<T> subscribe() const;

    /// The property's DataSchema: its type's, with the constraints applied.
    /// Throws PropertyError (`definition`).
    td::DataSchema data_schema() const { return validator().schema(); }

    // For the runtime.
    /// Validates a client's value (errors under `body`), converts it and sets
    /// it.
    void set_from_client(const json::Value& value) {
        store(detail::from_client<T>(validator(), value));
        save();
    }
    /// Validates a client's value without setting it.
    void check_from_client(const json::Value& value) const {
        (void)detail::from_client<T>(validator(), value);
    }
    /// Where changes are published; the first binding stays.
    void bind(detail::PropBinding binding) {
        std::lock_guard lock(state_->mutex);
        if (!state_->binding) {
            state_->binding = std::move(binding);
        }
    }

  private:
    friend class Watcher<T>;

    struct State {
        explicit State(T initial) : value(initial), default_value(std::move(initial)) {}
        mutable std::mutex mutex;
        std::condition_variable_any changed;
        T value;
        std::uint64_t version = 0;
        T default_value;
        td::Constraints constraints;
        std::once_flag validator_once;
        std::optional<SchemaValidator> validator;
        std::string validator_error;
        std::optional<detail::PropBinding> binding;
    };

    const SchemaValidator& validator() const {
        std::call_once(state_->validator_once, [&] {
            try {
                state_->validator.emplace(detail::build_validator<T>(constraints()));
            } catch (const std::exception& error) {
                state_->validator_error = error.what();
            }
        });
        if (!state_->validator) {
            throw PropertyError::definition(state_->validator_error);
        }
        return *state_->validator;
    }

    std::optional<detail::PropBinding> binding() const {
        std::lock_guard lock(state_->mutex);
        return state_->binding;
    }

    void store(T value) {
        if (const auto bound = binding();
            bound && bound->broker->has_subscribers(bound->thing, bound->name)) {
            bound->broker->publish(
                Message::now(bound->thing, bound->name, MessageKind::property, encode(value)));
        }
        {
            std::lock_guard lock(state_->mutex);
            state_->value = std::move(value);
            ++state_->version;
        }
        state_->changed.notify_all();
    }

    void save() const {
        if (const auto bound = binding(); bound && bound->save) {
            bound->save();
        }
    }

    std::shared_ptr<State> state_;
};

/// Watches a Prop: sees its latest value (values set in quick succession may
/// be seen once), and can wait for the next change.
template <PropertyValue T>
class Watcher {
  public:
    /// Whether the value changed since it was last seen with `get`.
    bool has_changed() const {
        std::lock_guard lock(state_->mutex);
        return state_->version != seen_;
    }
    /// The current value, which is then seen.
    T get() {
        std::lock_guard lock(state_->mutex);
        seen_ = state_->version;
        return state_->value;
    }
    /// The current value, without marking it seen.
    T peek() const {
        std::lock_guard lock(state_->mutex);
        return state_->value;
    }
    /// Waits up to `timeout` for a change; returns whether there was one.
    bool wait_changed(std::chrono::nanoseconds timeout) const {
        std::unique_lock lock(state_->mutex);
        return state_->changed.wait_for(lock, timeout, [&] { return state_->version != seen_; });
    }
    /// Waits for a change until `stop` is requested; returns whether there
    /// was one.
    bool wait_changed(std::stop_token stop) const {
        std::unique_lock lock(state_->mutex);
        return state_->changed.wait(lock, stop, [&] { return state_->version != seen_; });
    }

  private:
    friend class Prop<T>;
    explicit Watcher(std::shared_ptr<typename Prop<T>::State> state) : state_(std::move(state)) {
        std::lock_guard lock(state_->mutex);
        seen_ = state_->version;
    }
    std::shared_ptr<typename Prop<T>::State> state_;
    std::uint64_t seen_ = 0;
};

template <PropertyValue T>
Watcher<T> Prop<T>::subscribe() const {
    return Watcher<T>(state_);
}

/// The type-erased operations of one property of one Thing (internal).
class PropertyHandler {
  public:
    virtual ~PropertyHandler() = default;
    virtual json::Value read() = 0;
    virtual void write(const json::Value& value) = 0;
    /// Validates a client's value, as `write` would, without writing it.
    virtual void check(const json::Value& value) = 0;
    virtual bool resettable() const = 0;
    /// Only called when `resettable()` is true.
    virtual void reset() = 0;
    virtual std::optional<json::Value> default_json() const = 0;
    virtual const td::DataSchema& schema() const = 0;
    virtual bool has_setter() const = 0;
    virtual bool observable() const = 0;
};

/// What a property says about itself, besides its handler.
struct PropertyMeta {
    std::optional<std::string> title;
    std::optional<std::string> description;
    std::optional<bool> read_only;
    std::optional<bool> global_lock;
    std::optional<std::string> unit;
    std::vector<std::string> semantic_types;
    bool setting = false;
};

/// One property of a running Thing: what clients (and the HTTP binding) use.
/// Every operation throws PropertyError.
class PropertyEntry {
  public:
    PropertyEntry(std::string name, std::string thing, std::string title,
                  std::optional<std::string> description, bool read_only, bool global_lock,
                  std::optional<std::string> unit, std::vector<std::string> semantic_types,
                  std::shared_ptr<PropertyHandler> handler, std::shared_ptr<const RuntimeShared> shared);

    const std::string& name() const { return name_; }
    const std::string& title() const { return title_; }
    const std::optional<std::string>& description() const { return description_; }
    /// Whether clients may only read the property.
    bool is_read_only() const { return read_only_; }
    /// Whether clients may observe it (data properties only).
    bool is_observable() const { return handler_->observable(); }
    /// Whether clients may reset it. Read-only properties can't be reset by
    /// clients (reference bug B4 fixed).
    bool is_resettable() const { return !read_only_ && handler_->resettable(); }
    /// The DataSchema, with the constraints.
    const td::DataSchema& data_schema() const { return handler_->schema(); }
    std::optional<json::Value> default_value() const { return handler_->default_json(); }
    const std::optional<std::string>& unit() const { return unit_; }
    const std::vector<std::string>& semantic_types() const { return semantic_types_; }

    /// Reads the value. Reads don't take the global lock.
    json::Value read() const;
    /// Writes a value sent by a client: refuses read-only properties, takes
    /// the global lock if enabled, validates the value (errors under `body`)
    /// and stores it. `null` counts as a missing body and is refused, even
    /// for a property that may be `null`.
    void write(const json::Value& value) const;
    /// Checks a value as `write` would, without the lock or writing: for
    /// writing several properties only if every value is valid.
    void validate(const json::Value& value) const;
    /// Resets the property for a client: refuses read-only properties, takes
    /// the global lock if enabled, and resets.
    void reset() const;

  private:
    std::optional<GlobalLock::Guard> lock() const;

    std::string name_;
    std::string thing_;
    std::string title_;
    std::optional<std::string> description_;
    bool read_only_;
    bool global_lock_;
    std::optional<std::string> unit_;
    std::vector<std::string> semantic_types_;
    std::shared_ptr<PropertyHandler> handler_;
    std::shared_ptr<const RuntimeShared> shared_;
};

}  // namespace teta::wot
