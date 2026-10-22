// Thing types and their definitions: the counterpart of teta-wot-core's
// `thing` and `ThingDefinition`, and of what teta-wot's macros generate
// (`#[derive(Thing)]`, `#[thing_impl]`).
//
// A Thing is a class with a static `define` function, which declares its
// affordances with member pointers (or functions taking the Thing first), so
// every type is checked at compile time:
//
//     class Light : public tw::Thing<Light> {
//       public:
//         tw::Prop<std::int64_t> brightness{100};
//         tw::Prop<bool> is_on{false};
//
//         std::string status() const { ... }
//         void toggle() { is_on.update([](bool& on) { on = !on; }); }
//
//         static void define(tw::ThingSpec<Light>& t) {
//             t.doc("A testing light Thing.");
//             t.property("brightness", &Light::brightness).doc("The brightness.").ge(0).le(100);
//             t.property("is_on", &Light::is_on).doc("Whether the light is on.").read_only();
//             t.property("status", &Light::status).doc("A human-readable status.");
//             t.action("toggle", &Light::toggle).doc("Toggle the light.");
//         }
//     };
//
// Action parameters are named in order, so a wrong count fails to compile:
// `t.action<"x", "y">("move_to", &Stage::move_to)`. Parameters of type
// `ActionCtx&`, `Server` and `Dep<S>` are supplied by the server and aren't
// named. `t.action("set", &T::set, tw::whole_input)` makes the one parameter
// the whole input.

#pragma once

#include <algorithm>
#include <any>
#include <array>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

#include "teta/wot/action.hpp"
#include "teta/wot/codec.hpp"
#include "teta/wot/context.hpp"
#include "teta/wot/device.hpp"
#include "teta/wot/event.hpp"
#include "teta/wot/property.hpp"
#include "teta/wot/runtime_shared.hpp"
#include "teta/wot/td/constraints.hpp"
#include "teta/wot/td/describe.hpp"
#include "teta/wot/validate.hpp"

namespace teta::wot {

/// A Thing definition that can't work: `` `thing.affordance`: message ``.
class DefinitionError : public std::exception {
  public:
    DefinitionError(std::string thing, std::string affordance, std::string message);
    const std::string& thing() const { return thing_; }
    const std::string& affordance() const { return affordance_; }
    const std::string& message() const { return message_; }
    const char* what() const noexcept override { return text_.c_str(); }

  private:
    std::string thing_;
    std::string affordance_;
    std::string message_;
    std::string text_;
};

/// Affordance names a Thing may not use, because the server needs
/// `/{thing}/{name}` for something else.
inline constexpr std::array<std::string_view, 1> reserved_affordance_names = {"ws"};

/// Why `name` can't be an affordance's name, if it can't: it must be one URL
/// path segment (letters, digits, `_` and `-`) and not reserved.
std::optional<std::string> affordance_name_problem(std::string_view name);

/// What a Thing knows about its place on the server; its lifecycle hooks
/// receive one.
class ThingCtx {
  public:
    ThingCtx(std::string name, Server server) : name_(std::move(name)), server_(std::move(server)) {}
    /// The name the Thing is served under.
    const std::string& name() const { return name_; }
    const Server& server() const { return server_; }
    /// The Thing's own folder for persistent files, if the server persists
    /// settings.
    std::optional<std::filesystem::path> settings_folder() const;

