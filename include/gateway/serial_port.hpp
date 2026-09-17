#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace gateway {

class SerialPort {
public:
    SerialPort() = default;
    ~SerialPort();
    SerialPort(const SerialPort&) = delete;
    SerialPort& operator=(const SerialPort&) = delete;

    bool open_port(const std::string& device, int baud, bool rs485, std::string& error);
    void close_port();
    bool is_open() const;
    int wait_readable(int timeout_ms, std::string& error);
    long read_some(std::uint8_t* dst, std::size_t capacity, std::string& error);
    bool write_all(const std::uint8_t* data, std::size_t length, std::string& error);

private:
    int fd_{-1};
};

}  // namespace gateway

