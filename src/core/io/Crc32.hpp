#pragma once

#include "core/Types.hpp"

namespace Caffeine::IO {

// CRC32 using the standard IEEE 802.3 polynomial (0xEDB88320, reflected).
// Self-contained — no external dependencies.
namespace detail {

inline constexpr u32 kCrc32Poly = 0xEDB88320u;

inline constexpr auto buildCrc32Table() noexcept {
    struct Table { u32 v[256] = {}; };
    Table t{};
    for (u32 i = 0; i < 256; ++i) {
        u32 crc = i;
        for (int j = 0; j < 8; ++j)
            crc = (crc >> 1) ^ ((crc & 1) ? kCrc32Poly : 0u);
        t.v[i] = crc;
    }
    return t;
}

inline constexpr auto kCrc32Table = buildCrc32Table();

} // namespace detail

// Returns CRC32 of [data, data+size).
[[nodiscard]] inline u32 crc32(const void* data, u64 size) noexcept {
    const auto* bytes = static_cast<const u8*>(data);
    u32 crc = 0xFFFFFFFFu;
    for (u64 i = 0; i < size; ++i)
        crc = (crc >> 8) ^ detail::kCrc32Table.v[(crc ^ bytes[i]) & 0xFF];
    return crc ^ 0xFFFFFFFFu;
}

// Continues an in-progress CRC32 computation (for streaming).
[[nodiscard]] inline u32 crc32Continue(u32 prev, const void* data, u64 size) noexcept {
    const auto* bytes = static_cast<const u8*>(data);
    u32 crc = prev ^ 0xFFFFFFFFu;
    for (u64 i = 0; i < size; ++i)
        crc = (crc >> 8) ^ detail::kCrc32Table.v[(crc ^ bytes[i]) & 0xFF];
    return crc ^ 0xFFFFFFFFu;
}

} // namespace Caffeine::IO
