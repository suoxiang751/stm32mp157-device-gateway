#pragma once

#include <cstdint>
#include <string>

namespace gateway {

struct AppConfig {
    std::string serial_device{"/dev/ttySTM1"};
    int serial_baud{115200};
    bool serial_rs485{true};
    std::uint8_t modbus_slave{1};
    int command_timeout_ms{1200};
    int command_retries{1};

    bool mqtt_enabled{false};
    std::string mqtt_host{"127.0.0.1"};
    std::uint16_t mqtt_port{1883};
    std::string mqtt_client_id{"stm32mp157-gateway"};
    std::string mqtt_command_topic{"device/demo/command"};
    std::string mqtt_state_topic{"device/demo/state"};
    std::uint16_t mqtt_keepalive_s{30};

    static AppConfig load(const std::string& path);
};

}  // namespace gateway

