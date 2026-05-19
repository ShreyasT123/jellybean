#include <gtest/gtest.h>
#include <array>
#include <vector>
#include "jellybean/proto/codec.hpp"

using namespace jellybean::proto;

TEST(ProtoCodecTest, EncodeParseRoundTrip) {
    const std::array<std::byte, 5> payload{
        std::byte{0x11},
        std::byte{0x22},
        std::byte{0x33},
        std::byte{0x44},
        std::byte{0x55}
    };

    std::vector<std::byte> buffer(encoded_frame_size(payload.size()));
    const std::size_t encoded = encode_frame(
        buffer,
        MessageType::ActorMessage,
        0x7u,
        42u,
        99u,
        payload);

    ASSERT_EQ(encoded, buffer.size());

    const auto parsed = parse_frame(std::span<const std::byte>(buffer.data(), encoded));
    ASSERT_TRUE(parsed.valid);
    ASSERT_NE(parsed.header_ptr(), nullptr);
    EXPECT_EQ(parsed.header.magic, MAGIC);
    EXPECT_EQ(parsed.header.version, VERSION);
    EXPECT_EQ(parsed.header.type, static_cast<std::uint16_t>(MessageType::ActorMessage));
    EXPECT_EQ(parsed.header.flags, 0x7u);
    EXPECT_EQ(parsed.header.actor_id, 42u);
    EXPECT_EQ(parsed.header.message_id, 99u);
    EXPECT_EQ(parsed.header.payload_length, payload.size());
    const auto parsed_payload = parsed.payload();
    EXPECT_TRUE(std::equal(parsed_payload.begin(), parsed_payload.end(), payload.begin()));
}

TEST(ProtoCodecTest, CorruptedPayloadFailsCrc) {
    const std::array<std::byte, 4> payload{
        std::byte{0xAA},
        std::byte{0xBB},
        std::byte{0xCC},
        std::byte{0xDD}
    };

    std::vector<std::byte> buffer(encoded_frame_size(payload.size()));
    const std::size_t encoded = encode_frame(buffer, MessageType::Heartbeat, 7u, payload);
    ASSERT_EQ(encoded, buffer.size());

    buffer[sizeof(FrameHeader)] ^= std::byte{0x01};

    const auto parsed = parse_frame(std::span<const std::byte>(buffer.data(), encoded));
    EXPECT_FALSE(parsed.valid);
    EXPECT_NE(parsed.expected_crc, parsed.actual_crc);
}

TEST(ProtoCodecTest, RejectsShortBuffer) {
    std::array<std::byte, sizeof(FrameHeader) - 1> short_buf{};
    const auto parsed = parse_frame(short_buf);
    EXPECT_FALSE(parsed.valid);
    EXPECT_EQ(parsed.header_ptr(), nullptr);
}

TEST(ProtoCodecTest, EncodeAddsExpectedPadding) {
    const std::array<std::byte, 3> payload{
        std::byte{0x10},
        std::byte{0x20},
        std::byte{0x30}
    };

    std::vector<std::byte> buffer(encoded_frame_size(payload.size()));
    const std::size_t encoded = encode_frame(buffer, MessageType::Handshake, 1u, payload);
    const auto layout = layout_for_payload(payload.size());

    ASSERT_EQ(encoded, layout.total_size);
    ASSERT_EQ(encoded, buffer.size());

    const std::size_t payload_end = sizeof(FrameHeader) + payload.size();
    for (std::size_t i = payload_end; i < layout.crc_offset; ++i) {
        EXPECT_EQ(buffer[i], std::byte{0});
    }

    const auto parsed = parse_frame(std::span<const std::byte>(buffer.data(), encoded));
    EXPECT_TRUE(parsed.valid);
}
