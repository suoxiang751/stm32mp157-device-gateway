#include "gateway/device_state.hpp"

#include <sstream>

namespace gateway {
namespace {

std::string escape_json(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (const char c : value) {
        if (c == '"' || c == '\\') out.push_back('\\');
        if (static_cast<unsigned char>(c) >= 0x20U) out.push_back(c);
    }
    return out;
}

}  // namespace

void DeviceState::mark_pending(const ControlCommand& command,
                               std::chrono::steady_clock::time_point deadline) {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_[command.request_id] = PendingCommand{command, CommandPhase::queued, deadline};
}

void DeviceState::mark_sent(const std::string& request_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = pending_.find(request_id);
    if (it != pending_.end()) it->second.phase = CommandPhase::sent;
}

std::optional<ControlCommand> DeviceState::mark_failed(const std::string& request_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = pending_.find(request_id);
    if (it == pending_.end()) return std::nullopt;
    auto command = it->second.command;
    pending_.erase(it);
    return command;
}

std::optional<ControlCommand> DeviceState::confirm_write(
    std::uint8_t slave, std::uint16_t address, std::uint16_t value) {
    std::lock_guard<std::mutex> lock(mutex_);
    registers_[address] = value;
    for (auto it = pending_.begin(); it != pending_.end(); ++it) {
        const auto& command = it->second.command;
        if (command.slave == slave && command.address == address && command.value == value) {
            auto confirmed = command;
            pending_.erase(it);
            return confirmed;
        }
    }
    return std::nullopt;
}

std::optional<ControlCommand> DeviceState::expire_one(
    std::chrono::steady_clock::time_point now) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = pending_.begin(); it != pending_.end(); ++it) {
        if (now >= it->second.deadline) {
            auto expired = it->second.command;
            pending_.erase(it);
            return expired;
        }
    }
    return std::nullopt;
}

const char* DeviceState::phase_name(CommandPhase phase) {
    switch (phase) {
        case CommandPhase::queued: return "queued";
        case CommandPhase::sent: return "sent";
        case CommandPhase::acknowledged: return "acknowledged";
        case CommandPhase::applied: return "applied";
        case CommandPhase::timed_out: return "timed_out";
        case CommandPhase::failed: return "failed";
    }
    return "unknown";
}

std::string DeviceState::snapshot_json() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream out;
    out << "{\"type\":\"state\",\"registers\":{";
    bool first = true;
    for (const auto& item : registers_) {
        if (!first) out << ',';
        first = false;
        out << '\"' << item.first << "\":" << item.second;
    }
    out << "},\"pending_count\":" << pending_.size() << '}';
    return out.str();
}

std::string DeviceState::result_json(const ControlCommand& command,
                                     CommandPhase phase,
                                     const std::string& detail) const {
    std::ostringstream out;
    out << "{\"type\":\"command_result\",\"request_id\":\""
        << escape_json(command.request_id) << "\",\"phase\":\""
        << phase_name(phase) << "\",\"slave\":" << static_cast<unsigned>(command.slave)
        << ",\"address\":" << command.address << ",\"value\":" << command.value
        << ",\"detail\":\"" << escape_json(detail) << "\"}";
    return out.str();
}

}  // namespace gateway

