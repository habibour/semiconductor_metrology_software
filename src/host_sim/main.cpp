// host_sim: a scripted stand-in for the factory host (FR-HOST-1/2). It runs a
// script against a machine, prints every message it sends and receives in
// decoded form, and exits non-zero if any expectation fails, so CI can use it.
//
//   host_sim --script FILE [--connect HOST:PORT] [--var NAME=VALUE]... [--device N] [--quiet]
//
// --connect provides $HOST and $PORT to the script. Exit codes: 0 every step
// passed, 1 a step failed, 2 bad arguments or a script that does not parse.

#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

#include "ssim/host_sim/runner.hpp"
#include "ssim/host_sim/script.hpp"

namespace {

int usage() {
    std::cerr << "usage: host_sim --script FILE [--connect HOST:PORT] [--var NAME=VALUE]... "
                 "[--device N] [--quiet]\n";
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    std::string script_path;
    std::map<std::string, std::string> vars;
    ssim::host_sim::RunOptions options;
    bool quiet = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
        if (arg == "--script") {
            const char* v = next();
            if (!v) return usage();
            script_path = v;
        } else if (arg == "--connect") {
            const char* v = next();
            if (!v) return usage();
            const std::string hp = v;
            const auto colon = hp.rfind(':');
            if (colon == std::string::npos) return usage();
            vars["HOST"] = hp.substr(0, colon);
            vars["PORT"] = hp.substr(colon + 1);
        } else if (arg == "--var") {
            const char* v = next();
            if (!v) return usage();
            const std::string kv = v;
            const auto eq = kv.find('=');
            if (eq == std::string::npos) return usage();
            vars[kv.substr(0, eq)] = kv.substr(eq + 1);
        } else if (arg == "--device") {
            const char* v = next();
            if (!v) return usage();
            options.device_id = static_cast<std::uint16_t>(std::stoi(v));
        } else if (arg == "--quiet") {
            quiet = true;
        } else {
            return usage();
        }
    }
    if (script_path.empty()) {
        return usage();
    }

    std::ifstream file(script_path);
    if (!file) {
        std::cerr << "cannot read " << script_path << "\n";
        return 2;
    }
    std::stringstream text;
    text << file.rdbuf();

    auto commands = ssim::host_sim::parse_script(text.str(), vars);
    if (!commands) {
        std::cerr << script_path << ": " << commands.error().message << "\n";
        return 2;
    }

    if (!quiet) {
        options.log = [](const std::string& line) { std::cout << line << std::endl; };
    }
    const ssim::host_sim::RunResult result = ssim::host_sim::run_script(commands.value(), options);
    if (!result.ok) {
        std::cerr << "FAIL " << script_path << ":" << result.failed_line << ": " << result.message
                  << "\n";
        return 1;
    }
    std::cout << "PASS " << script_path << "\n";
    return 0;
}
