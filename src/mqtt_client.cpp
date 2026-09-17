#include "gateway/mqtt_client.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#endif

namespace gateway {
namespace {

void close_socket(SocketHandle socket) {
    if (socket == kInvalidSocket) return;
#ifdef _WIN32
    closesocket(socket);
#else
    ::close(socket);
#endif
}

int socket_error() {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

bool wait_readable(SocketHandle socket, int timeout_ms) {
    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(socket, &read_set);
    timeval timeout{};
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
#ifdef _WIN32
    return ::select(0, &read_set, nullptr, nullptr, &timeout) > 0;
#else
    return ::select(socket + 1, &read_set, nullptr, nullptr, &timeout) > 0;
#endif
}

bool send_all(SocketHandle socket, const std::uint8_t* data, std::size_t length,
              std::string& error) {
    std::size_t offset = 0;
    while (offset < length) {
        const int chunk = static_cast<int>(length - offset);
        const int sent = ::send(socket,
                                reinterpret_cast<const char*>(data + offset),
                                chunk, 0);
        if (sent <= 0) {
            error = "mqtt send failed: " + std::to_string(socket_error());
            return false;
        }
        offset += static_cast<std::size_t>(sent);
    }
    return true;
}

bool recv_exact(SocketHandle socket, std::uint8_t* data, std::size_t length,
                std::string& error) {
    std::size_t offset = 0;
    while (offset < length) {
        if (!wait_readable(socket, 2000)) {
            error = "mqtt receive timeout";
            return false;
        }
        const int chunk = static_cast<int>(length - offset);
        const int count = ::recv(socket, reinterpret_cast<char*>(data + offset), chunk, 0);
        if (count <= 0) {
            error = count == 0 ? "mqtt peer closed" :
                "mqtt receive failed: " + std::to_string(socket_error());
            return false;
        }
        offset += static_cast<std::size_t>(count);
    }
    return true;
}

void append_u16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void append_string(std::vector<std::uint8_t>& out, const std::string& value) {
    append_u16(out, static_cast<std::uint16_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}

std::vector<std::uint8_t> make_packet(std::uint8_t header,
                                      const std::vector<std::uint8_t>& body) {
    std::vector<std::uint8_t> packet;
    packet.push_back(header);
    std::size_t remaining = body.size();
    do {
        std::uint8_t encoded = static_cast<std::uint8_t>(remaining % 128U);
        remaining /= 128U;
        if (remaining > 0U) encoded |= 0x80U;
        packet.push_back(encoded);
    } while (remaining > 0U);
    packet.insert(packet.end(), body.begin(), body.end());
    return packet;
}

}  // namespace

struct MqttClient::Impl {
    SocketHandle socket{kInvalidSocket};
    bool is_connected{false};
    std::uint16_t packet_id{1};
    std::uint16_t keepalive_s{30};
    std::chrono::steady_clock::time_point last_io{std::chrono::steady_clock::now()};
    MessageHandler handler;

    bool read_packet(int timeout_ms, std::uint8_t& header,
                     std::vector<std::uint8_t>& body, std::string& error) {
        if (!wait_readable(socket, timeout_ms)) return true;
        if (!recv_exact(socket, &header, 1U, error)) return false;

        std::size_t multiplier = 1U;
        std::size_t remaining = 0U;
        for (int i = 0; i < 4; ++i) {
            std::uint8_t byte = 0;
            if (!recv_exact(socket, &byte, 1U, error)) return false;
            remaining += static_cast<std::size_t>(byte & 0x7FU) * multiplier;
            if ((byte & 0x80U) == 0U) break;
            multiplier *= 128U;
            if (i == 3) {
                error = "invalid MQTT remaining length";
                return false;
            }
        }
        if (remaining > 1024U * 1024U) {
            error = "MQTT packet exceeds 1 MiB safety limit";
            return false;
        }
        body.resize(remaining);
        if (remaining > 0U && !recv_exact(socket, body.data(), remaining, error)) return false;
        last_io = std::chrono::steady_clock::now();
        return true;
    }
};

MqttClient::MqttClient() : impl_(new Impl) {
#ifdef _WIN32
    WSADATA data{};
    WSAStartup(MAKEWORD(2, 2), &data);
#endif
}

MqttClient::~MqttClient() {
    disconnect();
    delete impl_;
#ifdef _WIN32
    WSACleanup();
#endif
}

bool MqttClient::connect_to(const std::string& host, std::uint16_t port,
                            const std::string& client_id, std::uint16_t keepalive_s,
                            std::string& error) {
    disconnect();
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* result = nullptr;
    const std::string service = std::to_string(port);
    const int lookup = ::getaddrinfo(host.c_str(), service.c_str(), &hints, &result);
    if (lookup != 0) {
        error = "getaddrinfo failed: " + std::to_string(lookup);
        return false;
    }
    for (addrinfo* it = result; it != nullptr; it = it->ai_next) {
        SocketHandle candidate = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (candidate == kInvalidSocket) continue;
        if (::connect(candidate, it->ai_addr, static_cast<int>(it->ai_addrlen)) == 0) {
            impl_->socket = candidate;
            break;
        }
        close_socket(candidate);
    }
    ::freeaddrinfo(result);
    if (impl_->socket == kInvalidSocket) {
        error = "cannot connect to MQTT broker " + host + ":" + service;
        return false;
    }

    std::vector<std::uint8_t> body;
    append_string(body, "MQTT");
    body.push_back(0x04U);
    body.push_back(0x02U);
    append_u16(body, keepalive_s);
    append_string(body, client_id);
    const auto packet = make_packet(0x10U, body);
    if (!send_all(impl_->socket, packet.data(), packet.size(), error)) {
        disconnect();
        return false;
    }

    std::uint8_t header = 0;
    std::vector<std::uint8_t> reply;
    if (!impl_->read_packet(3000, header, reply, error) || header != 0x20U ||
        reply.size() != 2U || reply[1] != 0U) {
        if (error.empty()) error = "broker rejected MQTT CONNECT";
        disconnect();
        return false;
    }
    impl_->keepalive_s = keepalive_s;
    impl_->is_connected = true;
    impl_->last_io = std::chrono::steady_clock::now();
    return true;
}

bool MqttClient::subscribe(const std::string& topic, std::string& error) {
    if (!connected()) {
        error = "MQTT is not connected";
        return false;
    }
    std::vector<std::uint8_t> body;
    const std::uint16_t id = impl_->packet_id++;
    append_u16(body, id);
    append_string(body, topic);
    body.push_back(0x00U);
    const auto packet = make_packet(0x82U, body);
    if (!send_all(impl_->socket, packet.data(), packet.size(), error)) return false;
    impl_->last_io = std::chrono::steady_clock::now();
    std::uint8_t header = 0;
    std::vector<std::uint8_t> reply;
    if (!impl_->read_packet(3000, header, reply, error) || header != 0x90U ||
        reply.size() < 3U || reply[0] != static_cast<std::uint8_t>(id >> 8U) ||
        reply[1] != static_cast<std::uint8_t>(id & 0xFFU) || reply[2] == 0x80U) {
        if (error.empty()) error = "broker rejected MQTT SUBSCRIBE";
        return false;
    }
    return true;
}

bool MqttClient::publish(const std::string& topic, const std::string& payload,
                         std::string& error) {
    if (!connected()) {
        error = "MQTT is not connected";
        return false;
    }
    std::vector<std::uint8_t> body;
    append_string(body, topic);
    body.insert(body.end(), payload.begin(), payload.end());
    const auto packet = make_packet(0x30U, body);
    if (!send_all(impl_->socket, packet.data(), packet.size(), error)) return false;
    impl_->last_io = std::chrono::steady_clock::now();
    return true;
}

bool MqttClient::poll(int timeout_ms, std::string& error) {
    if (!connected()) {
        error = "MQTT is not connected";
        return false;
    }
    std::uint8_t header = 0;
    std::vector<std::uint8_t> body;
    if (!impl_->read_packet(timeout_ms, header, body, error)) {
        impl_->is_connected = false;
        return false;
    }
    if (header != 0U) {
        const std::uint8_t type = static_cast<std::uint8_t>(header >> 4U);
        if (type == 3U && body.size() >= 2U) {
            const std::size_t topic_length = static_cast<std::size_t>(body[0] << 8U) | body[1];
            if (2U + topic_length <= body.size()) {
                const std::string topic(body.begin() + 2,
                                        body.begin() + static_cast<std::ptrdiff_t>(2U + topic_length));
                std::size_t payload_offset = 2U + topic_length;
                const std::uint8_t qos = static_cast<std::uint8_t>((header >> 1U) & 0x03U);
                if (qos > 0U) payload_offset += 2U;
                if (payload_offset <= body.size() && impl_->handler) {
                    const std::string payload(body.begin() + static_cast<std::ptrdiff_t>(payload_offset),
                                              body.end());
                    impl_->handler(topic, payload);
                }
            }
        }
    }

    const auto now = std::chrono::steady_clock::now();
    const auto idle = std::chrono::duration_cast<std::chrono::seconds>(now - impl_->last_io).count();
    if (idle >= static_cast<long long>(impl_->keepalive_s / 2U)) {
        const std::uint8_t ping[] = {0xC0U, 0x00U};
        if (!send_all(impl_->socket, ping, sizeof(ping), error)) {
            impl_->is_connected = false;
            return false;
        }
        impl_->last_io = now;
    }
    return true;
}

void MqttClient::disconnect() {
    if (impl_->socket != kInvalidSocket) {
        const std::uint8_t packet[] = {0xE0U, 0x00U};
        std::string ignored;
        if (impl_->is_connected) {
            send_all(impl_->socket, packet, sizeof(packet), ignored);
        }
        close_socket(impl_->socket);
    }
    impl_->socket = kInvalidSocket;
    impl_->is_connected = false;
}

bool MqttClient::connected() const { return impl_->is_connected; }

void MqttClient::set_message_handler(MessageHandler handler) {
    impl_->handler = std::move(handler);
}

}  // namespace gateway
