#include "Mtp1.h"
#include "TouchSession.h"

#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

void Check(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}

void PutU16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value >> 8);
    bytes[offset + 1] = static_cast<std::uint8_t>(value);
}

void PutU32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value >> 24);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 16);
    bytes[offset + 2] = static_cast<std::uint8_t>(value >> 8);
    bytes[offset + 3] = static_cast<std::uint8_t>(value);
}

void PutU64(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint64_t value) {
    PutU32(bytes, offset, static_cast<std::uint32_t>(value >> 32));
    PutU32(bytes, offset + 4, static_cast<std::uint32_t>(value));
}

void PutF32(std::vector<std::uint8_t>& bytes, std::size_t offset, float value) {
    PutU32(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

struct WireContact { std::uint32_t id; std::uint8_t flags; float x; float y; };

std::vector<std::uint8_t> Encode(mtp::MessageType type, std::uint32_t sequence,
                                 std::span<const WireContact> contacts = {}) {
    const std::size_t count = type == mtp::MessageType::Frame ? contacts.size() : 0;
    std::vector<std::uint8_t> bytes(mtp::kHeaderSize + count * mtp::kContactSize, 0);
    bytes[0] = 'M'; bytes[1] = 'T'; bytes[2] = 'P'; bytes[3] = '1';
    PutU16(bytes, 4, 1);
    PutU16(bytes, 6, static_cast<std::uint16_t>(type));
    PutU32(bytes, 8, static_cast<std::uint32_t>(count * mtp::kContactSize));
    PutU32(bytes, 12, sequence);
    PutU16(bytes, 16, static_cast<std::uint16_t>(count));
    PutU64(bytes, 20, 1000000 + sequence);
    PutU64(bytes, 28, 2000000 + sequence);
    for (std::size_t index = 0; index < count; ++index) {
        const std::size_t offset = mtp::kHeaderSize + index * mtp::kContactSize;
        PutU32(bytes, offset, contacts[index].id);
        bytes[offset + 4] = 4;
        bytes[offset + 5] = contacts[index].flags;
        PutF32(bytes, offset + 8, contacts[index].x);
        PutF32(bytes, offset + 12, contacts[index].y);
    }
    return bytes;
}

void ExpectProtocolError(const std::function<void()>& operation) {
    try { operation(); } catch (const mtp::ProtocolError&) { return; }
    throw std::runtime_error("expected ProtocolError");
}

void TestFragmentationAndCoalescing() {
    const auto hello = Encode(mtp::MessageType::Hello, 1);
    const std::array contacts{WireContact{42, 7, 0.25f, 0.75f}};
    const auto frame = Encode(mtp::MessageType::Frame, 2, contacts);
    std::vector<std::uint8_t> stream = hello;
    stream.insert(stream.end(), frame.begin(), frame.end());
    for (std::size_t split = 0; split <= stream.size(); ++split) {
        mtp::StreamDecoder decoder;
        decoder.Append(std::span(stream).first(split));
        decoder.Append(std::span(stream).subspan(split));
        Check(decoder.HasMessage(), "HELLO missing after fragmentation");
        Check(decoder.PopMessage().type == mtp::MessageType::Hello, "wrong first message");
        Check(decoder.HasMessage(), "FRAME missing after fragmentation");
        const auto decoded = decoder.PopMessage();
        Check(decoded.contacts.size() == 1 && decoded.contacts[0].identifier == 42, "contact mismatch");
        Check(std::abs(decoded.contacts[0].x - 0.25f) < 0.00001f, "float mismatch");
    }
}

void TestMalformedInput() {
    auto bytes = Encode(mtp::MessageType::Frame, 1, std::array{WireContact{1, 7, 0.5f, 0.5f}});
    bytes[0] = 'X';
    ExpectProtocolError([&] { (void)mtp::DecodeMessage(bytes); });
    bytes = Encode(mtp::MessageType::Frame, 1, std::array{WireContact{1, 7, 0.5f, 0.5f}});
    PutF32(bytes, mtp::kHeaderSize + 8, std::numeric_limits<float>::quiet_NaN());
    ExpectProtocolError([&] { (void)mtp::DecodeMessage(bytes); });
    const std::array duplicates{WireContact{7, 7, 0.1f, 0.2f}, WireContact{7, 7, 0.3f, 0.4f}};
    bytes = Encode(mtp::MessageType::Frame, 1, duplicates);
    ExpectProtocolError([&] { (void)mtp::DecodeMessage(bytes); });
}

void TestLifecycleSlotsAndLift() {
    mtp::TouchSession session;
    Check(!session.Process(mtp::DecodeMessage(Encode(mtp::MessageType::Hello, 10))), "HELLO reported");
    const auto reset = session.Process(mtp::DecodeMessage(Encode(mtp::MessageType::Reset, 11)));
    Check(reset && reset->flags == MTP_IOCTL_FRAME_RESET, "RESET not emitted");
    const std::array first{WireContact{100, 7, -1.0f, 2.0f}, WireContact{200, 7, 0.5f, 0.25f}};
    const auto report1 = session.Process(mtp::DecodeMessage(Encode(mtp::MessageType::Frame, 12, first)));
    Check(report1 && report1->active_contact_count == 2, "active count mismatch");
    Check(report1->contacts[0].slot == 0 && report1->contacts[0].x == 0, "slot/clamp mismatch");
    Check(report1->contacts[1].slot == 1, "second slot mismatch");
    const std::array second{WireContact{200, 7, 0.6f, 0.3f}};
    const auto report2 = session.Process(mtp::DecodeMessage(Encode(mtp::MessageType::Frame, 13, second)));
    Check(report2 && report2->active_contact_count == 1 && report2->report_contact_count == 2,
          "lift report mismatch");
    Check((report2->contacts[0].flags & MTP_IOCTL_CONTACT_TIP) == 0, "lift Tip remained set");
    Check(report2->contacts[1].slot == 1, "stable slot lost");
    const std::array third{WireContact{300, 7, 0.2f, 0.2f}, WireContact{200, 7, 0.7f, 0.4f}};
    const auto report3 = session.Process(mtp::DecodeMessage(Encode(mtp::MessageType::Frame, 14, third)));
    Check(report3 && report3->contacts[0].slot == 0, "lowest slot was not reused");
}

void TestSequenceAndDisconnectRelease() {
    mtp::TouchSession session;
    (void)session.Process(mtp::DecodeMessage(Encode(mtp::MessageType::Hello, 1)));
    (void)session.Process(mtp::DecodeMessage(Encode(mtp::MessageType::Reset, 2)));
    (void)session.Process(mtp::DecodeMessage(Encode(mtp::MessageType::Frame, 3,
        std::array{WireContact{1, 7, 0.5f, 0.5f}})));
    ExpectProtocolError([&] { (void)session.Process(mtp::DecodeMessage(Encode(mtp::MessageType::Frame, 5))); });
    const auto release = session.OnDisconnect();
    Check(release && release->report_contact_count == 1 && release->active_contact_count == 0,
          "disconnect did not release contact");
}

void TestSustainedRate() {
    mtp::TouchSession session;
    (void)session.Process(mtp::DecodeMessage(Encode(mtp::MessageType::Hello, 1)));
    (void)session.Process(mtp::DecodeMessage(Encode(mtp::MessageType::Reset, 2)));
    const std::array contacts{WireContact{1, 7, 0.1f, 0.2f}, WireContact{2, 7, 0.2f, 0.3f},
                              WireContact{3, 7, 0.3f, 0.4f}, WireContact{4, 7, 0.4f, 0.5f},
                              WireContact{5, 7, 0.5f, 0.6f}};
    for (std::uint32_t sequence = 3; sequence < 12503; ++sequence) {
        const auto report = session.Process(mtp::DecodeMessage(Encode(mtp::MessageType::Frame, sequence, contacts)));
        Check(report && report->active_contact_count == 5, "sustained report failed");
    }
}

} // namespace

int main() {
    try {
        TestFragmentationAndCoalescing();
        TestMalformedInput();
        TestLifecycleSlotsAndLift();
        TestSequenceAndDisconnectRelease();
        TestSustainedRate();
        std::cout << "mtp-tests: ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "mtp-tests: failed: " << error.what() << '\n';
        return 1;
    }
}
