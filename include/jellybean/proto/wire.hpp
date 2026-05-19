#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace jellybean::proto {

inline constexpr std::uint32_t MAGIC = 0x48454C31u;  // "HEL1"
inline constexpr std::uint16_t VERSION = 1;
inline constexpr std::size_t PAYLOAD_ALIGNMENT = 8;
inline constexpr std::size_t CRC_SIZE = sizeof(std::uint32_t);

enum class MessageType : std::uint16_t {
    Handshake = 0x0001,
    ActorMessage = 0x0002,
    Heartbeat = 0x0003,
    RaftAppend = 0x0010,
    RaftVote = 0x0011,
    RaftSnapshot = 0x0012,
};

#pragma pack(push, 1)
struct FrameHeader {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint16_t type;
    std::uint16_t flags;
    std::uint16_t reserved;
    std::uint32_t payload_length;
    std::uint64_t actor_id;
    std::uint64_t message_id;
};
#pragma pack(pop)

static_assert(sizeof(FrameHeader) == 32, "FrameHeader layout must remain fixed");

struct ParsedFrame {
    FrameHeader header{};
    std::vector<std::byte> payload_owned{};
    bool valid{false};
    std::uint32_t expected_crc{0};
    std::uint32_t actual_crc{0};

    [[nodiscard]] auto header_ptr() const noexcept -> const FrameHeader* {
        return valid ? &header : nullptr;
    }

    [[nodiscard]] auto payload() const noexcept -> std::span<const std::byte> {
        return {payload_owned.data(), payload_owned.size()};
    }
};

[[nodiscard]] constexpr auto align_up(std::size_t value,
                                      std::size_t alignment) noexcept -> std::size_t {
    return (value + alignment - 1) & ~(alignment - 1);
}

[[nodiscard]] constexpr auto payload_size_fits(std::size_t payload_size) noexcept -> bool {
    return payload_size <= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max());
}

[[nodiscard]] constexpr auto encoded_frame_size(std::size_t payload_size) noexcept -> std::size_t {
    return sizeof(FrameHeader) + align_up(payload_size, PAYLOAD_ALIGNMENT) + CRC_SIZE;
}

[[nodiscard]] std::uint32_t crc32c(std::span<const std::byte> data) noexcept;

}  // namespace jellybean::proto