  private:
    std::string name_;
    Server server_;
};

/// A convenient base for Things: `shared_from_this()` gives the shared
/// pointer the runtime holds. Things don't have to derive from it.
template <typename Self>
class Thing : public std::enable_shared_from_this<Self> {};

template <typename T>
class ThingSpec;

/// A Thing type: a class with `static void define(ThingSpec<T>&)`.
template <typename T>
concept ThingType = std::is_class_v<T> && requires(ThingSpec<T>& spec) { T::define(spec); };

/// Makes an action's only parameter its whole input.
struct WholeInput {};
inline constexpr WholeInput whole_input{};

namespace detail {

/// A string literal as a template argument: `t.action<"x", "y">(...)`.
template <std::size_t N>
struct FixedString {
    char text[N]{};
    constexpr FixedString(const char (&value)[N]) { std::copy_n(value, N, text); }
    constexpr std::string_view view() const { return {text, N - 1}; }
};

template <typename... Ts>
struct TypeList {
    static constexpr std::size_t size = sizeof...(Ts);
    template <std::size_t I>
    using at = std::tuple_element_t<I, std::tuple<Ts...>>;
};

/// The signature of a member function, a function, or a functor whose
/// `operator()` isn't a template.
template <typename F, typename = void>
struct Signature;

#define TETA_WOT_MEMBER_SIGNATURE(QUALIFIERS)        \
    template <typename R, typename C, typename... A> \
    struct Signature<R (C::*)(A...) QUALIFIERS> {    \
        using result = R;                            \
        using args = TypeList<A...>;                 \
        using object = C;                            \
        static constexpr bool member = true;         \
    };
TETA_WOT_MEMBER_SIGNATURE()
TETA_WOT_MEMBER_SIGNATURE(const)
TETA_WOT_MEMBER_SIGNATURE(noexcept)
TETA_WOT_MEMBER_SIGNATURE(const noexcept)
#undef TETA_WOT_MEMBER_SIGNATURE

template <typename R, typename... A>
struct Signature<R (*)(A...)> {
    using result = R;
    using args = TypeList<A...>;
    using object = void;
    static constexpr bool member = false;
};
template <typename R, typename... A>
struct Signature<R (*)(A...) noexcept> : Signature<R (*)(A...)> {};

template <typename Call>
struct CallOperator;
#define TETA_WOT_CALL_SIGNATURE(QUALIFIERS)          \
    template <typename R, typename C, typename... A> \
    struct CallOperator<R (C::*)(A...) QUALIFIERS> { \
        using result = R;                            \
        using args = TypeList<A...>;                 \
        using object = void;                         \
        static constexpr bool member = false;        \
    };
TETA_WOT_CALL_SIGNATURE()
TETA_WOT_CALL_SIGNATURE(const)
TETA_WOT_CALL_SIGNATURE(noexcept)
TETA_WOT_CALL_SIGNATURE(const noexcept)
#undef TETA_WOT_CALL_SIGNATURE

template <typename F>
struct Signature<F, std::void_t<decltype(&F::operator())>> : CallOperator<decltype(&F::operator())> {};

template <typename List>
struct Front;
template <typename A, typename... Rest>
struct Front<TypeList<A, Rest...>> {
    using type = A;
    using rest = TypeList<Rest...>;
};

/// What a function of a Thing takes besides the Thing: all its parameters
/// for a member function, the ones after the first otherwise.
template <bool member, typename Args>
struct ArgsAfterThing {
    using type = Args;
};
template <typename Args>
struct ArgsAfterThing<false, Args> {
    using type = typename Front<Args>::rest;
};

template <typename T, typename F>
struct ThingFunction {
    using signature = Signature<F>;
    using result = typename signature::result;
    using args = typename ArgsAfterThing<signature::member, typename signature::args>::type;
};

/// Calls a member function of the Thing, or a function taking it first.
template <typename T, typename F, typename... A>
decltype(auto) invoke_on(F& f, T& thing, A&&... args) {
    if constexpr (std::is_member_function_pointer_v<F>) {
        return (thing.*f)(std::forward<A>(args)...);
    } else {
        return f(thing, std::forward<A>(args)...);
    }
}

/// Whether `F` is something a Thing's function can be: a member function of
/// the Thing (or a base), or a function whose first parameter is the Thing.
template <typename T, typename F>
concept FunctionOf = requires { typename Signature<F>::result; } && [] {
    using S = Signature<F>;
    if constexpr (S::member) {
        return std::is_base_of_v<typename S::object, T>;
    } else if constexpr (S::args::size == 0) {
        return false;
    } else {
        using First = typename Front<typename S::args>::type;
        return std::is_lvalue_reference_v<First> && std::is_same_v<std::remove_cvref_t<First>, T>;
    }
}();

template <typename>
inline constexpr bool is_dep = false;
template <typename S>
inline constexpr bool is_dep<Dep<S>> = true;

enum class ParamKind { data, ctx, server, dep };

template <typename A>
constexpr ParamKind param_kind() {
    using D = std::remove_cvref_t<A>;
    if constexpr (std::is_same_v<D, ActionCtx>) {
        return ParamKind::ctx;
    } else if constexpr (std::is_same_v<D, Server>) {
        return ParamKind::server;
    } else if constexpr (is_dep<D>) {
        return ParamKind::dep;
    } else {
        return ParamKind::data;
    }
}

template <typename A>
inline constexpr bool is_data_param = param_kind<A>() == ParamKind::data;

template <typename>
struct ParamInfo;
template <typename... A>
struct ParamInfo<TypeList<A...>> {
    static constexpr std::size_t count = sizeof...(A);
    static constexpr std::array<bool, sizeof...(A) + 1> is_data = {is_data_param<A>..., false};
    static constexpr std::size_t data_count = (std::size_t{0} + ... + (is_data_param<A> ? 1 : 0));
    /// The position of parameter `I` among the data parameters.
    template <std::size_t I>
    static constexpr std::size_t data_index = [] {
        std::size_t n = 0;
        for (std::size_t i = 0; i < I; ++i) {
            n += is_data[i] ? 1 : 0;
        }
        return n;
    }();
    using data_tuple = decltype(std::tuple_cat(
        std::declval<
            std::conditional_t<is_data_param<A>, std::tuple<std::remove_cvref_t<A>>, std::tuple<>>>()...));
    static constexpr bool data_by_value_or_const_ref =
        (... && (!is_data_param<A> || !std::is_lvalue_reference_v<A> ||
                 std::is_const_v<std::remove_reference_t<A>>));
    static constexpr bool data_decodable = (... && (!is_data_param<A> || Decodable<std::remove_cvref_t<A>>));
};

template <typename>
inline constexpr bool is_expected = false;
template <typename V, typename E>
inline constexpr bool is_expected<tl::expected<V, E>> = true;

/// What an action returns, as its output type: `void` is `std::monostate`
/// (Rust's `()`), and an `Expected<V>` is `V`.
template <typename R>
struct OutputOf {
    using type = std::conditional_t<std::is_void_v<R>, std::monostate, std::remove_cvref_t<R>>;
};
template <typename V, typename E>
struct OutputOf<tl::expected<V, E>> {
    using type = std::conditional_t<std::is_void_v<V>, std::monostate, V>;
};

/// Runs `call` and returns its result as the output type, throwing an
/// Expected's error.
template <typename R, typename Call>
typename OutputOf<R>::type run_for_output(Call&& call) {
    if constexpr (std::is_void_v<R>) {
        std::forward<Call>(call)();
        return {};
    } else if constexpr (is_expected<std::remove_cvref_t<R>>) {
        auto result = std::forward<Call>(call)();
        if (!result) {
            throw std::move(result).error();
        }
        if constexpr (std::is_void_v<typename std::remove_cvref_t<R>::value_type>) {
            return {};
        } else {
            return std::move(*result);
        }
    } else {
        return std::forward<Call>(call)();
    }
}

}  // namespace detail

// ---- Options ------------------------------------------------------------------------

/// The options every affordance has.
template <typename Self>
class Described {
  public:
    /// The title (by default the affordance's name).
    Self& title(std::string text) {
        title_ = std::move(text);
        return self();
    }
    Self& description(std::string text) {
        description_ = std::move(text);
        return self();
    }
    /// The title and description from a docstring: the title is the first
    /// line; the description is the rest if the second line is blank, the
    /// whole text otherwise (as teta-wot reads doc comments).
    Self& doc(std::string_view text) {
        auto [t, d] = td::split_docstring(text);
        title_ = std::move(t);
        description_ = std::move(d);
        return self();
    }
    /// A semantic annotation (`@type`) shown in the Thing Description.
    Self& semantic_type(std::string type) {
        semantic_types_.push_back(std::move(type));
        return self();
    }

