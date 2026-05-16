#include "jellybean/proto/codec.hpp"
#include <array>
#include <cstring>
#include <limits>

#if defined(__SSE4_2__) && (defined(__x86_64__) || defined(_M_X64))
#include <nmmintrin.h>
#endif
#include <span>

namespace jellybean::proto {
namespace {

constexpr std::uint32_t CRC32C_POLYNOMIAL = 0x82F63B78u;

constexpr std::array<std::uint32_t, 256> make_crc32c_table() noexcept {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < table.size(); ++i) {
        std::uint32_t crc = i;
        for (int bit = 0; bit < 8; ++bit) {
            if ((crc & 1u) != 0u) {
                crc = (crc >> 1u) ^ CRC32C_POLYNOMIAL;
            } else {
                crc >>= 1u;
            }
        }
        table[i] = crc;
    }
    return table;
}

constexpr auto CRC32C_TABLE = make_crc32c_table();

[[nodiscard, maybe_unused]] std::uint32_t crc32c_software(std::span<const std::byte> data) noexcept {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (const std::byte value : data) {
        const auto byte = std::to_integer<std::uint8_t>(value);
        const auto idx = static_cast<std::uint8_t>((crc ^ byte) & 0xFFu);
        crc = (crc >> 8u) ^ CRC32C_TABLE[idx];
    }
    return ~crc;
}

} // namespace

std::uint32_t crc32c(std::span<const std::byte> data) noexcept {
#if defined(__SSE4_2__) && (defined(__x86_64__) || defined(_M_X64))
    std::uint32_t crc = 0xFFFFFFFFu;
    const auto* p = reinterpret_cast<const std::uint8_t*>(data.data());
    std::size_t len = data.size();

    while (len >= sizeof(std::uint64_t)) {
        std::uint64_t chunk = 0;
        std::memcpy(&chunk, p, sizeof(chunk));
        std::uint64_t hw_crc = crc;
        hw_crc = static_cast<std::uint64_t>(_mm_crc32_u64(hw_crc, chunk));
        crc = static_cast<std::uint32_t>(hw_crc);
        p += sizeof(chunk);
        len -= sizeof(chunk);
    }

    while (len > 0) {
        crc = _mm_crc32_u8(crc, *p);
        ++p;
        --len;
    }
    return ~crc;
#else
    return crc32c_software(data);
#endif
}

ParsedFrame parse_frame(std::span<const std::byte> buffer) noexcept {
    ParsedFrame parsed{};
    if (buffer.size() < sizeof(FrameHeader) + CRC_SIZE) {
        return parsed;
    }

    const auto* header = reinterpret_cast<const FrameHeader*>(buffer.data());
    if (header->magic != MAGIC || header->version != VERSION) {
        return parsed;
    }

    const std::size_t payload_size = static_cast<std::size_t>(header->payload_length);
    const auto layout = layout_for_payload(payload_size);
    if (layout.total_size < sizeof(FrameHeader) + CRC_SIZE || layout.total_size > buffer.size()) {
        return parsed;
    }

    std::uint32_t wire_crc = 0;
    std::memcpy(&wire_crc, buffer.data() + layout.crc_offset, sizeof(wire_crc));
    const auto computed_crc = crc32c(buffer.first(sizeof(FrameHeader) + payload_size));

    parsed.header = header;
    parsed.payload = std::span<const std::byte>(buffer.data() + sizeof(FrameHeader), payload_size);
    parsed.expected_crc = computed_crc;
    parsed.actual_crc = wire_crc;
    parsed.valid = (computed_crc == wire_crc);
    return parsed;
}

std::size_t encode_frame(
    std::span<std::byte> buffer,
    MessageType type,
    std::uint16_t flags,
    std::uint64_t actor_id,
    std::uint64_t message_id,
    std::span<const std::byte> payload) noexcept {
    if (!payload_size_fits(payload.size())) {
        return 0;
    }

    const auto layout = layout_for_payload(payload.size());
    if (layout.total_size > buffer.size() || layout.total_size < sizeof(FrameHeader) + CRC_SIZE) {
        return 0;
    }

    FrameHeader header{};
    header.magic = MAGIC;
    header.version = VERSION;
    header.type = static_cast<std::uint16_t>(type);
    header.flags = flags;
    header.reserved = 0;
    header.payload_length = static_cast<std::uint32_t>(payload.size());
    header.actor_id = actor_id;
    header.message_id = message_id;
    std::memcpy(buffer.data(), &header, sizeof(header));

    if (!payload.empty()) {
        std::memcpy(buffer.data() + sizeof(FrameHeader), payload.data(), payload.size());
    }

    const std::size_t padding_size = layout.padded_payload_size - payload.size();
    if (padding_size > 0) {
        std::memset(buffer.data() + sizeof(FrameHeader) + payload.size(), 0, padding_size);
    }

    const std::uint32_t checksum = crc32c(buffer.first(sizeof(FrameHeader) + payload.size()));
    std::memcpy(buffer.data() + layout.crc_offset, &checksum, sizeof(checksum));
    return layout.total_size;
}

std::size_t encode_frame(
    std::span<std::byte> buffer,
    MessageType type,
    std::uint64_t actor_id,
    std::span<const std::byte> payload) noexcept {
    return encode_frame(buffer, type, 0, actor_id, 0, payload);
}

} // namespace jellybean::proto
