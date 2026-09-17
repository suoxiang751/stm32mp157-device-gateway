#include "gateway/gateway.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <regex>
#include <thread>
#include <utility>

#include "gateway/mqtt_client.hpp"

namespace gateway {
namespace {

std::optional<std::string> json_string(const std::string& json, const std::string& key) {
    const std::regex pattern("\\\"" + key + "\\\"\\s*:\\s*\\\"([^\\\"]+)\\\"");
    std::smatch match;
    if (!std::regex_search(json, match, pattern)) return std::nullopt;
    return match[1].str();
}

std::optional<unsigned long> json_unsigned(const std::string& json, const std::string& key) {
    const std::regex pattern("\\\"" + key + "\\\"\\s*:\\s*([0-9]+)");
    std::smatch match;
    if (!std::regex_search(json, match, pattern)) return std::nullopt;
    try {
        return std::stoul(match[1].str());
    } catch (...) {
        return std::nullopt;
    }
}

std::uint16_t read_u16_be(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[offset]) << 8U) |
                                      data[offset + 1U]);
}

}  // namespace

std::optional<ControlCommand> parse_control_command(
    const std::string& json, std::uint8_t default_slave, std::string& error) {
    const auto action = json_string(json, "action");
    if (!action || (*action != "write_single" && *action != "write_single_register")) {
        error = "action must be write_single_register";
        return std::nullopt;
    }
    const auto request_id = json_string(json, "request_id");
    const auto address = json_unsigned(json, "address");
    const auto value = json_unsigned(json, "value");
    const auto slave = json_unsigned(json, "slave");
    if (!request_id || request_id->size() > 64U || !address || !value) {
        error = "request_id, address and value are required";
        return std::nullopt;
    }
    const unsigned long slave_value = slave.value_or(default_slave);
    if (*address > 0xFFFFUL || *value > 0xFFFFUL || slave_value < 1UL || slave_value > 247UL) {
        error = "slave/address/value out of range";
        return std::nullopt;
    }
    ControlCommand command;
    command.request_id = *request_id;
    command.slave = static_cast<std::uint8_t>(slave_value);
    command.address = static_cast<std::uint16_t>(*address);
    command.value = static_cast<std::uint16_t>(*value);
    return command;
}

Gateway::Gateway(AppConfig config) : config_(std::move(config)) {}

Gateway::~Gateway() { stop(); }

bool Gateway::start(std::string& error) {
    if (running_.exchange(true)) {
        error = "gateway is already running";
        return false;
    }
    if (!serial_.open_port(config_.serial_device, config_.serial_baud,
                           config_.serial_rs485, error)) {
        running_.store(false);
        return false;
    }
    serial_thread_ = std::thread(&Gateway::serial_loop, this);
    network_thread_ = std::thread(&Gateway::network_loop, this);
    return true;
}

void Gateway::stop() {
    const bool was_running = running_.exchange(false);
    if (was_running) {
        command_queue_.close();
        event_queue_.close();
    }
    if (serial_thread_.joinable()) serial_thread_.join();
    if (network_thread_.joinable()) network_thread_.join();
    serial_.close_port();
}

bool Gateway::submit(ControlCommand command) {
    if (!running_.load()) return false;
    return command_queue_.try_push(std::move(command));
}

void Gateway::emit_event(std::string json) {
    if (!event_queue_.try_push(std::move(json))) {
        std::cerr << "event queue full; dropping non-control telemetry\n";
    }
}

void Gateway::handle_frame(const ModbusRtuFrame& frame) {
    if (frame.exception) {
        std::cerr << "Modbus exception response function=0x" << std::hex
                  << static_cast<unsigned>(frame.function) << std::dec << '\n';
        return;
    }
    if (frame.function == 0x06U && frame.data.size() == 4U) {
        const auto address = read_u16_be(frame.data, 0U);
        const auto value = read_u16_be(frame.data, 2U);
        auto confirmed = state_.confirm_write(frame.slave, address, value);
        if (confirmed) {
            emit_event(state_.result_json(*confirmed, CommandPhase::applied,
                                          "device echoed the write response"));
            emit_event(state_.snapshot_json());
        }
    }
}

