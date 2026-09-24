#include "ssim/secsgem/hsms/data_message.hpp"

#include "ssim/secsgem/secs2/codec.hpp"

namespace ssim::secsgem::hsms {

ssim::core::Result<secs2::Message> to_message(const Frame& frame) {
    using R = ssim::core::Result<secs2::Message>;
    if (frame.header.stype != static_cast<std::uint8_t>(SType::kData)) {
        return R::err({kErrNotADataFrame, "frame is not a data message"});
    }
    secs2::Message message;
    message.stream = frame.header.stream();
    message.function = frame.header.function();
    message.w_bit = frame.header.w_bit();
    if (!frame.body.empty()) {
        auto body = secs2::decode_body(ByteSpan(frame.body));
        if (!body) {
            return R::err(body.error());
        }
        message.body = std::move(body).value();
    }
    return R::ok(std::move(message));
}

}  // namespace ssim::secsgem::hsms
