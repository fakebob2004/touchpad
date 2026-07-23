#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace mtp {

constexpr std::size_t kHeaderSize = 36;
constexpr std::size_t kContactSize = 44;
constexpr std::size_t kMaxContacts = 10;
constexpr std::size_t kMaxMessageSize = kHeaderSize + kMaxContacts * kContactSize;

enum class MessageType : std::uint16_t { Hello = 1, Frame = 2, Reset = 3 };

struct Contact {
    std::uint32_t identifier{};
    std::uint8_t state{};
    std::uint8_t flags{};
    float x{};
    float y{};
    float velocity_x{};
    float velocity_y{};
    float size{};
    float angle{};
    float major_axis{};
    float minor_axis{};
    float density{};
};

struct Message {
    MessageType type{};
    std::uint32_t sequence{};
    std::uint64_t capture_time_us{};
    std::uint64_t device_time_us{};
    std::vector<Contact> contacts;
};

class ProtocolError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] std::size_t ValidateHeader(std::span<const std::uint8_t, kHeaderSize> header);
[[nodiscard]] Message DecodeMessage(std::span<const std::uint8_t> bytes);

class StreamDecoder {
public:
    void Append(std::span<const std::uint8_t> bytes);
    [[nodiscard]] bool HasMessage() const noexcept { return !messages_.empty(); }
    [[nodiscard]] Message PopMessage();
    void Reset() noexcept;

private:
    void ParseAvailable();
    std::vector<std::uint8_t> buffer_;
    std::vector<Message> messages_;
};

} // namespace mtp
