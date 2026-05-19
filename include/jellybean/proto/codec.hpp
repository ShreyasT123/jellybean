#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

#include "jellybean/proto/frame.hpp"

namespace jellybean::proto {

[[nodiscard]] ParsedFrame parse_frame(std::span<const std::byte> buffer) noexcept;

[[nodiscard]] std::size_t encode_frame(std::span<std::byte> buffer, MessageType type,
                                       std::uint16_t flags, std::uint64_t actor_id,
                                       std::uint64_t message_id,
                                       std::span<const std::byte> payload) noexcept;

[[nodiscard]] std::size_t encode_frame(std::span<std::byte> buffer, MessageType type,
                                       std::uint64_t actor_id, std::span<const std::byte> payload) noexcept;

}  // namespace jellybean::proto
