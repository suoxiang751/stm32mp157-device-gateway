#include "gateway/config.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <string>

namespace gateway {
namespace {

std::string trim(std::string value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) {
        return std::isspace(c) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) {
        return std::isspace(c) != 0;
    }).base();
    if (first >= last) {
        return {};
    }
    return std::string(first, last);
}

bool parse_bool(const std::string& value) {
    return value == "1" || value == "true" || value == "yes" || value == "on";
}

int parse_int(const std::string& key, const std::string& value, int min, int max) {
    std::size_t used = 0;
    const int parsed = std::stoi(value, &used, 10);
    if (used != value.size() || parsed < min || parsed > max) {
        throw std::runtime_error("invalid value for " + key + ": " + value);
    }
    return parsed;
}

}  // namespace

AppConfig AppConfig::load(const std::string& path) {
    AppConfig cfg;
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot open config: " + path);
    }

    std::string line;
    int line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        line = trim(line);
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const auto equal = line.find('=');
        if (equal == std::string::npos) {
            throw std::runtime_error("config line " + std::to_string(line_number) +
                                     " has no '='");
        }
        const std::string key = trim(line.substr(0, equal));
        const std::string value = trim(line.substr(equal + 1));

        if (key == "serial.device") cfg.serial_device = value;
        else if (key == "serial.baud") cfg.serial_baud = parse_int(key, value, 1200, 4000000);
        else if (key == "serial.rs485") cfg.serial_rs485 = parse_bool(value);
        else if (key == "modbus.slave") cfg.modbus_slave = static_cast<std::uint8_t>(parse_int(key, value, 1, 247));
        else if (key == "command.timeout_ms") cfg.command_timeout_ms = parse_int(key, value, 100, 60000);
        else if (key == "command.retries") cfg.command_retries = parse_int(key, value, 0, 5);
        else if (key == "mqtt.enabled") cfg.mqtt_enabled = parse_bool(value);
        else if (key == "mqtt.host") cfg.mqtt_host = value;
        else if (key == "mqtt.port") cfg.mqtt_port = static_cast<std::uint16_t>(parse_int(key, value, 1, 65535));
        else if (key == "mqtt.client_id") cfg.mqtt_client_id = value;
        else if (key == "mqtt.command_topic") cfg.mqtt_command_topic = value;
        else if (key == "mqtt.state_topic") cfg.mqtt_state_topic = value;
        else if (key == "mqtt.keepalive_s") cfg.mqtt_keepalive_s = static_cast<std::uint16_t>(parse_int(key, value, 5, 3600));
        else throw std::runtime_error("unknown config key: " + key);
    }
    return cfg;
}

}  // namespace gateway

