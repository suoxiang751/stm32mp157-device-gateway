#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "gateway/crc16.hpp"
#include "gateway/device_state.hpp"
#include "gateway/gateway.hpp"
#include "gateway/modbus_rtu.hpp"
#include "gateway/modbus_tcp.hpp"

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void append_crc(std::vector<std::uint8_t>& frame) {
    const auto crc = gateway::modbus_crc16(frame);
    frame.push_back(static_cast<std::uint8_t>(crc & 0xFFU));
    frame.push_back(static_cast<std::uint8_t>((crc >> 8U) & 0xFFU));
}

void test_crc_and_builders() {
    const std::vector<std::uint8_t> body{0x01, 0x03, 0x00, 0x00, 0x00, 0x0A};
    expect(gateway::modbus_crc16(body) == 0xCDC5U, "known Modbus CRC vector");
    const auto request = gateway::build_read_holding_registers(1, 0, 10);
    expect(request == std::vector<std::uint8_t>({0x01, 0x03, 0x00, 0x00,
                                                 0x00, 0x0A, 0xC5, 0xCD}),
           "read request wire order");
}

void test_split_and_concatenated_frames() {
    std::vector<std::uint8_t> first{0x01, 0x03, 0x04, 0x00, 0x0A, 0x00, 0x14};
    append_crc(first);
    std::vector<std::uint8_t> second{0x01, 0x06, 0x00, 0x10, 0x00, 0x01};
    append_crc(second);

    gateway::ModbusRtuParser parser;
    auto frames = parser.feed(first.data(), 3);
    expect(frames.empty(), "split frame remains buffered");
    std::vector<std::uint8_t> tail(first.begin() + 3, first.end());
    tail.insert(tail.end(), second.begin(), second.end());
    frames = parser.feed(tail.data(), tail.size());
    expect(frames.size() == 2U, "split plus concatenated frames decode twice");
    expect(frames[0].function == 0x03U && frames[0].data.size() == 5U,
           "read response decoded");
    expect(frames[1].function == 0x06U && frames[1].data[1] == 0x10U,
           "write response decoded");
}

void test_crc_recovery() {
    std::vector<std::uint8_t> frame{0x01, 0x06, 0x00, 0x10, 0x00, 0x01};
    append_crc(frame);
    auto bad = frame;
    bad.back() ^= 0x55U;
    bad.insert(bad.end(), frame.begin(), frame.end());
    gateway::ModbusRtuParser parser;
    const auto frames = parser.feed(bad.data(), bad.size());
    expect(parser.crc_errors() > 0U, "CRC error counted");
    expect(!frames.empty(), "parser resynchronizes after corrupt frame");
}

void test_modbus_tcp_bridge() {
    gateway::ModbusTcpAdu adu;
    adu.transaction_id = 1;
    adu.unit_id = 1;
    adu.function = 3;
    adu.data = {0x00, 0x00, 0x00, 0x02};
    const auto raw = gateway::encode_modbus_tcp(adu);
    expect(raw == std::vector<std::uint8_t>({0x00, 0x01, 0x00, 0x00, 0x00,
                                             0x06, 0x01, 0x03, 0x00, 0x00,
                                             0x00, 0x02}),
           "MBAP length and PDU layout");
    const auto decoded = gateway::decode_modbus_tcp(raw);
    expect(decoded && decoded->transaction_id == 1U && decoded->data.size() == 4U,
           "Modbus TCP round trip");
    const auto rtu = gateway::tcp_request_to_rtu(*decoded);
    expect(rtu.size() == 8U && gateway::decode_rtu_frame(rtu).has_value(),
           "TCP PDU converts to CRC-protected RTU");
}

void test_command_validation() {
    std::string error;
    const auto valid = gateway::parse_control_command(
        R"({"request_id":"abc-1","action":"write_single_register","address":16,"value":1})",
        1, error);
    expect(valid && valid->slave == 1U && valid->address == 16U && valid->value == 1U,
           "valid cloud command parsed");
    error.clear();
    const auto invalid = gateway::parse_control_command(
        R"({"request_id":"abc-2","action":"write_single_register","address":70000,"value":1})",
        1, error);
    expect(!invalid && !error.empty(), "out-of-range command rejected");
}

void test_state_confirmation_and_timeout() {
    gateway::DeviceState state;
    gateway::ControlCommand command{"req-1", 1, 16, 1, 0};
    state.mark_pending(command, std::chrono::steady_clock::now() + std::chrono::seconds(1));
    state.mark_sent(command.request_id);
    const auto confirmed = state.confirm_write(1, 16, 1);
    expect(confirmed && confirmed->request_id == "req-1", "matching response confirms command");
    expect(state.snapshot_json().find("\"16\":1") != std::string::npos,
           "confirmed value becomes reported state");

    gateway::ControlCommand stale{"req-2", 1, 17, 2, 0};
    state.mark_pending(stale, std::chrono::steady_clock::now() - std::chrono::milliseconds(1));
    const auto expired = state.expire_one(std::chrono::steady_clock::now());
    expect(expired && expired->request_id == "req-2", "expired command is removed");
}

}  // namespace

int main() {
    test_crc_and_builders();
    test_split_and_concatenated_frames();
    test_crc_recovery();
    test_modbus_tcp_bridge();
    test_command_validation();
    test_state_confirmation_and_timeout();
    if (failures == 0) {
        std::cout << "All gateway tests passed\n";
        return EXIT_SUCCESS;
    }
    std::cerr << failures << " test(s) failed\n";
    return EXIT_FAILURE;
}

