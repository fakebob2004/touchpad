#include "Mtp1.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_set>

namespace mtp {
namespace {

std::uint16_t ReadU16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8) | p[1]);
}

std::uint32_t ReadU32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) |
           (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | p[3];
}

std::uint64_t ReadU64(const std::uint8_t* p) {
    return (static_cast<std::uint64_t>(ReadU32(p)) << 32) | ReadU32(p + 4);
}

float ReadF32(const std::uint8_t* p) {
    return std::bit_cast<float>(ReadU32(p));
}

MessageType ReadType(std::uint16_t value) {
    switch (value) {
    case 1: return MessageType::Hello;
    case 2: return MessageType::Frame;
    case 3: return MessageType::Reset;
    default: throw ProtocolError("unknown message type");
    }
}

void RequireFinite(float value) {
    if (!std::isfinite(value)) {
        throw ProtocolError("contact contains NaN or infinity");
    }
}

} // namespace

std::size_t ValidateHeader(std::span<const std::uint8_t, kHeaderSize> header) {
    if (std::memcmp(header.data(), "MTP1", 4) != 0) {
        throw ProtocolError("invalid magic");
    }
    if (ReadU16(header.data() + 4) != 1) {
        throw ProtocolError("unsupported version");
    }
    const MessageType type = ReadType(ReadU16(header.data() + 6));
    const std::uint32_t payload = ReadU32(header.data() + 8);
    const std::uint16_t count = ReadU16(header.data() + 16);
    const std::uint16_t flags = ReadU16(header.data() + 18);
    if ((flags & ~kFrameButton) != 0 || (type != MessageType::Frame && flags != 0)) {
        throw ProtocolError("invalid header flags");
    }
    if (count > kMaxContacts || payload != static_cast<std::uint32_t>(count * kContactSize)) {
        throw ProtocolError("invalid payload length or contact count");
    }
    if (type != MessageType::Frame && count != 0) {
        throw ProtocolError("control message contains contacts");
    }
    return kHeaderSize + payload;
}

Message DecodeMessage(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < kHeaderSize) {
        throw ProtocolError("truncated header");
    }
    std::array<std::uint8_t, kHeaderSize> header{};
    std::copy_n(bytes.begin(), kHeaderSize, header.begin());
    const std::size_t expected = ValidateHeader(header);
    if (bytes.size() != expected) {
        throw ProtocolError("message length mismatch");
    }

    Message message;
    message.type = ReadType(ReadU16(bytes.data() + 6));
    message.sequence = ReadU32(bytes.data() + 12);
    message.flags = ReadU16(bytes.data() + 18);
    message.capture_time_us = ReadU64(bytes.data() + 20);
    message.device_time_us = ReadU64(bytes.data() + 28);
    const std::uint16_t count = ReadU16(bytes.data() + 16);
    message.contacts.reserve(count);
    std::unordered_set<std::uint32_t> identifiers;
    for (std::uint16_t index = 0; index < count; ++index) {
        const std::uint8_t* p = bytes.data() + kHeaderSize + index * kContactSize;
        if (ReadU16(p + 6) != 0) {
            throw ProtocolError("contact reserved field is nonzero");
        }
        Contact contact;
        contact.identifier = ReadU32(p);
        contact.state = p[4];
        contact.flags = p[5];
        if ((contact.flags & ~std::uint8_t{0x07}) != 0) {
            throw ProtocolError("unknown contact flags");
        }
        contact.x = ReadF32(p + 8);
        contact.y = ReadF32(p + 12);
        contact.velocity_x = ReadF32(p + 16);
        contact.velocity_y = ReadF32(p + 20);
        contact.size = ReadF32(p + 24);
        contact.angle = ReadF32(p + 28);
        contact.major_axis = ReadF32(p + 32);
        contact.minor_axis = ReadF32(p + 36);
        contact.density = ReadF32(p + 40);
        RequireFinite(contact.x); RequireFinite(contact.y);
        RequireFinite(contact.velocity_x); RequireFinite(contact.velocity_y);
        RequireFinite(contact.size); RequireFinite(contact.angle);
        RequireFinite(contact.major_axis); RequireFinite(contact.minor_axis);
        RequireFinite(contact.density);
        if (!identifiers.insert(contact.identifier).second) {
            throw ProtocolError("duplicate contact identifier");
        }
        message.contacts.push_back(contact);
    }
    return message;
}

void StreamDecoder::Append(std::span<const std::uint8_t> bytes) {
    if (buffer_.size() + bytes.size() > kMaxMessageSize * 2) {
        throw ProtocolError("stream buffer limit exceeded");
    }
    buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
    ParseAvailable();
}

Message StreamDecoder::PopMessage() {
    if (messages_.empty()) {
        throw std::logic_error("no decoded message available");
    }
    Message message = std::move(messages_.front());
    messages_.erase(messages_.begin());
    return message;
}

void StreamDecoder::Reset() noexcept {
    buffer_.clear();
    messages_.clear();
}

void StreamDecoder::ParseAvailable() {
    while (buffer_.size() >= kHeaderSize) {
        std::array<std::uint8_t, kHeaderSize> header{};
        std::copy_n(buffer_.begin(), kHeaderSize, header.begin());
        const std::size_t length = ValidateHeader(header);
        if (buffer_.size() < length) {
            return;
        }
        messages_.push_back(DecodeMessage(std::span<const std::uint8_t>(buffer_.data(), length)));
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(length));
    }
}

} // namespace mtp
