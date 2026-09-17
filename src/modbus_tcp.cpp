#include "gateway/modbus_tcp.hpp"

#include "gateway/crc16.hpp"

namespace gateway {
namespace {

std::uint16_t read_u16_be(const std::uint8_t* p) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8U) | p[1]);
}

void append_u16_be(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

}  // namespace

std::optional<ModbusTcpAdu> decode_modbus_tcp(const std::vector<std::uint8_t>& raw) {
    if (raw.size() < 8U) return std::nullopt;
    const std::uint16_t protocol_id = read_u16_be(raw.data() + 2);
    const std::uint16_t length = read_u16_be(raw.data() + 4);
    if (protocol_id != 0U || length < 2U || raw.size() != static_cast<std::size_t>(6U + length)) {
        return std::nullopt;
    }
    ModbusTcpAdu adu;
    adu.transaction_id = read_u16_be(raw.data());
    adu.unit_id = raw[6];
    adu.function = raw[7];
    adu.data.assign(raw.begin() + 8, raw.end());
    return adu;
}

std::vector<std::uint8_t> encode_modbus_tcp(const ModbusTcpAdu& adu) {
    std::vector<std::uint8_t> out;
    out.reserve(8U + adu.data.size());
    append_u16_be(out, adu.transaction_id);
    append_u16_be(out, 0U);
    append_u16_be(out, static_cast<std::uint16_t>(2U + adu.data.size()));
    out.push_back(adu.unit_id);
    out.push_back(adu.function);
    out.insert(out.end(), adu.data.begin(), adu.data.end());
    return out;
}

std::vector<std::uint8_t> tcp_request_to_rtu(const ModbusTcpAdu& adu) {
    std::vector<std::uint8_t> out{adu.unit_id, adu.function};
    out.insert(out.end(), adu.data.begin(), adu.data.end());
    const auto crc = modbus_crc16(out);
    out.push_back(static_cast<std::uint8_t>(crc & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((crc >> 8U) & 0xFFU));
    return out;
}

ModbusTcpAdu rtu_response_to_tcp(std::uint16_t transaction_id, const ModbusRtuFrame& frame) {
    ModbusTcpAdu adu;
    adu.transaction_id = transaction_id;
    adu.unit_id = frame.slave;
    adu.function = frame.function;
    adu.data = frame.data;
    return adu;
}

}  // namespace gateway

