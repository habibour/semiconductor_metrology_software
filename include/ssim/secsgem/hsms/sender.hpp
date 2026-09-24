#pragma once

// Thread-safety: all three methods may be called from any thread; the work
// itself happens on the HSMS I/O thread.
//
// How the layer above HSMS (the GEM service) sends things without touching a
// socket, and how it gets onto the I/O thread. The server implements it; unit
// tests use a fake, so GEM behaviour is tested without sockets.

#include <cstdint>
#include <functional>

#include "ssim/secsgem/secs2/message.hpp"

namespace ssim::secsgem::hsms {

class IMessageSender {
public:
    virtual ~IMessageSender() = default;

    // Sends a data message to the connected host. If it was sent, on_sent
    // (when given) is called on the I/O thread with the system bytes used, so
    // the caller can recognise the reply or a T3 timeout later.
    virtual void post_request(secs2::Message message,
                              std::function<void(std::uint32_t)> on_sent = {}) = 0;

    // Sends the reply to a request, echoing its system bytes.
    virtual void post_reply(std::uint32_t system_bytes, secs2::Message message) = 0;

    // Runs a task on the I/O thread. This is how a bus subscriber, which runs
    // on the controller or processing thread, hands work to code that owns
    // state on the I/O thread without taking a lock.
    virtual void post_task(std::function<void()> task) = 0;
};

}  // namespace ssim::secsgem::hsms