void Gateway::serial_loop() {
    using namespace std::chrono_literals;
    std::array<std::uint8_t, 256> rx{};
    while (running_.load()) {
        if (auto command = command_queue_.pop_for(5ms)) {
            const auto deadline = std::chrono::steady_clock::now() +
                std::chrono::milliseconds(config_.command_timeout_ms);
            state_.mark_pending(*command, deadline);
            const auto frame = build_write_single_register(
                command->slave, command->address, command->value);
            std::string error;
            if (!serial_.write_all(frame.data(), frame.size(), error)) {
                state_.mark_failed(command->request_id);
                emit_event(state_.result_json(*command, CommandPhase::failed, error));
            } else {
                state_.mark_sent(command->request_id);
                emit_event(state_.result_json(*command, CommandPhase::sent,
                                              "bytes accepted and drained by UART"));
            }
        }

        std::string error;
        const int ready = serial_.wait_readable(20, error);
        if (ready < 0) {
            if (running_.load()) std::cerr << error << '\n';
            break;
        }
        if (ready > 0) {
            const long count = serial_.read_some(rx.data(), rx.size(), error);
            if (count < 0) {
                std::cerr << error << '\n';
                break;
            }
            if (count > 0) {
                const auto frames = parser_.feed(rx.data(), static_cast<std::size_t>(count));
                for (const auto& frame : frames) handle_frame(frame);
            }
        }

        while (auto expired = state_.expire_one(std::chrono::steady_clock::now())) {
            if (expired->attempts < config_.command_retries) {
                ++expired->attempts;
                if (!command_queue_.try_push(*expired)) {
                    emit_event(state_.result_json(*expired, CommandPhase::failed,
                                                  "retry queue full"));
                }
            } else {
                emit_event(state_.result_json(*expired, CommandPhase::timed_out,
                                              "no matching device response"));
            }
        }
    }
    running_.store(false);
}

void Gateway::network_loop() {
    using namespace std::chrono_literals;
    if (!config_.mqtt_enabled) {
        while (running_.load()) {
            if (auto event = event_queue_.pop_for(100ms)) std::cout << *event << std::endl;
        }
        return;
    }

    int reconnect_seconds = 1;
    while (running_.load()) {
        MqttClient mqtt;
        std::string error;
        if (!mqtt.connect_to(config_.mqtt_host, config_.mqtt_port,
                             config_.mqtt_client_id, config_.mqtt_keepalive_s, error)) {
            std::cerr << error << "; retrying in " << reconnect_seconds << "s\n";
            for (int i = 0; i < reconnect_seconds * 10 && running_.load(); ++i) {
                std::this_thread::sleep_for(100ms);
            }
            reconnect_seconds = std::min(reconnect_seconds * 2, 30);
            continue;
        }
        mqtt.set_message_handler([this](const std::string&, const std::string& payload) {
            std::string parse_error;
            auto command = parse_control_command(payload, config_.modbus_slave, parse_error);
            if (!command) {
                std::cerr << "rejected MQTT command: " << parse_error << '\n';
                return;
            }
            if (!submit(std::move(*command))) {
                std::cerr << "command queue full; request rejected\n";
            }
        });
        if (!mqtt.subscribe(config_.mqtt_command_topic, error)) {
            std::cerr << error << '\n';
            mqtt.disconnect();
            continue;
        }
        reconnect_seconds = 1;
        emit_event(state_.snapshot_json());

        while (running_.load() && mqtt.connected()) {
            while (auto event = event_queue_.pop_for(1ms)) {
                if (!mqtt.publish(config_.mqtt_state_topic, *event, error)) break;
            }
            if (!mqtt.poll(100, error)) {
                std::cerr << error << '\n';
                break;
            }
        }
        mqtt.disconnect();
    }
}

}  // namespace gateway
