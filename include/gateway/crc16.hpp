#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gateway {

std::uint16_t modbus_crc16(const std::uint8_t* data, std::size_t length);
inline std::uint16_t modbus_crc16(const std::vector<std::uint8_t>& data) {
    return modbus_crc16(data.data(), data.size());
}

}  // namespace gateway

