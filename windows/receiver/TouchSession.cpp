#include "TouchSession.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace mtp {
namespace {
constexpr std::uint8_t kTip = 0x02;
constexpr auto kFrameTimeout = std::chrono::milliseconds(200);
}

std::optional<MTP_IOCTL_FRAME> TouchSession::Process(const Message& message) {
    if (sequence_seen_ && message.sequence != previous_sequence_ + 1u) {
        throw ProtocolError("sequence discontinuity");
    }
    sequence_seen_ = true;
    previous_sequence_ = message.sequence;

    if (message.type == MessageType::Hello) {
        if (hello_seen_ || reset_seen_) {
            throw ProtocolError("unexpected HELLO");
        }
        hello_seen_ = true;
        return std::nullopt;
    }
    if (!hello_seen_) {
        throw ProtocolError("message received before HELLO");
    }
    if (message.type == MessageType::Reset) {
        reset_seen_ = true;
        return ReleaseAll(message.sequence, true);
    }
    if (!reset_seen_) {
        throw ProtocolError("FRAME received before RESET");
    }

    MTP_IOCTL_FRAME output{};
    output.abi_version = MTP_IOCTL_ABI_VERSION;
    output.sequence = message.sequence;
    output.scan_time_100us =
        static_cast<std::uint16_t>((message.capture_time_us / 100u) & 0xffffu);
    std::unordered_set<std::uint32_t> present;
    std::array<std::optional<Contact>, MTP_HID_MAX_CONTACTS> next_contacts{};

    for (const Contact& contact : message.contacts) {
        if ((contact.flags & kTip) == 0) {
            continue;
        }
        present.insert(contact.identifier);
        auto found = slots_.find(contact.identifier);
        std::uint8_t slot;
        if (found == slots_.end()) {
            if (slots_.size() >= MTP_HID_MAX_CONTACTS) {
                throw ProtocolError("more than five active contacts");
            }
            slot = AllocateSlot();
            slots_.emplace(contact.identifier, slot);
        } else {
            slot = found->second;
        }
        next_contacts[slot] = contact;
    }

    std::vector<std::uint32_t> leaving;
    for (const auto& [identifier, slot] : slots_) {
        if (!present.contains(identifier)) {
            leaving.push_back(identifier);
            if (previous_contacts_[slot].has_value()) {
                next_contacts[slot] = previous_contacts_[slot];
                next_contacts[slot]->flags &= static_cast<std::uint8_t>(~kTip);
            }
        }
    }

    for (std::uint8_t slot = 0; slot < MTP_HID_MAX_CONTACTS; ++slot) {
        if (!next_contacts[slot].has_value()) {
            continue;
        }
        if (output.report_contact_count >= MTP_HID_MAX_CONTACTS) {
            throw ProtocolError("report contact capacity exceeded");
        }
        MTP_IOCTL_CONTACT& target = output.contacts[output.report_contact_count++];
        const Contact& source = *next_contacts[slot];
        target.slot = slot;
        target.flags = source.flags;
        target.x = ToLogical(source.x);
        target.y = ToLogical(source.y);
        if ((source.flags & kTip) != 0) {
            ++output.active_contact_count;
        }
    }

    for (std::uint32_t identifier : leaving) {
        const auto found = slots_.find(identifier);
        if (found != slots_.end()) {
            previous_contacts_[found->second].reset();
            slots_.erase(found);
        }
    }
    for (const auto& [identifier, slot] : slots_) {
        const auto contact = std::find_if(message.contacts.begin(), message.contacts.end(),
            [identifier](const Contact& item) { return item.identifier == identifier; });
        if (contact != message.contacts.end()) {
            previous_contacts_[slot] = *contact;
        }
    }
    last_frame_time_ = std::chrono::steady_clock::now();
    return output;
}

std::optional<MTP_IOCTL_FRAME> TouchSession::OnDisconnect() {
    const bool should_report = HasActiveContacts();
    MTP_IOCTL_FRAME release = ReleaseAll(previous_sequence_, true);
    hello_seen_ = false;
    reset_seen_ = false;
    sequence_seen_ = false;
    return should_report ? std::optional<MTP_IOCTL_FRAME>(release) : std::nullopt;
}

std::optional<MTP_IOCTL_FRAME> TouchSession::OnTimeout(std::chrono::steady_clock::time_point now) {
    if (!HasActiveContacts() || now - last_frame_time_ < kFrameTimeout) {
        return std::nullopt;
    }
    return ReleaseAll(previous_sequence_, true);
}

MTP_IOCTL_FRAME TouchSession::ReleaseAll(std::uint32_t sequence, bool reset) {
    MTP_IOCTL_FRAME output{};
    output.abi_version = MTP_IOCTL_ABI_VERSION;
    output.sequence = sequence;
    output.flags = reset ? MTP_IOCTL_FRAME_RESET : 0;
    for (std::uint8_t slot = 0; slot < MTP_HID_MAX_CONTACTS; ++slot) {
        if (!previous_contacts_[slot].has_value()) {
            continue;
        }
        const Contact& source = *previous_contacts_[slot];
        MTP_IOCTL_CONTACT& target = output.contacts[output.report_contact_count++];
        target.slot = slot;
        target.flags = source.flags & static_cast<std::uint8_t>(~kTip);
        target.x = ToLogical(source.x);
        target.y = ToLogical(source.y);
    }
    slots_.clear();
    previous_contacts_.fill(std::nullopt);
    return output;
}

std::uint16_t TouchSession::ToLogical(float value) {
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    return static_cast<std::uint16_t>(std::lround(clamped * MTP_COORDINATE_LOGICAL_MAX));
}

std::uint8_t TouchSession::AllocateSlot() const {
    for (std::uint8_t candidate = 0; candidate < MTP_HID_MAX_CONTACTS; ++candidate) {
        const bool used = std::any_of(slots_.begin(), slots_.end(),
            [candidate](const auto& item) { return item.second == candidate; });
        if (!used) {
            return candidate;
        }
    }
    throw ProtocolError("no free HID contact slot");
}

} // namespace mtp