    const std::optional<std::string>& title_text() const { return title_; }
    const std::optional<std::string>& description_text() const { return description_; }
    const std::vector<std::string>& semantic_types() const { return semantic_types_; }

  private:
    Self& self() { return static_cast<Self&>(*this); }
    std::optional<std::string> title_;
    std::optional<std::string> description_;
    std::vector<std::string> semantic_types_;
};

/// The options of both kinds of property.
template <typename Self>
class PropertyOptions : public Described<Self> {
  public:
    /// Clients may read but not write the property.
    Self& read_only() {
        meta_.read_only = true;
        return self();
    }
    /// Opts out of the global lock for client writes.
    Self& global_lock(bool enabled) {
        meta_.global_lock = enabled;
        return self();
    }
    /// The unit of measure shown in the Thing Description.
    Self& unit(std::string unit) {
        meta_.unit = std::move(unit);
        return self();
    }
    /// Makes the property a setting (Phase 5): saved to the Thing's settings
    /// file after every change and loaded when the server starts.
    Self& setting() {
        meta_.setting = true;
        return self();
    }
    Self& ge(td::Bound value) {
        return constrain([&](td::Constraints& c) { c.ge(value); });
    }
    Self& gt(td::Bound value) {
        return constrain([&](td::Constraints& c) { c.gt(value); });
    }
    Self& le(td::Bound value) {
        return constrain([&](td::Constraints& c) { c.le(value); });
    }
    Self& lt(td::Bound value) {
        return constrain([&](td::Constraints& c) { c.lt(value); });
    }
    Self& multiple_of(td::Bound value) {
        return constrain([&](td::Constraints& c) { c.multiple_of(value); });
    }
    Self& min_length(std::uint64_t length) {
        return constrain([&](td::Constraints& c) { c.min_length(length); });
    }
    Self& max_length(std::uint64_t length) {
        return constrain([&](td::Constraints& c) { c.max_length(length); });
    }
    Self& pattern(std::string regex) {
        return constrain([&](td::Constraints& c) { c.pattern(std::move(regex)); });
    }
    Self& allow_inf_nan(bool allow) {
        return constrain([&](td::Constraints& c) { c.allow_inf_nan(allow); });
    }
    /// All the constraints at once.
    Self& constraints(td::Constraints constraints) {
        constraints_ = std::move(constraints);
        return self();
    }

    PropertyMeta meta() const {
        PropertyMeta meta = meta_;
        meta.title = this->title_text();
        meta.description = this->description_text();
        meta.semantic_types = this->semantic_types();
        return meta;
    }
    const std::optional<td::Constraints>& constraint_set() const { return constraints_; }

