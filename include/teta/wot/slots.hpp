// Thing slots: connections between the Things of one server. A port of
// teta-wot-core's `slots`.
//
// A slot is a member of a Thing, filled in by the runtime after every Thing
// has been built, and before settings load and Things start:
//
// | Member         | Holds              | Python type hint  |
// |----------------|--------------------|-------------------|
// | `Slot<T>`      | exactly one        | `T`               |
// | `OptSlot<T>`   | one or none        | `T | None`        |
// | `SlotMap<T>`   | any number, by name| `Mapping[str, T]` |
//
// `T` is a Thing type (the slot holds a `ThingRef<T>`, for in-process calls
// with validation and locking) or an interface: any other class, which
// Things declare they provide with `t.provides<I>()` (the slot holds a
// `std::shared_ptr<I>`).
//
// Which Things fill a slot is decided by the slot's default, which the
// configuration's `thing_slots` can override: by type (every matching Thing,
// the default), by name, by names, or none.
//
// Declare a slot in `define`: `t.slot("stage", &Autofocus::stage)`, or
// `t.slot("camera", &Autofocus::camera).defaults_to("camera")`. Include
// <teta/wot.hpp> (or runtime.hpp) where slots are used.

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "teta/wot/codec.hpp"

namespace teta::wot {

class ThingHandle;

/// Which Things a slot connects to.
class SlotSelection {
  public:
    enum class Kind {
        /// Every Thing of the slot's type (the default; `...` in Python).
        by_type,
        /// The Thing with this name.
        name,
        /// The Things with these names.
        names,
        /// No Thing (`None`).
        nothing,
    };

    static SlotSelection by_type() { return SlotSelection(Kind::by_type, {}); }
    static SlotSelection name(std::string name) { return SlotSelection(Kind::name, {std::move(name)}); }
    static SlotSelection names(std::vector<std::string> names) {
        return SlotSelection(Kind::names, std::move(names));
    }
    static SlotSelection nothing() { return SlotSelection(Kind::nothing, {}); }

    Kind kind() const { return kind_; }
    /// The names, for `name` (one) and `names`.
    const std::vector<std::string>& targets() const { return names_; }
    /// As teta-wot writes it in its messages: `...`, `camera`,
    /// `['a', 'b']`, `None`.
    std::string to_string() const;

    bool operator==(const SlotSelection&) const = default;

  private:
    SlotSelection(Kind kind, std::vector<std::string> names) : kind_(kind), names_(std::move(names)) {}
    Kind kind_;
    std::vector<std::string> names_;
};

/// How many Things a slot holds.
enum class SlotKind {
    /// Exactly one.
    single,
    /// One or none.
    optional,
    /// Any number, by name.
    map,
};

namespace detail {

/// What a slot of `T` holds, and how a Thing provides it: defined in
/// runtime.hpp, as `ThingRef<T>` for a Thing type and `std::shared_ptr<T>`
/// for an interface.
template <typename T>
struct SlotTarget;

template <typename F>
struct SlotFieldTraits;

/// The Things picked for a slot, by name.
using PickedThings = std::vector<std::pair<std::string, std::shared_ptr<ThingHandle>>>;

[[noreturn]] void throw_unconnected(const std::string& target);

}  // namespace detail

/// A slot holding exactly one Thing: a `ThingRef<T>` for a Thing type, a
/// `std::shared_ptr<T>` for an interface. `slot->` reaches it once the
/// runtime has connected the slot.
template <typename T>
class Slot {
  public:
    using Handle = typename detail::SlotTarget<T>::Handle;

    /// Whether the runtime has connected the slot.
    bool connected() const { return value_.has_value(); }
    /// The connected Thing. Throws std::logic_error before the runtime
    /// connects the slot (only code that runs while the Thing is built can
    /// see it unconnected).
    const Handle& get() const {
        if (!value_) {
            detail::throw_unconnected("a slot for " + detail::SlotTarget<T>::target_name() +
                                      " has not been connected to a Thing yet");
        }
        return *value_;
    }
    const Handle& operator*() const { return get(); }
    /// The ThingRef, or the interface.
    auto operator->() const {
        if constexpr (detail::SlotTarget<T>::is_thing) {
            return &get();
        } else {
            return get().get();
        }
    }

  private:
    friend struct detail::SlotFieldTraits<Slot>;
    std::optional<Handle> value_;
};

/// A slot holding one Thing or none.
template <typename T>
class OptSlot {
  public:
    using Handle = typename detail::SlotTarget<T>::Handle;

    /// The connected Thing, or null if there is none (or the slot isn't
    /// connected yet).
    const Handle* get() const { return value_ ? &*value_ : nullptr; }
    explicit operator bool() const { return value_.has_value(); }

