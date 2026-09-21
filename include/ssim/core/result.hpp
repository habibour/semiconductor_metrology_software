#pragma once

// Thread-safety: Result<T> and Error are plain value types; safe to copy or
// move across threads, not safe to mutate the same instance concurrently.

#include <string>
#include <utility>
#include <variant>

namespace ssim::core {

// A stable reason code plus a human-readable message for an expected
// failure (bad config, bad frame, refused command, hardware fault). See
// CLAUDE.md §6.3: expected failures are values, exceptions are reserved for
// programming errors.
struct Error {
    int code;
    std::string message;
};

// Holds either a T (success) or an Error (expected failure). Never throws;
// callers must check has_value() (or the explicit bool conversion) before
// calling value().
template <typename T>
class [[nodiscard]] Result {
public:
    static Result ok(T value) { return Result(std::move(value)); }
    static Result err(Error error) { return Result(std::move(error)); }

    bool has_value() const { return std::holds_alternative<T>(data_); }
    explicit operator bool() const { return has_value(); }

    const T& value() const& { return std::get<T>(data_); }
    T& value() & { return std::get<T>(data_); }
    T value() && { return std::get<T>(std::move(data_)); }

    const Error& error() const& { return std::get<Error>(data_); }

private:
    explicit Result(T value) : data_(std::move(value)) {}
    explicit Result(Error error) : data_(std::move(error)) {}

    std::variant<T, Error> data_;
};

}  // namespace ssim::core