  private:
    Self& self() { return static_cast<Self&>(*this); }
    template <typename F>
    Self& constrain(F&& f) {
        if (!constraints_) {
            constraints_.emplace();
        }
        std::forward<F>(f)(*constraints_);
        return self();
    }
    PropertyMeta meta_;
    std::optional<td::Constraints> constraints_;
};

/// A data property's options.
class DataPropertyOptions : public PropertyOptions<DataPropertyOptions> {};

/// A functional property's options: its setter, resetter and default.
template <typename T, typename V>
class FunctionalPropertyOptions : public PropertyOptions<FunctionalPropertyOptions<T, V>> {
  public:
    /// Adds a setter, which makes the property writable by clients: a member
    /// function taking the value, or a function taking the Thing and the
    /// value.
    template <typename F>
        requires detail::FunctionOf<T, F>
    FunctionalPropertyOptions& setter(F f) {
        using Args = typename detail::ThingFunction<T, F>::args;
        static_assert(Args::size == 1, "a setter takes the property's value");
        static_assert(std::is_same_v<std::remove_cvref_t<typename detail::Front<Args>::type>, V>,
                      "a setter takes a value of the property's type (the getter's)");
        setter_ = [f](T& thing, V value) mutable { detail::invoke_on(f, thing, std::move(value)); };
        return *this;
    }
    /// Adds a resetter, which puts the property back to a default state.
    template <typename F>
        requires detail::FunctionOf<T, F>
    FunctionalPropertyOptions& resetter(F f) {
        static_assert(detail::ThingFunction<T, F>::args::size == 0, "a resetter takes nothing but the Thing");
        resetter_ = [f](T& thing) mutable { detail::invoke_on(f, thing); };
        return *this;
    }
    /// The default shown in the Thing Description. With a setter, it also
    /// makes the property resettable.
    FunctionalPropertyOptions& default_value(V value) {
        default_ = std::move(value);
        return *this;
    }

    std::function<V(T&)> getter_;
    std::function<void(T&, V)> setter_;
    std::function<void(T&)> resetter_;
    std::optional<V> default_;
};

/// An action parameter's options.
class ParamOptions {
  public:
    /// The default used when the parameter is left out, shown in the input
    /// schema (`#[param(default = …)]`).
    template <Encodable V>
    ParamOptions& default_value(const V& value) {
        default_ = encode(value);
        return *this;
    }
    /// The parameter may be left out, taking its type's default value
    /// (`#[param(default)]`).
    ParamOptions& defaulted() {
        defaulted_ = true;
        return *this;
    }
    /// The parameter's description in the input schema.
    ParamOptions& description(std::string text) {
        description_ = std::move(text);
        return *this;
    }
    ParamOptions& doc(std::string text) { return description(std::move(text)); }

    std::string name_;
    std::optional<json::Value> default_;
    bool defaulted_ = false;
    std::optional<std::string> description_;
};

/// What an action says about itself, besides its handler.
struct ActionMeta {
    std::optional<std::string> title;
    std::optional<std::string> description;
    std::chrono::nanoseconds retention = default_retention;
    bool global_lock = true;
    std::vector<std::string> semantic_types;
    bool synchronous = false;
};

/// An action's options.
class ActionOptions : public Described<ActionOptions> {
  public:
    /// How long a finished invocation is kept (300 s by default).
    ActionOptions& retention(std::chrono::nanoseconds retention) {
        retention_ = retention;
        return *this;
    }
    /// Opts out of holding the global lock while the action runs.
    ActionOptions& global_lock(bool enabled) {
        global_lock_ = enabled;
        return *this;
    }
    /// A short action whose caller waits for its output: in the `wot` wire
    /// profile, invoking it answers with the output once it has finished.
    ActionOptions& synchronous(bool synchronous = true) {
        synchronous_ = synchronous;
        return *this;
    }
    /// Accepted for parity with teta-wot's `#[action(blocking)]`: every
    /// action already runs on a worker thread.
    ActionOptions& blocking() { return *this; }
    /// A parameter's options, by name.
    ParamOptions& param(std::string_view name) {
        for (auto& p : params_) {
            if (p.name_ == name) {
                return p;
            }
        }
        params_.emplace_back();
        params_.back().name_ = std::string(name);
        return params_.back();
    }

    ActionMeta meta() const {
        return {title_text(), description_text(), retention_, global_lock_, semantic_types(), synchronous_};
    }
    const std::vector<ParamOptions>& params() const { return params_; }

  private:
    std::chrono::nanoseconds retention_ = default_retention;
    bool global_lock_ = true;
    bool synchronous_ = false;
    std::vector<ParamOptions> params_;
};

/// An event's options.
class EventOptions : public Described<EventOptions> {};

// ---- Action handlers ----------------------------------------------------------------

/// The type-erased operations of one action of one Thing (internal).
class ActionHandler {
  public:
    virtual ~ActionHandler() = default;
    virtual const td::DataSchema& input_schema() const = 0;
    virtual const td::DataSchema& output_schema() const = 0;

    /// A validated input and how to run the action with it.
    struct Prepared {
        /// The input as the invocation records it: decoded and written back,
        /// with defaults filled in.
        json::Value echo;
        /// Runs the action; returns its output as JSON.
        std::function<json::Value(ActionCtx&)> run;
        /// Runs the action; returns its output as the C++ value, in a
        /// std::any (`std::monostate` for nothing).
        std::function<std::any(ActionCtx&)> run_typed;
    };
    /// Validates a client's input (errors under `body`); throws
    /// ValidationError.
    virtual Prepared prepare(const json::Value& input) const = 0;
    /// The names of the parameters that make up the input (none for an
    /// action without, or whose whole input is one parameter).
    virtual const std::vector<std::string>& parameter_names() const = 0;
    /// Whether this action runs `callable`, of type `type` (for typed
    /// in-process calls through member function pointers).
    virtual bool runs(const std::type_info& type, const void* callable) const = 0;
};

namespace detail {

/// The input schema of named parameters: an object of them that refuses
/// others (the struct teta-wot's `#[thing_impl]` generates).
td::DataSchema parameters_schema(const std::vector<codec::Field>& fields);

template <typename T, typename F, bool Whole, FixedString... Names>
class TypedActionHandler final : public ActionHandler {
    using Function = ThingFunction<T, F>;
    using Info = ParamInfo<typename Function::args>;
    using Result = typename Function::result;
    using Output = typename OutputOf<Result>::type;
    using DataTuple = typename Info::data_tuple;

