#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace gateway {

class MqttClient {
public:
    using MessageHandler = std::function<void(const std::string&, const std::string&)>;

    MqttClient();
    ~MqttClient();
    MqttClient(const MqttClient&) = delete;
    MqttClient& operator=(const MqttClient&) = delete;

    bool connect_to(const std::string& host, std::uint16_t port,
                    const std::string& client_id, std::uint16_t keepalive_s,
                    std::string& error);
    bool subscribe(const std::string& topic, std::string& error);
    bool publish(const std::string& topic, const std::string& payload, std::string& error);
    bool poll(int timeout_ms, std::string& error);
    void disconnect();
    bool connected() const;
    void set_message_handler(MessageHandler handler);

private:
    struct Impl;
    Impl* impl_;
};

}  // namespace gateway

