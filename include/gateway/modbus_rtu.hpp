#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace gateway {

struct ModbusRtuFrame {
    std::uint8_t slave{0};
    std::uint8_t function{0};
    std::vector<std::uint8_t> data;
    bool exception{false};
};

std::vector<std::uint8_t> build_read_holding_registers(
    std::uint8_t slave, std::uint16_t address, std::uint16_t count);
std::vector<std::uint8_t> build_write_single_register(
    std::uint8_t slave, std::uint16_t address, std::uint16_t value);
std::optional<ModbusRtuFrame> decode_rtu_frame(const std::vector<std::uint8_t>& raw);

class ModbusRtuParser {
public:
    explicit ModbusRtuParser(std::size_t max_buffer = 512);
    std::vector<ModbusRtuFrame> feed(const std::uint8_t* data, std::size_t length);
    std::size_t crc_errors() const { return crc_errors_; }
    std::size_t dropped_bytes() const { return dropped_bytes_; }

private:
    std::optional<std::size_t> expected_length() const;
    std::vector<std::uint8_t> buffer_;
    std::size_t max_buffer_;
    std::size_t crc_errors_{0};
    std::size_t dropped_bytes_{0};
};

}  // namespace gateway