  public:
    TypedActionHandler(std::shared_ptr<T> thing, F function, const ActionOptions& options)
        : thing_(std::move(thing)), function_(std::move(function)) {
        if constexpr (sizeof...(Names) > 0) {
            names_ = {std::string(Names.view())...};
        }
        defaults_.resize(Info::data_count);
        defaulted_.resize(Info::data_count);
        std::vector<std::optional<std::string>> descriptions(Info::data_count);
        for (const auto& p : options.params()) {
            const auto it = std::ranges::find(names_, p.name_);
            if (it == names_.end()) {
                throw std::invalid_argument("`" + p.name_ + "` isn't one of the action's parameters");
            }
            const auto index = static_cast<std::size_t>(it - names_.begin());
            defaults_[index] = p.default_;
            defaulted_[index] = p.defaulted_;
            descriptions[index] = p.description_;
        }
        if constexpr (Whole) {
            input_ = for_type<std::tuple_element_t<0, DataTuple>>();
        } else if constexpr (Info::data_count == 0) {
            input_ = for_type<NoInput>();
        } else {
            std::vector<codec::Field> fields;
            add_fields(fields, descriptions, std::make_index_sequence<Info::data_count>{});
            input_ = parameters_schema(fields);
        }
        validator_.emplace(input_);
        output_ = for_type<Output>();
    }

    const td::DataSchema& input_schema() const override { return input_; }
    const td::DataSchema& output_schema() const override { return output_; }
    const std::vector<std::string>& parameter_names() const override { return names_; }

    bool runs(const std::type_info& type, const void* callable) const override {
        if constexpr (std::equality_comparable<F>) {
            return type == typeid(F) && *static_cast<const F*>(callable) == function_;
        } else {
            return false;
        }
    }

    Prepared prepare(const json::Value& input) const override {
        const Loc loc{"body"};
        json::Value coerced = validator_->validate(input, loc);
        auto data = std::make_shared<std::optional<DataTuple>>();
        json::Value echo;
        try {
            if constexpr (Whole) {
                using P = std::tuple_element_t<0, DataTuple>;
                data->emplace(Codec<P>::decode(coerced));
                echo = encode(std::get<0>(**data));
            } else if constexpr (Info::data_count == 0) {
                echo = encode(Codec<NoInput>::decode(coerced));
                data->emplace();
            } else {
                data->emplace(decode_fields(coerced, std::make_index_sequence<Info::data_count>{}));
                echo = json::Value::object();
                encode_fields(**data, echo, std::make_index_sequence<Info::data_count>{});
            }
        } catch (const DecodeError& error) {
            throw ValidationError::decode_error(loc, error.what(), std::move(coerced));
        }
        auto thing = thing_;
        auto function = function_;
        Prepared prepared;
        prepared.echo = std::move(echo);
        prepared.run_typed = [thing, function, data](ActionCtx& ctx) mutable -> std::any {
            return std::any(call(*thing, function, ctx, **data));
        };
        prepared.run = [thing, function, data](ActionCtx& ctx) mutable -> json::Value {
            return encode(call(*thing, function, ctx, **data));
        };
        return prepared;
    }

  private:
    template <std::size_t... I>
    void add_fields(std::vector<codec::Field>& fields,
                    const std::vector<std::optional<std::string>>& descriptions,
                    std::index_sequence<I...>) const {
        (add_field<I>(fields, descriptions[I]), ...);
    }

    template <std::size_t I>
    void add_field(std::vector<codec::Field>& fields, const std::optional<std::string>& description) const {
        using P = std::tuple_element_t<I, DataTuple>;
        codec::Field field;
        field.name = names_[I];
        field.schema = Codec<P>::json_schema();
        field.description = description;
        if (defaults_[I]) {
            field.default_value = defaults_[I];
        } else if (defaulted_[I]) {
            if constexpr (std::default_initializable<P>) {
                field.default_value = encode(P{});
            }
        }
        field.required = !field.default_value && !defaulted_[I] && !codec::is_optional<P>;
        fields.push_back(std::move(field));
    }

    template <std::size_t... I>
    DataTuple decode_fields(const json::Value& input, std::index_sequence<I...>) const {
        return DataTuple(decode_field<I>(input)...);
    }

    template <std::size_t I>
    std::tuple_element_t<I, DataTuple> decode_field(const json::Value& input) const {
        using P = std::tuple_element_t<I, DataTuple>;
        if (const auto it = input.find(names_[I]); it != input.end()) {
            return Codec<P>::decode(*it);
        }
        if (defaults_[I]) {
            return Codec<P>::decode(*defaults_[I]);
        }
        if constexpr (std::default_initializable<P>) {
            return P{};
        } else {
            throw DecodeError("missing field `" + names_[I] + "`");
        }
    }

