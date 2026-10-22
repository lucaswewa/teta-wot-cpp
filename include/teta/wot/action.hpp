// Why an action failed (ADR-0009): a port of teta-wot-core's `ActionError`.
//
// Action code reports failures by throwing, or by returning
// `Expected<T>`. The runtime turns whatever escapes an action into an
// ActionError, which decides the invocation's end and its problem:
//
// - Cancelled ends the invocation as `cancelled`;
// - HandledError (made with `ActionError::handled`) is an anticipated
//   failure, logged without its cause chain;
// - GlobalLockBusy is the `GlobalLockBusyError` (409);
// - anything else is logged at ERROR with its type and cause chain.
//
// The problem's `title` is the short name of the exception's type, as
// teta-wot's is the Rust error type's: `DeviceError`, `ValidationError`, or a
// Thing's own `MotorJammed`. Exceptions of the standard library's types are
// plain `Error`, as an `anyhow::Error` is. An exception that isn't a
// `std::exception` is a panic (`throw "message"` is Rust's
// `panic!("message")`).

#pragma once

#include <chrono>
#include <exception>
#include <string>
#include <string_view>
#include <tl/expected.hpp>

#include "teta/wot/problem.hpp"

namespace teta::wot {

/// How long finished invocations are kept by default.
inline constexpr std::chrono::seconds default_retention{300};

/// An anticipated failure, with a message for the user: logged at ERROR
/// without a cause chain, and the invocation ends as `error`.
class HandledError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

/// Why an action failed.
class ActionError : public std::exception {
  public:
    enum class Kind { cancelled, handled, global_lock_busy, panicked, other };

    /// An ActionError of any kind, with its message, problem title and
    /// traceback (for `other`).
    ActionError(Kind kind, std::string message, std::string title, std::string traceback = {});

    /// An anticipated failure (HandledError).
    static ActionError handled(std::string message);

    /// What `error` (an exception thrown by action code) means for the
    /// invocation. An ActionError stays as it is.
    static ActionError from(std::exception_ptr error);

    Kind kind() const { return kind_; }
    bool is_cancelled() const { return kind_ == Kind::cancelled; }
    bool is_handled() const { return kind_ == Kind::handled; }
    bool is_panic() const { return kind_ == Kind::panicked; }

    /// The message, followed by the causes of nested exceptions (`a: b`).
    const char* what() const noexcept override { return message_.c_str(); }
    /// The short name of the kind of error, used as the problem's `title`.
    const std::string& title() const { return title_; }
    /// The cause chain, for the log's `traceback`.
    const std::string& traceback() const { return traceback_; }
    /// The problem details for this error.
    ProblemDetails problem() const;

  private:
    Kind kind_;
    std::string message_;
    std::string title_;
    std::string traceback_;
};

/// What an action may return in place of throwing: a value or an
/// ActionError (`tl::expected` until C++23's `std::expected`, ADR-0002).
template <typename T, typename E = ActionError>
using Expected = tl::expected<T, E>;

/// The error side of an Expected: `return tw::unexpected(ActionError::handled("..."))`.
template <typename E>
auto unexpected(E error) {
    return tl::make_unexpected(std::move(error));
}

namespace detail {

/// An exception's message followed by its nested causes, joined with `: `
/// (Rust's `{:#}`), and the traceback (Rust's `{:?}` of an anyhow chain).
std::string message_chain(const std::exception& error);
std::string traceback_of(const std::exception& error);

/// The short name of an exception's dynamic type; `Error` for the standard
/// library's.
std::string exception_title(const std::exception& error);

}  // namespace detail

}  // namespace teta::wot
