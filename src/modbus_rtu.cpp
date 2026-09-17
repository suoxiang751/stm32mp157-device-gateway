#include "gateway/modbus_rtu.hpp"

#include <algorithm>

#include "gateway/crc16.hpp"

namespace gateway {
namespace {

void append_u16_be(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void append_crc(std::vector<std::uint8_t>& out) {
    const std::uint16_t crc = modbus_crc16(out);
    out.push_back(static_cast<std::uint8_t>(crc & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((crc >> 8U) & 0xFFU));
}

}  // namespace

std::vector<std::uint8_t> build_read_holding_registers(
    std::uint8_t slave, std::uint16_t address, std::uint16_t count) {
    std::vector<std::uint8_t> out{slave, 0x03U};
    append_u16_be(out, address);
    append_u16_be(out, count);
    append_crc(out);
    return out;
}

std::vector<std::uint8_t> build_write_single_register(
    std::uint8_t slave, std::uint16_t address, std::uint16_t value) {
    std::vector<std::uint8_t> out{slave, 0x06U};
    append_u16_be(out, address);
    append_u16_be(out, value);
    append_crc(out);
    return out;
}

std::optional<ModbusRtuFrame> decode_rtu_frame(const std::vector<std::uint8_t>& raw) {
    if (raw.size() < 5U) {
        return std::nullopt;
    }
    const std::size_t payload_length = raw.size() - 2U;
    const std::uint16_t expected = modbus_crc16(raw.data(), payload_length);
    const std::uint16_t actual = static_cast<std::uint16_t>(raw[payload_length]) |
        static_cast<std::uint16_t>(raw[payload_length + 1U] << 8U);
    if (expected != actual) {
        return std::nullopt;
    }
    ModbusRtuFrame frame;
    frame.slave = raw[0];
    frame.function = raw[1];
    frame.exception = (frame.function & 0x80U) != 0U;
    frame.data.assign(raw.begin() + 2, raw.end() - 2);
    return frame;
}

ModbusRtuParser::ModbusRtuParser(std::size_t max_buffer)
    : max_buffer_(std::max<std::size_t>(max_buffer, 8U)) {
    buffer_.reserve(max_buffer_);
}

std::optional<std::size_t> ModbusRtuParser::expected_length() const {
    if (buffer_.size() < 2U) {
        return std::nullopt;
    }
    const std::uint8_t function = buffer_[1];
    if ((function & 0x80U) != 0U) {
        return 5U;
    }
    if (function == 0x03U || function == 0x04U) {
        if (buffer_.size() < 3U) {
            return std::nullopt;
        }
        return static_cast<std::size_t>(buffer_[2]) + 5U;
    }
    if (function == 0x06U || function == 0x10U) {
        return 8U;
    }
    return 0U;
}

std::vector<ModbusRtuFrame> ModbusRtuParser::feed(
    const std::uint8_t* data, std::size_t length) {
    std::vector<ModbusRtuFrame> frames;
    if (length > max_buffer_) {
        data += length - max_buffer_;
        dropped_bytes_ += length - max_buffer_;
        length = max_buffer_;
    }
    if (buffer_.size() + length > max_buffer_) {
        const auto drop = buffer_.size() + length - max_buffer_;
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(drop));
        dropped_bytes_ += drop;
    }
    buffer_.insert(buffer_.end(), data, data + length);

    while (buffer_.size() >= 2U) {
        const auto expected = expected_length();
        if (!expected.has_value()) {
            break;
        }
        if (*expected == 0U || *expected > max_buffer_) {
            buffer_.erase(buffer_.begin());
            ++dropped_bytes_;
            continue;
        }
        if (buffer_.size() < *expected) {
            break;
        }
        std::vector<std::uint8_t> candidate(buffer_.begin(),
                                            buffer_.begin() + static_cast<std::ptrdiff_t>(*expected));
        auto frame = decode_rtu_frame(candidate);
        if (!frame.has_value()) {
            buffer_.erase(buffer_.begin());
            ++crc_errors_;
            ++dropped_bytes_;
            continue;
        }
        frames.push_back(std::move(*frame));
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(*expected));
    }
    return frames;
}

}  // namespace gateway

