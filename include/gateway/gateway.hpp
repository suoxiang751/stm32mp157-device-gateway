#pragma once

#include <atomic>
#include <string>
#include <thread>

#include "gateway/blocking_queue.hpp"
#include "gateway/config.hpp"
#include "gateway/device_state.hpp"
#include "gateway/modbus_rtu.hpp"
#include "gateway/serial_port.hpp"

namespace gateway {

class Gateway {
public:
    explicit Gateway(AppConfig config);
    ~Gateway();
    Gateway(const Gateway&) = delete;
    Gateway& operator=(const Gateway&) = delete;

    bool start(std::string& error);
    void stop();
    bool submit(ControlCommand command);
    bool running() const { return running_.load(); }

private:
    void serial_loop();
    void network_loop();
    void handle_frame(const ModbusRtuFrame& frame);
    void emit_event(std::string json);

    AppConfig config_;
    SerialPort serial_;
    DeviceState state_;
    ModbusRtuParser parser_;
    BlockingQueue<ControlCommand> command_queue_{32};
    BlockingQueue<std::string> event_queue_{64};
    std::atomic<bool> running_{false};
    std::thread serial_thread_;
    std::thread network_thread_;
};

std::optional<ControlCommand> parse_control_command(
    const std::string& json, std::uint8_t default_slave, std::string& error);

}  // namespace gateway

