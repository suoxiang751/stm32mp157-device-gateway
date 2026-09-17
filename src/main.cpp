#include <atomic>
#include <chrono>
#include <csignal>
#include <exception>
#include <iostream>
#include <string>
#include <thread>

#include "gateway/config.hpp"
#include "gateway/gateway.hpp"

namespace {

std::atomic<bool> keep_running{true};

void handle_signal(int) { keep_running.store(false); }

void usage(const char* program) {
    std::cout << "Usage: " << program
              << " [--config path] [--once-write address value]\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string config_path = "config/gateway.conf";
    bool once = false;
    unsigned long address = 0;
    unsigned long value = 0;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            config_path = argv[++i];
        } else if (arg == "--once-write" && i + 2 < argc) {
            once = true;
            try {
                address = std::stoul(argv[++i], nullptr, 0);
                value = std::stoul(argv[++i], nullptr, 0);
            } catch (const std::exception&) {
                std::cerr << "address and value must be integers\n";
                return 2;
            }
        } else if (arg == "--help") {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    if (address > 0xFFFFUL || value > 0xFFFFUL) {
        std::cerr << "address and value must fit uint16\n";
        return 2;
    }

    try {
        const auto config = gateway::AppConfig::load(config_path);
        gateway::Gateway app(config);
        std::string error;
        if (!app.start(error)) {
            std::cerr << "startup failed: " << error << '\n';
            return 1;
        }

        std::signal(SIGINT, handle_signal);
        std::signal(SIGTERM, handle_signal);
        if (once) {
            gateway::ControlCommand command;
            command.request_id = "local-once";
            command.slave = config.modbus_slave;
            command.address = static_cast<std::uint16_t>(address);
            command.value = static_cast<std::uint16_t>(value);
            if (!app.submit(command)) {
                std::cerr << "command queue is unavailable\n";
                app.stop();
                return 1;
            }
            std::this_thread::sleep_for(
                std::chrono::milliseconds(config.command_timeout_ms + 500));
        } else {
            while (keep_running.load() && app.running()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
        }
        app.stop();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