    template <std::size_t... I>
    void encode_fields(const DataTuple& data, json::Value& out, std::index_sequence<I...>) const {
        ((out[names_[I]] = encode(std::get<I>(data))), ...);
    }

    template <std::size_t I>
    static decltype(auto) argument(ActionCtx& ctx, DataTuple& data) {
        using A = typename Function::args::template at<I>;
        if constexpr (param_kind<A>() == ParamKind::ctx) {
            return (ctx);
        } else if constexpr (param_kind<A>() == ParamKind::server) {
            return ctx.server();
        } else if constexpr (param_kind<A>() == ParamKind::dep) {
            using S = typename std::remove_cvref_t<A>::service_type;
            auto service = ctx.server().template service<S>();
            if (!service) {
                throw std::runtime_error("the service `" + codec::short_name(typeid(S)) +
                                         "` isn't registered with the server");
            }
            return std::remove_cvref_t<A>(std::move(service));
        } else {
            return std::move(std::get<Info::template data_index<I>>(data));
        }
    }

    static Output call(T& thing, F& function, ActionCtx& ctx, DataTuple& data) {
        return run_for_output<Result>([&]() -> decltype(auto) {
            return [&]<std::size_t... I>(std::index_sequence<I...>) -> decltype(auto) {
                return invoke_on(function, thing, argument<I>(ctx, data)...);
            }(std::make_index_sequence<Info::count>{});
        });
    }

    std::shared_ptr<T> thing_;
    F function_;
    std::vector<std::string> names_;
    std::vector<std::optional<json::Value>> defaults_;
    std::vector<bool> defaulted_;
    td::DataSchema input_;
    td::DataSchema output_;
    std::optional<SchemaValidator> validator_;
};

// ---- Property handlers --------------------------------------------------------------

template <typename T, typename V>
class DataHandler final : public PropertyHandler {
  public:
    DataHandler(std::shared_ptr<T> thing, Prop<V>& prop, td::DataSchema schema)
        : thing_(std::move(thing)), prop_(prop), schema_(std::move(schema)) {}
    json::Value read() override {
        return prop_.read([](const V& value) { return encode(value); });
    }
    void write(const json::Value& value) override { prop_.set_from_client(value); }
    void check(const json::Value& value) override { prop_.check_from_client(value); }
    bool resettable() const override { return true; }
    void reset() override { prop_.reset(); }
    std::optional<json::Value> default_json() const override { return encode(prop_.default_value()); }
    const td::DataSchema& schema() const override { return schema_; }
    bool has_setter() const override { return true; }
    bool observable() const override { return true; }

  private:
    std::shared_ptr<T> thing_;  // keeps `prop_` alive
    Prop<V>& prop_;
    td::DataSchema schema_;
};

template <typename T, typename V>
class FunctionalHandler final : public PropertyHandler {
  public:
    FunctionalHandler(std::shared_ptr<T> thing, const FunctionalPropertyOptions<T, V>& options,
                      SchemaValidator validator)
        : thing_(std::move(thing)),
          getter_(options.getter_),
          setter_(options.setter_),
          resetter_(options.resetter_),
          default_(options.default_),
          validator_(std::move(validator)) {}
    json::Value read() override { return encode(getter_(*thing_)); }
    void write(const json::Value& value) override {
        if (!setter_) {
            throw PropertyError::read_only("");
        }
        setter_(*thing_, from_client<V>(validator_, value));
    }
    void check(const json::Value& value) override { (void)from_client<V>(validator_, value); }
    bool resettable() const override { return resetter_ || (setter_ && default_); }
    void reset() override {
        if (resetter_) {
            resetter_(*thing_);
        } else if (setter_ && default_) {
            setter_(*thing_, *default_);
        } else {
            throw PropertyError::not_resettable("");
        }
    }
    std::optional<json::Value> default_json() const override {
        if (default_) {
            return encode(*default_);
        }
        return std::nullopt;
    }
    const td::DataSchema& schema() const override { return validator_.schema(); }
    bool has_setter() const override { return static_cast<bool>(setter_); }
    bool observable() const override { return false; }

  private:
    std::shared_ptr<T> thing_;
    std::function<V(T&)> getter_;
    std::function<void(T&, V)> setter_;
    std::function<void(T&)> resetter_;
    std::optional<V> default_;
    SchemaValidator validator_;
};

}  // namespace detail

// ---- The definition -----------------------------------------------------------------

/// The affordances and metadata of a Thing type, declared in its `define`.
/// Options are chained on what each declaration returns.
template <typename T>
class ThingSpec {
  public:
    /// What the runtime builds for each kind of affordance.
    struct PropertySlot {
        std::string name;
        std::function<PropertyMeta()> meta;
        std::function<bool()> default_read_only;
        std::function<std::shared_ptr<PropertyHandler>(const std::shared_ptr<T>&, const std::string& thing,
                                                       const std::string& name,
                                                       const std::shared_ptr<const RuntimeShared>&)>
            build;
    };
    struct ActionSlot {
        std::string name;
        ActionOptions* options;
        std::function<std::shared_ptr<ActionHandler>(const std::shared_ptr<T>&)> build;
    };
    struct EventSlot {
        std::string name;
        EventOptions* options;
        std::function<std::optional<td::DataSchema>(T&, const std::string& thing,
                                                    const std::shared_ptr<MessageBroker>&)>
            build;
    };
    struct DeviceSlot {
        std::string name;
        std::function<void(T&, const std::string& name)> open;
        std::function<void(T&)> close;
    };

