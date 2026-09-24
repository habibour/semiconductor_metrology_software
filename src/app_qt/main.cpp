// equipment_qt: the operator panel. Qt lives only in this directory; the
// machine itself is ssim_machine's MachineRuntime, the same core the CLI uses.

#include <QApplication>
#include <QMessageBox>
#include <QString>
#include <iostream>
#include <string>

#include "main_window.hpp"
#include "ssim/core/config.hpp"

namespace {
constexpr int kExitConfigError = 2;
constexpr int kExitRuntimeFault = 3;
}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    ssim::core::Config config;
    std::string config_path;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "--config" || arg == "--out" || arg == "--rtf") && i + 1 < argc) {
            const std::string value = argv[++i];
            if (arg == "--config") config_path = value;
        } else {
            std::cerr << "usage: equipment_qt [--config PATH] [--out DIR] [--rtf X]\n";
            return kExitConfigError;
        }
    }

    if (!config_path.empty()) {
        auto loaded = ssim::core::load_config_file(config_path);
        if (!loaded) {
            std::cerr << "config error: " << loaded.error().message << "\n";
            return kExitConfigError;
        }
        for (const auto& warning : loaded.value().warnings) {
            std::cerr << "config warning: " << warning << "\n";
        }
        config = loaded.value().config;
    }
    // --out and --rtf override the file, as in equipment_cli.
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--out" && i + 1 < argc) config.output.dir = argv[i + 1];
        if (arg == "--rtf" && i + 1 < argc) config.scan.realtime_factor = std::stod(argv[i + 1]);
    }

    ssim::qt::MainWindow window;
    const QString error = window.rebuild(config);
    if (!error.isEmpty()) {
        std::cerr << "runtime fault: " << error.toStdString() << "\n";
        return kExitRuntimeFault;
    }
    window.show();
    return app.exec();
}
