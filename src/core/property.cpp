#include "teta/wot/property.hpp"

#include "teta/wot/action.hpp"
#include "teta/wot/context.hpp"
#include "teta/wot/device.hpp"

namespace teta::wot {

PropertyError PropertyError::invalid(ValidationError error) {
    PropertyError out(Kind::invalid, error.what());
    out.validation_.emplace(std::move(error));
    return out;
}

PropertyError PropertyError::read_only(const std::string& name) {
    return {Kind::read_only, "property `" + name + "` is read-only"};
}

PropertyError PropertyError::not_resettable(const std::string& name) {
    return {Kind::not_resettable, "property `" + name + "` can't be reset"};
}

PropertyError PropertyError::definition(const std::string& message) {
    return {Kind::definition, "the property is badly defined: " + message};
}

PropertyError PropertyError::failed(const std::string& message) {
    return {Kind::failed, message};
}

PropertyError PropertyError::from(std::exception_ptr error) {
    try {
        std::rethrow_exception(error);
    } catch (const PropertyError& e) {
        return e;
    } catch (const ValidationError& e) {
        return invalid(e);
    } catch (const GlobalLockBusy& e) {
        return {Kind::global_lock_busy, e.what()};
    } catch (const Cancelled& e) {
        return {Kind::cancelled, e.what()};
    } catch (const DeviceError& e) {
        return {Kind::device, e.what()};
    } catch (const std::exception& e) {
        return failed(detail::message_chain(e));
    } catch (...) {
        return failed("unknown error");
    }
}

ProblemDetails PropertyError::problem() const {
    const char* title = "PropertyError";
    switch (kind_) {
        case Kind::global_lock_busy:
            return ProblemDetails::teta_wot_things("GlobalLockBusyError", message_, 409);
        case Kind::cancelled:
            return ProblemDetails::teta_wot_things("InvocationCancelledError", message_, 500);
        case Kind::invalid:
            title = "ValidationError";
            break;
        case Kind::read_only:
            title = "ReadOnlyPropertyError";
            break;
        case Kind::not_resettable:
            title = "FeatureNotAvailableError";
            break;
        case Kind::device:
            title = "DeviceError";
            break;
        case Kind::definition:
            title = "PropertyDefinitionError";
            break;
        case Kind::failed:
            break;
    }
    return ProblemDetails::untyped(title, message_, 500);
}

PropertyEntry::PropertyEntry(std::string name, std::string thing, std::string title,
                             std::optional<std::string> description, bool read_only, bool global_lock,
                             std::optional<std::string> unit, std::vector<std::string> semantic_types,
                             std::shared_ptr<PropertyHandler> handler,
                             std::shared_ptr<const RuntimeShared> shared)
    : name_(std::move(name)),
      thing_(std::move(thing)),
      title_(std::move(title)),
      description_(std::move(description)),
      read_only_(read_only),
      global_lock_(global_lock),
      unit_(std::move(unit)),
      semantic_types_(std::move(semantic_types)),
      handler_(std::move(handler)),
      shared_(std::move(shared)) {}

namespace {

// Runs `f`, turning whatever it throws into a PropertyError.
template <typename F>
decltype(auto) guarded(F&& f) {
    try {
        return std::forward<F>(f)();
    } catch (...) {
        throw PropertyError::from(std::current_exception());
    }
}

}  // namespace

json::Value PropertyEntry::read() const {
    return guarded([&] { return handler_->read(); });
}

std::optional<GlobalLock::Guard> PropertyEntry::lock() const {
    if (!shared_->lock || !global_lock_) {
        return std::nullopt;
    }
    const auto scope = InvocationScope::current();
    return shared_->lock->acquire(scope ? scope->lock_owner() : Uuid::v4());
}

void PropertyEntry::write(const json::Value& value) const {
    if (read_only_ || !handler_->has_setter()) {
        throw PropertyError::read_only(name_);
    }
    if (value.is_null()) {
        throw PropertyError::invalid(ValidationError::missing({"body"}, nullptr));
    }
    guarded([&] {
        const auto guard = lock();
        handler_->write(value);
    });
}

void PropertyEntry::validate(const json::Value& value) const {
    if (read_only_ || !handler_->has_setter()) {
        throw PropertyError::read_only(name_);
    }
    if (value.is_null()) {
        throw PropertyError::invalid(ValidationError::missing({"body"}, nullptr));
    }
    guarded([&] { handler_->check(value); });
}

void PropertyEntry::reset() const {
    if (read_only_) {
        throw PropertyError::read_only(name_);
    }
    if (!handler_->resettable()) {
        throw PropertyError::not_resettable(name_);
    }
    guarded([&] {
        const auto guard = lock();
        handler_->reset();
    });
}

}  // namespace teta::wot