    ThingSpec() : title_(codec::short_name(typeid(T))) {}
    ThingSpec(const ThingSpec&) = delete;
    ThingSpec& operator=(const ThingSpec&) = delete;

    /// The Thing's title (by default its type's name).
    ThingSpec& title(std::string text) {
        title_ = std::move(text);
        return *this;
    }
    ThingSpec& description(std::string text) {
        description_ = std::move(text);
        return *this;
    }
    /// The Thing's doc comment: its description.
    ThingSpec& doc(std::string text) { return description(std::move(text)); }
    /// A semantic annotation (`@type`) for the Thing.
    ThingSpec& semantic_type(std::string type) {
        semantic_types_.push_back(std::move(type));
        return *this;
    }
    /// A prefix for the TD's `@context`, for semantic types and units written
    /// as compact IRIs (`saref:LightSwitch`).
    ThingSpec& context(std::string prefix, std::string iri) {
        context_prefixes_.emplace_back(std::move(prefix), std::move(iri));
        return *this;
    }
    /// The class name in the settings file's name, `Settings-{class}.json`
    /// (by default the title).
    ThingSpec& class_name(std::string name) {
        class_name_ = std::move(name);
        return *this;
    }

    /// A data property: a Prop member of the Thing.
    template <typename C, typename V>
        requires std::is_base_of_v<C, T>
    DataPropertyOptions& property(std::string name, Prop<V> C::*member) {
        auto options = keep(std::make_unique<DataPropertyOptions>());
        properties_.push_back(
            {std::move(name), [options] { return options->meta(); }, [] { return false; },
             [options, member](
                 const std::shared_ptr<T>& thing, const std::string& thing_name,
                 const std::string& property_name,
                 const std::shared_ptr<const RuntimeShared>& shared) -> std::shared_ptr<PropertyHandler> {
                 Prop<V>& prop = (*thing).*member;
                 if (options->constraint_set()) {
                     prop.set_constraints(*options->constraint_set());
                 }
                 auto schema = prop.data_schema();
                 prop.bind({thing_name, property_name, shared->broker, {}});
                 return std::make_shared<detail::DataHandler<T, V>>(thing, prop, std::move(schema));
             }});
        return *options;
    }

    /// A functional property: its value comes from `getter`, a member
    /// function of the Thing (or a function taking the Thing). Add a setter
    /// to make it writable.
    template <typename G>
        requires detail::FunctionOf<T, G>
    auto& property(std::string name, G getter) {
        using V = std::remove_cvref_t<typename detail::ThingFunction<T, G>::result>;
        static_assert(detail::ThingFunction<T, G>::args::size == 0, "a property's getter takes nothing");
        static_assert(PropertyValue<V>,
                      "a property's value needs a codec that reads and writes it (Codec<V>)");
        auto options = keep(std::make_unique<FunctionalPropertyOptions<T, V>>());
        options->getter_ = [getter](T& thing) mutable -> V { return detail::invoke_on(getter, thing); };
        properties_.push_back(
            {std::move(name), [options] { return options->meta(); },
             [options] { return !static_cast<bool>(options->setter_); },
             [options](const std::shared_ptr<T>& thing, const std::string&, const std::string&,
                       const std::shared_ptr<const RuntimeShared>&) -> std::shared_ptr<PropertyHandler> {
                 auto validator =
                     detail::build_validator<V>(options->constraint_set().value_or(td::Constraints{}));
                 return std::make_shared<detail::FunctionalHandler<T, V>>(thing, *options,
                                                                          std::move(validator));
             }});
        return *options;
    }

    /// An action: a member function of the Thing (or a function taking the
    /// Thing first), with its data parameters named in order.
    template <detail::FixedString... Names, typename F>
        requires detail::FunctionOf<T, F>
    ActionOptions& action(std::string name, F function) {
        using Info = detail::ParamInfo<typename detail::ThingFunction<T, F>::args>;
        static_assert(sizeof...(Names) == Info::data_count,
                      "name every parameter of the action, in order, except ActionCtx&, Server and Dep<S>: "
                      "t.action<\"x\", \"y\">(\"move_to\", &Stage::move_to)");
        check_action<F>();
        return add_action<F, false, Names...>(std::move(name), std::move(function));
    }

    /// An action whose one data parameter is its whole input.
    template <typename F>
        requires detail::FunctionOf<T, F>
    ActionOptions& action(std::string name, F function, WholeInput) {
        using Info = detail::ParamInfo<typename detail::ThingFunction<T, F>::args>;
        static_assert(Info::data_count == 1, "an action with a whole input has exactly one data parameter");
        check_action<F>();
        return add_action<F, true>(std::move(name), std::move(function));
    }

