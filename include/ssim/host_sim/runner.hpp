#pragma once

// Thread-safety: Not thread-safe; run_script() is synchronous and blocks the
// calling thread until the script ends or a step fails.
//
// FR-HOST-1/2. Runs a parsed script against a machine. Every message sent and
// received is logged in decoded form. The first failed expectation stops the
// run and is reported with its line number, so a CI job can use the exit code.
//
// Received data messages go into a queue; expect, wait-event and wait-alarm
// take the first queued message that matches and leave the others, so events
// arriving between a command and its reply cannot disturb a script. With
// auto-ack on (the default) every S6F11 and S5F1 that asks for a reply is
// answered at once, as a real host would.

#include <functional>
#include <string>
#include <vector>

#include "ssim/host_sim/script.hpp"

namespace ssim::host_sim {

struct RunOptions {
    std::uint16_t device_id = 0;                  // session id used in data messages
    std::function<void(const std::string&)> log;  // every line of the trace; may be empty
};

struct RunResult {
    bool ok = false;
    int failed_line = 0;  // 0 when ok
    std::string message;  // why the run failed
};

RunResult run_script(const std::vector<Command>& commands, const RunOptions& options);

}  // namespace ssim::host_sim
