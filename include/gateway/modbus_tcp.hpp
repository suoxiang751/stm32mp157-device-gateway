#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "gateway/modbus_rtu.hpp"

namespace gateway {

struct ModbusTcpAdu {
    std::uint16_t transaction_id{0};
    std::uint8_t unit_id{0};
    std::uint8_t function{0};
    std::vector<std::uint8_t> data;
};

std::optional<ModbusTcpAdu> decode_modbus_tcp(const std::vector<std::uint8_t>& raw);
std::vector<std::uint8_t> encode_modbus_tcp(const ModbusTcpAdu& adu);
std::vector<std::uint8_t> tcp_request_to_rtu(const ModbusTcpAdu& adu);
ModbusTcpAdu rtu_response_to_tcp(std::uint16_t transaction_id, const ModbusRtuFrame& frame);

}  // namespace gateway

