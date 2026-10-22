#include "teta/wot/action.hpp"

#include <typeinfo>
#include <vector>

#include "teta/wot/cancel.hpp"
#include "teta/wot/codec.hpp"
#include "teta/wot/lock.hpp"

namespace teta::wot {

namespace detail {

namespace {

// The messages of an exception and of the exceptions nested in it.
void collect(const std::exception& error, std::vector<std::string>& out) {
    out.emplace_back(error.what());
    // Not `std::rethrow_if_nested`, which terminates when the nested
    // exception is null: clang-cl's `std::throw_with_nested` captures none.
    const auto* nested = dynamic_cast<const std::nested_exception*>(&error);
    if (!nested || !nested->nested_ptr()) {
        return;
    }
    try {
        std::rethrow_exception(nested->nested_ptr());
    } catch (const std::exception& cause) {
        collect(cause, out);
    } catch (...) {
        out.emplace_back("unknown error");
    }
}

}  // namespace

std::string message_chain(const std::exception& error) {
    std::vector<std::string> messages;
    collect(error, messages);
    std::string out;
    for (std::size_t i = 0; i < messages.size(); ++i) {
        out += (i ? ": " : "") + messages[i];
    }
    return out;
}

std::string traceback_of(const std::exception& error) {
    std::vector<std::string> messages;
    collect(error, messages);
    std::string out = messages[0];
    if (messages.size() == 2) {
        out += "\n\nCaused by:\n    " + messages[1];
    } else if (messages.size() > 2) {
        out += "\n\nCaused by:";
        for (std::size_t i = 1; i < messages.size(); ++i) {
            out += "\n    " + std::to_string(i - 1) + ": " + messages[i];
        }
    }
    return out;
}

std::string exception_title(const std::exception& error) {
    const std::type_info& type = typeid(error);
    if (type == typeid(DecodeError)) {
        return "Error";  // serde_json's error
    }
    std::string name = codec::demangled(type);
    // MSVC writes `class ` or `struct ` before a type's name.
    const auto strip_kind = [](std::string& text) {
        for (std::string_view prefix : {"class ", "struct "}) {
            if (text.starts_with(prefix)) {
                text.erase(0, prefix.size());
            }
        }
    };
    strip_kind(name);
    // `std::throw_with_nested` wraps the exception in a type of its own:
    // the title is the wrapped exception's.
    for (std::string_view wrapper :
         {"std::_Nested_exception<", "std::__nested<", "std::_With_nested_v2<", "std::_With_nested<"}) {
        if (name.starts_with(wrapper)) {
            name = name.substr(wrapper.size(), name.rfind('>') - wrapper.size());
            if (const auto comma = name.find(','); comma != std::string::npos) {
                name.erase(comma);
            }
            strip_kind(name);
            break;
        }
    }
    if (name.starts_with("std::")) {
        return "Error";  // as an anyhow::Error
    }
    if (const auto angle = name.find('<'); angle != std::string::npos) {
        name.erase(angle);
    }
    if (const auto colon = name.rfind("::"); colon != std::string::npos) {
        name.erase(0, colon + 2);
    }
    return name;
}

}  // namespace detail

ActionError::ActionError(Kind kind, std::string message, std::string title, std::string traceback)
    : kind_(kind), message_(std::move(message)), title_(std::move(title)), traceback_(std::move(traceback)) {}

ActionError ActionError::handled(std::string message) {
    return {Kind::handled, std::move(message), "HandledError"};
}

ActionError ActionError::from(std::exception_ptr error) {
    try {
        std::rethrow_exception(error);
    } catch (const ActionError& e) {
        return e;
    } catch (const Cancelled& e) {
        return {Kind::cancelled, e.what(), "Cancelled"};
    } catch (const GlobalLockBusy& e) {
        return {Kind::global_lock_busy, e.what(), "GlobalLockBusy"};
    } catch (const HandledError& e) {
        return {Kind::handled, e.what(), "HandledError"};
    } catch (const std::exception& e) {
        return {Kind::other, detail::message_chain(e), detail::exception_title(e), detail::traceback_of(e)};
    } catch (const char* text) {
        // `throw "message"` is the nearest thing to Rust's `panic!("message")`.
        const std::string message = std::string("The action panicked: ") + text;
        return {Kind::panicked, message, "Panicked", message};
    } catch (const std::string& text) {
        const std::string message = "The action panicked: " + text;
        return {Kind::panicked, message, "Panicked", message};
    } catch (...) {
        const std::string message = "The action panicked: unknown panic";
        return {Kind::panicked, message, "Panicked", message};
    }
}

ProblemDetails ActionError::problem() const {
    switch (kind_) {
        case Kind::cancelled:
            return ProblemDetails::teta_wot_things("InvocationCancelledError", message_, 500);
        case Kind::global_lock_busy:
            return ProblemDetails::teta_wot_things("GlobalLockBusyError", message_, 409);
        case Kind::handled:
            return ProblemDetails::teta_wot_things("InvocationError", message_, 500);
        case Kind::panicked:
        case Kind::other:
            break;
    }
    return ProblemDetails::untyped(title_, message_, 500);
}

}  // namespace teta::wot
