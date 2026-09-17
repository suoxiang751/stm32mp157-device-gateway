#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>

namespace gateway {

enum class CommandPhase { queued, sent, acknowledged, applied, timed_out, failed };

struct ControlCommand {
    std::string request_id;
    std::uint8_t slave{1};
    std::uint16_t address{0};
    std::uint16_t value{0};
    int attempts{0};
};

struct PendingCommand {
    ControlCommand command;
    CommandPhase phase{CommandPhase::queued};
    std::chrono::steady_clock::time_point deadline;
};

class DeviceState {
public:
    void mark_pending(const ControlCommand& command,
                      std::chrono::steady_clock::time_point deadline);
    void mark_sent(const std::string& request_id);
    std::optional<ControlCommand> mark_failed(const std::string& request_id);
    std::optional<ControlCommand> confirm_write(
        std::uint8_t slave, std::uint16_t address, std::uint16_t value);
    std::optional<ControlCommand> expire_one(std::chrono::steady_clock::time_point now);
    std::string snapshot_json() const;
    std::string result_json(const ControlCommand& command, CommandPhase phase,
                            const std::string& detail) const;

private:
    static const char* phase_name(CommandPhase phase);
    mutable std::mutex mutex_;
    std::map<std::uint16_t, std::uint16_t> registers_;
    std::map<std::string, PendingCommand> pending_;
};

}  // namespace gateway