  private:
    friend struct detail::SlotFieldTraits<OptSlot>;
    std::optional<Handle> value_;
};

/// A slot holding any number of Things, by name, in configuration order (or
/// the configured order).
template <typename T>
class SlotMap {
  public:
    using Handle = typename detail::SlotTarget<T>::Handle;
    using Map = OrderedMap<Handle>;

    /// The connected Things. Throws std::logic_error before the runtime
    /// connects the slot.
    const Map& get() const {
        if (!value_) {
            detail::throw_unconnected("a slot for " + detail::SlotTarget<T>::target_name() +
                                      " has not been connected yet");
        }
        return *value_;
    }
    const Map& operator*() const { return get(); }
    const Map* operator->() const { return &get(); }

  private:
    friend struct detail::SlotFieldTraits<SlotMap>;
    std::optional<Map> value_;
};

/// A slot's options.
class SlotOptions {
  public:
    /// Connects, by default, to the Thing with this name.
    SlotOptions& defaults_to(std::string name) {
        default_ = SlotSelection::name(std::move(name));
        return *this;
    }
    /// Connects, by default, to the Things with these names.
    SlotOptions& defaults_to(std::vector<std::string> names) {
        default_ = SlotSelection::names(std::move(names));
        return *this;
    }
    /// Connects to nothing unless configured.
    SlotOptions& defaults_to_nothing() {
        default_ = SlotSelection::nothing();
        return *this;
    }

    const SlotSelection& default_selection() const { return default_; }

  private:
    SlotSelection default_ = SlotSelection::by_type();
};

namespace detail {

/// A slot of a running Thing, as the runtime connects it (internal).
struct SlotControl {
    SlotKind kind = SlotKind::single;
    std::string target_name;
    SlotSelection default_selection = SlotSelection::by_type();
    /// Whether a Thing can fill the slot.
    std::function<bool(const std::shared_ptr<ThingHandle>&)> accepts;
    /// Fills the slot with the picked Things, all of which it accepts.
    std::function<void(const PickedThings&)> fill;
    /// Empties the slot (when the runtime goes, so that Things referring to
    /// each other don't keep each other alive).
    std::function<void()> clear;
};

template <typename T>
struct SlotFieldTraits<Slot<T>> {
    static constexpr SlotKind kind = SlotKind::single;
    using Target = SlotTarget<T>;
    static void fill(Slot<T>& slot, const PickedThings& picked) {
        if (!picked.empty()) {
            slot.value_ = Target::resolve(picked.front().second);
        }
    }
    static void clear(Slot<T>& slot) { slot.value_.reset(); }
};

template <typename T>
struct SlotFieldTraits<OptSlot<T>> {
    static constexpr SlotKind kind = SlotKind::optional;
    using Target = SlotTarget<T>;
    static void fill(OptSlot<T>& slot, const PickedThings& picked) {
        if (!picked.empty()) {
            slot.value_ = Target::resolve(picked.front().second);
        }
    }
    static void clear(OptSlot<T>& slot) { slot.value_.reset(); }
};

template <typename T>
struct SlotFieldTraits<SlotMap<T>> {
    static constexpr SlotKind kind = SlotKind::map;
    using Target = SlotTarget<T>;
    static void fill(SlotMap<T>& slot, const PickedThings& picked) {
        typename SlotMap<T>::Map map;
        for (const auto& [name, thing] : picked) {
            if (auto handle = Target::resolve(thing)) {
                map.emplace(name, std::move(*handle));
            }
        }
        slot.value_ = std::move(map);
    }
    static void clear(SlotMap<T>& slot) { slot.value_.reset(); }
};

template <typename F>
concept SlotField = requires { SlotFieldTraits<F>::kind; };

/// The control of `thing`'s slot `member`.
template <typename T, typename F, typename C>
std::shared_ptr<SlotControl> slot_control(const std::shared_ptr<T>& thing, F C::* member,
                                          const SlotSelection& default_selection) {
    using Traits = SlotFieldTraits<F>;
    using Target = typename Traits::Target;
    auto control = std::make_shared<SlotControl>();
    control->kind = Traits::kind;
    control->target_name = Target::target_name();
    control->default_selection = default_selection;
    control->accepts = [](const std::shared_ptr<ThingHandle>& handle) {
        return Target::resolve(handle).has_value();
    };
    control->fill = [thing, member](const PickedThings& picked) { Traits::fill((*thing).*member, picked); };
    control->clear = [thing, member] { Traits::clear((*thing).*member); };
    return control;
}

}  // namespace detail

}  // namespace teta::wot
