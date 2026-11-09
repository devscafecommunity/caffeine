#pragma once

#include "core/Types.hpp"
#include <algorithm>

namespace Caffeine {

/// World space convention: 1 engine unit = 1 meter.
inline constexpr f32 kMetersPerWorldUnit = 1.0f;

namespace WorldUnits {
inline constexpr f32 centimeter = 0.01f;
inline constexpr f32 meter = 1.0f;
inline constexpr f32 kilometer = 1000.0f;

/// Default landscape patch (a playable valley, not a continent).
inline constexpr f32 kDefaultTerrainSizeM = 512.0f;
/// Local relief: rolling hills. Peaks always hit this only if generation remaps 0–1.
inline constexpr f32 kDefaultTerrainHeightM = 80.0f;
/// Alpine patch on a 1 km map — not Everest (8849 m) and not a planet.
inline constexpr f32 kAlpineTerrainHeightM = 220.0f;
inline constexpr f32 kAlpineTerrainSizeM = 1024.0f;

inline f32 suggestedHeightM(f32 worldSizeM) {
    // ~8–15% of horizontal span reads as real terrain, not a crater.
    return std::clamp(worldSizeM * 0.12f, 24.0f, 400.0f);
}
}  // namespace WorldUnits

}  // namespace Caffeine
