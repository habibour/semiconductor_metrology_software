#include "ssim/secsgem/secs2/message_factory.hpp"

namespace ssim::secsgem::secs2 {

void MessageFactory::register_message(std::uint8_t stream, std::uint8_t function, std::string name,
                                      BodyCheck check) {
    entries_[{stream, function}] = Entry{std::move(name), std::move(check)};
}

Validation MessageFactory::check(const Message& message) const {
    const auto it = entries_.find({message.stream, message.function});
    if (it != entries_.end()) {
        const BodyCheck& body_check = it->second.check;
        return (!body_check || body_check(message.body)) ? Validation::kOk
                                                         : Validation::kIllegalData;
    }
    // Not found: was the stream known at all?
    const auto first_of_stream = entries_.lower_bound({message.stream, std::uint8_t{0}});
    const bool stream_known =
        first_of_stream != entries_.end() && first_of_stream->first.first == message.stream;
    return stream_known ? Validation::kUnknownFunction : Validation::kUnknownStream;
}

std::string MessageFactory::name(std::uint8_t stream, std::uint8_t function) const {
    const auto it = entries_.find({stream, function});
    return it == entries_.end() ? std::string() : it->second.name;
}

}  // namespace ssim::secsgem::secs2
