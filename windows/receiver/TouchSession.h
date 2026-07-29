#pragma once

#include "Mtp1.h"
#include "TouchpadIoctl.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <unordered_map>

namespace mtp {

class TouchSession {
public:
    [[nodiscard]] std::optional<MTP_IOCTL_FRAME> Process(const Message& message);
    [[nodiscard]] std::optional<MTP_IOCTL_FRAME> OnDisconnect();
    [[nodiscard]] std::optional<MTP_IOCTL_FRAME> OnTimeout(std::chrono::steady_clock::time_point now);
    [[nodiscard]] bool HasActiveContacts() const noexcept {
        return !slots_.empty() || button_down_;
    }

private:
    [[nodiscard]] MTP_IOCTL_FRAME ReleaseAll(std::uint32_t sequence, bool reset);
    [[nodiscard]] static std::uint16_t ToLogical(float value);
    [[nodiscard]] static std::uint16_t ToLogicalY(float value);
    [[nodiscard]] std::uint8_t AllocateSlot() const;

    bool hello_seen_{};
    bool reset_seen_{};
    bool sequence_seen_{};
    bool button_down_{};
    std::uint32_t previous_sequence_{};
    std::unordered_map<std::uint32_t, std::uint8_t> slots_;
    std::array<std::optional<Contact>, MTP_HID_MAX_CONTACTS> previous_contacts_{};
    std::chrono::steady_clock::time_point last_frame_time_{};
};

} // namespace mtp
