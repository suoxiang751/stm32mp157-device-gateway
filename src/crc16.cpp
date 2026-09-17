#include "gateway/crc16.hpp"

namespace gateway {

std::uint16_t modbus_crc16(const std::uint8_t* data, std::size_t length) {
    std::uint16_t crc = 0xFFFFU;
    for (std::size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            const bool lsb = (crc & 0x0001U) != 0U;
            crc >>= 1U;
            if (lsb) {
                crc ^= 0xA001U;
            }
        }
    }
    return crc;
}

}  // namespace gateway

