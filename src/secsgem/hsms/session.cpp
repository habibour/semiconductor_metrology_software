#include "ssim/secsgem/hsms/session.hpp"

#include <cmath>
#include <utility>

#include "ssim/secsgem/secs2/codec.hpp"

namespace ssim::secsgem::hsms {

namespace {

Duration seconds_to_duration(double seconds) {
    return Duration(static_cast<Duration::rep>(std::llround(seconds * 1000.0)));
}

}  // namespace

SessionConfig session_config_from(const ssim::core::CommConfig& comm) {
    SessionConfig config;
    config.t3 = seconds_to_duration(comm.t3_s);
    config.t6 = seconds_to_duration(comm.t6_s);
    config.t7 = seconds_to_duration(comm.t7_s);
    config.t8 = seconds_to_duration(comm.t8_s);
    config.linktest_interval = seconds_to_duration(comm.linktest_s);
    config.max_frame_bytes = comm.max_frame_bytes;
    config.device_id = static_cast<std::uint16_t>(comm.device_id);
    return config;
}

Session::Session(SessionConfig config)
    : config_(config),
      decoder_(config.max_frame_bytes),
      next_system_bytes_(config.first_system_bytes) {}

void Session::reset_link_state() {
    t7_.stop();
    t8_.stop();
    t6_.stop();
    pending_linktest_.reset();
    transactions_.clear();
    decoder_ = FrameDecoder(config_.max_frame_bytes);
}

SessionOutput Session::on_connect(TimePoint now) {
    SessionOutput out;
    if (!fsm_.apply(ConnectionTrigger::kConnect)) {
        return out;
    }
    reset_link_state();
    last_activity_ = now;
    t7_.start(now, config_.t7);
    return out;
}

void Session::on_disconnect() {
    if (fsm_.apply(ConnectionTrigger::kDisconnect)) {
        reset_link_state();
    }
}

void Session::close_connection(std::string reason, SessionOutput& out) {
    out.close = true;
    out.close_reason = std::move(reason);
    on_disconnect();
}

void Session::send_control(SType stype, std::uint32_t system_bytes, std::uint8_t byte2,
                           std::uint8_t byte3, SessionOutput& out) {
    // A control frame has no body, so encoding cannot fail.
    out.to_send.push_back(
        encode_frame(make_control_frame(stype, system_bytes, byte2, byte3)).value());
}

void Session::reject(const Frame& offending, std::uint8_t reason, SessionOutput& out) {
    // Byte 2 names what was rejected: its SType, or its PType if that was the
    // problem. TODO(verify) against the standard.
    const std::uint8_t what =
        reason == kRejectPTypeNotSupported ? offending.header.ptype : offending.header.stype;
    send_control(SType::kRejectReq, offending.header.system_bytes, what, reason, out);
}

SessionOutput Session::on_bytes(TimePoint now, ByteSpan bytes) {
    SessionOutput out;
    if (fsm_.state() == ConnectionState::kNotConnected) {
        return out;
    }
    last_activity_ = now;

    auto frames = decoder_.feed(bytes);
    if (!frames) {
        close_connection("framing error: " + frames.error().message, out);
        return out;
    }
    for (const Frame& frame : frames.value()) {
        handle_frame(now, frame, out);
        if (out.close) {
            return out;  // the rest of this read is discarded with the connection
        }
    }
    // T8 runs only while a frame is unfinished, and restarts whenever more of
    // it arrives (it is an inter-character timeout).
    if (decoder_.has_partial()) {
        t8_.start(now, config_.t8);
    } else {
        t8_.stop();
    }
    return out;
}

void Session::handle_frame(TimePoint now, const Frame& frame, SessionOutput& out) {
    if (frame.header.ptype != 0) {
        reject(frame, kRejectPTypeNotSupported, out);
        return;
    }
    const auto stype = stype_from_byte(frame.header.stype);
    if (!stype) {
        reject(frame, kRejectSTypeNotSupported, out);
        return;
    }
    const std::uint32_t system = frame.header.system_bytes;

    switch (*stype) {
        case SType::kData: {
            if (fsm_.state() != ConnectionState::kSelected) {
                reject(frame, kRejectEntityNotSelected, out);
                return;
            }
            Delivery delivery{frame, std::nullopt};
            // Replies have even function numbers (odd is a primary message), so
            // a host request that happens to reuse one of our system bytes is
            // not mistaken for a reply.
            if (frame.header.function() % 2 == 0 && transactions_.erase(system) > 0) {
                delivery.reply_to = system;
            }
            out.deliveries.push_back(std::move(delivery));
            return;
        }
        case SType::kSelectReq:
            if (fsm_.state() == ConnectionState::kNotSelected) {
                (void)fsm_.apply(ConnectionTrigger::kSelect);
                t7_.stop();
                send_control(SType::kSelectRsp, system, 0, kSelectOk, out);
            } else {
                send_control(SType::kSelectRsp, system, 0, kSelectAlreadyActive, out);
            }
            return;
        case SType::kDeselectReq:
            if (fsm_.state() == ConnectionState::kSelected) {
                (void)fsm_.apply(ConnectionTrigger::kDeselect);
                transactions_.clear();       // nothing can be answered any more
                t7_.start(now, config_.t7);  // TODO(verify): T7 restarts on deselect
                send_control(SType::kDeselectRsp, system, 0, kDeselectOk, out);
            } else {
                send_control(SType::kDeselectRsp, system, 0, kDeselectNotSelected, out);
            }
            return;
        case SType::kLinktestReq:
            // Answered in every connected state. TODO(verify)
            send_control(SType::kLinktestRsp, system, 0, 0, out);
            return;
        case SType::kLinktestRsp:
            if (pending_linktest_ && *pending_linktest_ == system) {
                pending_linktest_.reset();
                t6_.stop();
            } else {
                reject(frame, kRejectTransactionNotOpen, out);
            }
            return;
        case SType::kSelectRsp:
        case SType::kDeselectRsp:
            // The machine never sends Select.req or Deselect.req.
            reject(frame, kRejectTransactionNotOpen, out);
            return;
        case SType::kRejectReq:
            // The peer refused something we sent: end that transaction.
            if (pending_linktest_ && *pending_linktest_ == system) {
                pending_linktest_.reset();
                t6_.stop();
            }
            transactions_.erase(system);
            return;
        case SType::kSeparateReq:
            close_connection("Separate.req received", out);
            return;
    }
}

SessionOutput Session::on_tick(TimePoint now) {
    SessionOutput out;
    if (fsm_.state() == ConnectionState::kNotConnected) {
        return out;
    }

    if (fsm_.state() == ConnectionState::kNotSelected && t7_.expired(now)) {
        (void)fsm_.apply(ConnectionTrigger::kT7Timeout);
        out.close = true;
        out.close_reason = "T7 timeout: not selected in time";
        reset_link_state();
        return out;
    }
    if (t8_.expired(now)) {
        close_connection("T8 timeout: frame stalled", out);
        return out;
    }
    if (pending_linktest_ && t6_.expired(now)) {
        close_connection("T6 timeout: no Linktest.rsp", out);
        return out;
    }

    // T3: report every expired request once, and forget it.
    for (auto it = transactions_.begin(); it != transactions_.end();) {
        if (now >= it->second) {
            out.timed_out.push_back(it->first);
            it = transactions_.erase(it);
        } else {
            ++it;
        }
    }

    if (fsm_.state() == ConnectionState::kSelected && !pending_linktest_ &&
        now - last_activity_ >= config_.linktest_interval) {
        const std::uint32_t system = allocate_system_bytes();
        pending_linktest_ = system;
        send_control(SType::kLinktestReq, system, 0, 0, out);
        t6_.start(now, config_.t6);
        last_activity_ = now;
    }
    return out;
}

std::uint32_t Session::allocate_system_bytes() {
    // Unique among everything still open. The open set is bounded, so this
    // loop ends after at most that many steps.
    for (;;) {
        const std::uint32_t candidate = next_system_bytes_;
        ++next_system_bytes_;  // wraps naturally
        if (candidate == 0) {
            continue;  // keep 0 unused
        }
        if (transactions_.count(candidate) == 0 &&
            !(pending_linktest_ && *pending_linktest_ == candidate)) {
            return candidate;
        }
    }
}

ssim::core::Result<bool> Session::queue_data(const secs2::Message& message,
                                             std::uint32_t system_bytes, SessionOutput& out) {
    using R = ssim::core::Result<bool>;
    std::vector<std::uint8_t> body;
    if (message.body.has_value()) {
        auto encoded = secs2::encode(*message.body);
        if (!encoded) {
            return R::err(encoded.error());
        }
        body = std::move(encoded).value();
    }
    if (kHeaderSize + body.size() > config_.max_frame_bytes) {
        return R::err({kErrMessageTooLarge, "message exceeds the maximum frame size"});
    }
    const Frame frame = make_data_frame(config_.device_id, message.stream, message.function,
                                        message.w_bit, system_bytes, std::move(body));
    auto bytes = encode_frame(frame);
    if (!bytes) {
        return R::err(bytes.error());
    }
    out.to_send.push_back(std::move(bytes).value());
    return R::ok(true);
}

ssim::core::Result<std::uint32_t> Session::send_request(TimePoint now,
                                                        const secs2::Message& message,
                                                        SessionOutput& out) {
    using R = ssim::core::Result<std::uint32_t>;
    if (fsm_.state() != ConnectionState::kSelected) {
        return R::err({kErrNotSelected, "data messages can only be sent while selected"});
    }
    if (message.w_bit && transactions_.size() >= config_.max_open_transactions) {
        return R::err({kErrTooManyTransactions, "too many open transactions"});
    }
    const std::uint32_t system = allocate_system_bytes();
    auto queued = queue_data(message, system, out);
    if (!queued) {
        return R::err(queued.error());
    }
    if (message.w_bit) {
        transactions_[system] = now + config_.t3;
    }
    last_activity_ = now;
    return R::ok(system);
}

ssim::core::Result<bool> Session::send_reply(std::uint32_t system_bytes,
                                             const secs2::Message& message, SessionOutput& out) {
    if (fsm_.state() != ConnectionState::kSelected) {
        return ssim::core::Result<bool>::err(
            {kErrNotSelected, "data messages can only be sent while selected"});
    }
    secs2::Message reply = message;
    reply.w_bit = false;  // a reply never asks for another reply
    return queue_data(reply, system_bytes, out);
}

}  // namespace ssim::secsgem::hsms