    /// An event: an Event member of the Thing.
    template <typename C, typename V>
        requires std::is_base_of_v<C, T>
    EventOptions& event(std::string name, Event<V> C::*member) {
        auto options = keep(std::make_unique<EventOptions>());
        std::string event_name = name;
        events_.push_back({std::move(name), options,
                           [member, event_name](T& thing, const std::string& thing_name,
                                                const std::shared_ptr<MessageBroker>& broker) {
                               (thing.*member).bind(thing_name, event_name, broker);
                               return Event<V>::data_schema();
                           }});
        return *options;
    }

    /// A device: a Device member of the Thing, opened (as
    /// `<thing>:<name>`) before the Thing starts and closed after it stops.
    template <typename C, typename D>
        requires std::is_base_of_v<C, T>
    ThingSpec& device(std::string name, Device<D> C::*member) {
        devices_.push_back(
            {std::move(name),
             [member](T& thing, const std::string& device_name) { (thing.*member).open(device_name); },
             [member](T& thing) { (thing.*member).close(); }});
        return *this;
    }

    /// Runs when the server starts, after the Thing's devices are open: a
    /// member function (or a function of the Thing) taking a ThingCtx& or
    /// nothing. Throwing stops the server from starting.
    template <typename F>
        requires detail::FunctionOf<T, F>
    ThingSpec& on_start(F f) {
        on_start_ = hook(std::move(f));
        return *this;
    }
    /// Runs when the server stops, before the Thing's devices close.
    template <typename F>
        requires detail::FunctionOf<T, F>
    ThingSpec& on_stop(F f) {
        on_stop_ = hook(std::move(f));
        return *this;
    }
    /// How the Thing summarises its state for `Server::thing_states`.
    template <typename F>
        requires detail::FunctionOf<T, F>
    ThingSpec& thing_state(F f) {
        thing_state_ = [f](T& thing) mutable { return encode(detail::invoke_on(f, thing)); };
        return *this;
    }

    // What the runtime reads.
    const std::string& title_text() const { return title_; }
    const std::optional<std::string>& description_text() const { return description_; }
    const std::vector<std::string>& semantic_types() const { return semantic_types_; }
    const std::vector<std::pair<std::string, std::string>>& context_prefixes() const {
        return context_prefixes_;
    }
    const std::optional<std::string>& class_name_text() const { return class_name_; }
    std::vector<PropertySlot>& properties() { return properties_; }
    std::vector<ActionSlot>& actions() { return actions_; }
    std::vector<EventSlot>& events() { return events_; }
    std::vector<DeviceSlot>& devices() { return devices_; }
    const std::function<void(T&, ThingCtx&)>& start_hook() const { return on_start_; }
    const std::function<void(T&, ThingCtx&)>& stop_hook() const { return on_stop_; }
    const std::function<json::Value(T&)>& state_function() const { return thing_state_; }

  private:
    template <typename O>
    O* keep(std::unique_ptr<O> options) {
        O* raw = options.get();
        kept_.push_back(std::shared_ptr<void>(std::move(options)));
        return raw;
    }

    template <typename F>
    static void check_action() {
        using Function = detail::ThingFunction<T, F>;
        using Info = detail::ParamInfo<typename Function::args>;
        static_assert(Info::data_by_value_or_const_ref,
                      "an action's parameters are read from the request: take them by value or const&");
        static_assert(Info::data_decodable,
                      "an action parameter needs a codec that reads it (Codec<T> with decode)");
        static_assert(Encodable<typename detail::OutputOf<typename Function::result>::type>,
                      "an action's output needs a codec (Codec<T>)");
    }

    template <typename F, bool Whole, detail::FixedString... Names>
    ActionOptions& add_action(std::string name, F function) {
        auto options = keep(std::make_unique<ActionOptions>());
        actions_.push_back(
            {std::move(name), options,
             [options, function](const std::shared_ptr<T>& thing) -> std::shared_ptr<ActionHandler> {
                 return std::make_shared<detail::TypedActionHandler<T, F, Whole, Names...>>(thing, function,
                                                                                            *options);
             }});
        return *options;
    }

    template <typename F>
    static std::function<void(T&, ThingCtx&)> hook(F f) {
        using Args = typename detail::ThingFunction<T, F>::args;
        if constexpr (Args::size == 0) {
            return [f](T& thing, ThingCtx&) mutable { detail::invoke_on(f, thing); };
        } else {
            static_assert(
                Args::size == 1 &&
                    std::is_same_v<std::remove_cvref_t<typename detail::Front<Args>::type>, ThingCtx>,
                "a lifecycle hook takes a ThingCtx& or nothing");
            return [f](T& thing, ThingCtx& ctx) mutable { detail::invoke_on(f, thing, ctx); };
        }
    }

    std::string title_;
    std::optional<std::string> description_;
    std::vector<std::string> semantic_types_;
    std::vector<std::pair<std::string, std::string>> context_prefixes_;
    std::optional<std::string> class_name_;
    std::vector<PropertySlot> properties_;
    std::vector<ActionSlot> actions_;
    std::vector<EventSlot> events_;
    std::vector<DeviceSlot> devices_;
    std::function<void(T&, ThingCtx&)> on_start_;
    std::function<void(T&, ThingCtx&)> on_stop_;
    std::function<json::Value(T&)> thing_state_;
    std::vector<std::shared_ptr<void>> kept_;
};

}  // namespace teta::wot
