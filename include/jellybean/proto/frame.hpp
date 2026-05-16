#pragma once
#include <cstddef>
#include "jellybean/proto/wire.hpp"

namespace jellybean::proto {

struct FrameLayout {
    std::size_t header_size{sizeof(FrameHeader)};
    std::size_t payload_size{0};
    std::size_t padded_payload_size{0};
    std::size_t crc_offset{sizeof(FrameHeader)};
    std::size_t total_size{sizeof(FrameHeader) + CRC_SIZE};
};

[[nodiscard]] constexpr FrameLayout layout_for_payload(std::size_t payload_size) noexcept {
    FrameLayout layout{};
    layout.payload_size = payload_size;
    layout.padded_payload_size = align_up(payload_size, PAYLOAD_ALIGNMENT);
    layout.crc_offset = sizeof(FrameHeader) + layout.padded_payload_size;
    layout.total_size = layout.crc_offset + CRC_SIZE;
    return layout;
}

} // namespace jellybean::proto
