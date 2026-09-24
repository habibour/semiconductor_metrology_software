#pragma once

// Thread-safety: register_message() is not thread-safe and is meant to be done
// once at start-up; after that check() and name() are safe to call
// concurrently (they only read).
//
// Factory pattern (PRD 6.6), mechanism only. The message catalogue itself
// (S1F1, S2F41, S6F11, ...) is Day 5's job; today's code just lets each
// (stream, function) pair register a name and a body-layout check, and lets
// the receiving side ask "is this legal?". The three failure answers are the
// three GEM situations that map to S9F3, S9F5 and S9F7 (PRD 8.6.3).
//
// An instance, not a global registry: no singletons (CLAUDE.md 6.1).

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>

#include "ssim/secsgem/secs2/item.hpp"
#include "ssim/secsgem/secs2/message.hpp"

namespace ssim::secsgem::secs2 {

enum class Validation {
    kOk,
    kUnknownStream,    // no message registered for this stream (S9F3)
    kUnknownFunction,  // stream known, function not (S9F5)
    kIllegalData,      // known message, body does not match its layout (S9F7)
};

class MessageFactory {
public:
    // Returns true if the body (or its absence) is a legal layout for the message.
    using BodyCheck = std::function<bool(const std::optional<Item>&)>;

    // Registering the same (stream, function) again replaces the entry.
    void register_message(std::uint8_t stream, std::uint8_t function, std::string name,
                          BodyCheck check);

    [[nodiscard]] Validation check(const Message& message) const;

    // Registered name, or an empty string if the pair is unknown.
    [[nodiscard]] std::string name(std::uint8_t stream, std::uint8_t function) const;

private:
    struct Entry {
        std::string name;
        BodyCheck check;
    };
    std::map<std::pair<std::uint8_t, std::uint8_t>, Entry> entries_;
};

}  // namespace ssim::secsgem::secs2
