#include "gateway/serial_port.hpp"

#include <cerrno>
#include <cstring>

#ifndef _WIN32
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#ifdef __linux__
#include <linux/serial.h>
#endif
#endif

namespace gateway {

SerialPort::~SerialPort() { close_port(); }

#ifndef _WIN32
namespace {

speed_t baud_constant(int baud) {
    switch (baud) {
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
#ifdef B230400
        case 230400: return B230400;
#endif
#ifdef B460800
        case 460800: return B460800;
#endif
#ifdef B921600
        case 921600: return B921600;
#endif
        default: return 0;
    }
}

}  // namespace
#endif

bool SerialPort::open_port(const std::string& device, int baud, bool rs485,
                           std::string& error) {
#ifdef _WIN32
    (void)device;
    (void)baud;
    (void)rs485;
    error = "the production serial transport targets Linux; run it in WSL/Linux or on STM32MP157";
    return false;
#else
    close_port();
    const speed_t speed = baud_constant(baud);
    if (speed == 0) {
        error = "unsupported baud rate: " + std::to_string(baud);
        return false;
    }
    fd_ = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd_ < 0) {
        error = "open " + device + ": " + std::strerror(errno);
        return false;
    }

    termios tty{};
    if (::tcgetattr(fd_, &tty) != 0) {
        error = "tcgetattr: " + std::string(std::strerror(errno));
        close_port();
        return false;
    }
    ::cfmakeraw(&tty);
    ::cfsetispeed(&tty, speed);
    ::cfsetospeed(&tty, speed);
    tty.c_cflag |= static_cast<tcflag_t>(CLOCAL | CREAD);
    tty.c_cflag &= static_cast<tcflag_t>(~CSTOPB);
    tty.c_cflag &= static_cast<tcflag_t>(~PARENB);
    tty.c_cflag &= static_cast<tcflag_t>(~CSIZE);
    tty.c_cflag |= CS8;
#ifdef CRTSCTS
    tty.c_cflag &= static_cast<tcflag_t>(~CRTSCTS);
#endif
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;
    if (::tcsetattr(fd_, TCSANOW, &tty) != 0) {
        error = "tcsetattr: " + std::string(std::strerror(errno));
        close_port();
        return false;
    }
    ::tcflush(fd_, TCIOFLUSH);

#if defined(__linux__) && defined(TIOCSRS485)
    if (rs485) {
        serial_rs485 options{};
        options.flags = SER_RS485_ENABLED | SER_RS485_RTS_ON_SEND;
        options.flags &= ~SER_RS485_RTS_AFTER_SEND;
        if (::ioctl(fd_, TIOCSRS485, &options) != 0) {
            error = "TIOCSRS485: " + std::string(std::strerror(errno));
            close_port();
            return false;
        }
    }
#else
    if (rs485) {
        error = "kernel headers do not expose TIOCSRS485";
        close_port();
        return false;
    }
#endif
    return true;
#endif
}

void SerialPort::close_port() {
#ifndef _WIN32
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
#else
    fd_ = -1;
#endif
}

bool SerialPort::is_open() const { return fd_ >= 0; }

int SerialPort::wait_readable(int timeout_ms, std::string& error) {
#ifdef _WIN32
    (void)timeout_ms;
    error = "serial transport unavailable on Windows";
    return -1;
#else
    pollfd item{};
    item.fd = fd_;
    item.events = POLLIN;
    const int result = ::poll(&item, 1, timeout_ms);
    if (result < 0 && errno != EINTR) {
        error = "poll: " + std::string(std::strerror(errno));
        return -1;
    }
    if (result <= 0) return 0;
    if ((item.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        error = "serial poll reported device error";
        return -1;
    }
    return (item.revents & POLLIN) != 0 ? 1 : 0;
#endif
}

long SerialPort::read_some(std::uint8_t* dst, std::size_t capacity, std::string& error) {
#ifdef _WIN32
    (void)dst;
    (void)capacity;
    error = "serial transport unavailable on Windows";
    return -1;
#else
    const ssize_t count = ::read(fd_, dst, capacity);
    if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
        return 0;
    }
    if (count < 0) {
        error = "read: " + std::string(std::strerror(errno));
        return -1;
    }
    return static_cast<long>(count);
#endif
}

bool SerialPort::write_all(const std::uint8_t* data, std::size_t length,
                           std::string& error) {
#ifdef _WIN32
    (void)data;
    (void)length;
    error = "serial transport unavailable on Windows";
    return false;
#else
    std::size_t offset = 0;
    while (offset < length) {
        const ssize_t count = ::write(fd_, data + offset, length - offset);
        if (count > 0) {
            offset += static_cast<std::size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR) continue;
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            pollfd item{};
            item.fd = fd_;
            item.events = POLLOUT;
            if (::poll(&item, 1, 500) > 0) continue;
        }
        error = "write: " + std::string(std::strerror(errno));
        return false;
    }
    if (::tcdrain(fd_) != 0) {
        error = "tcdrain: " + std::string(std::strerror(errno));
        return false;
    }
    return true;
#endif
}

}  // namespace gateway

