#pragma once

#include "math/Vec3.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace Caffeine::Render {

struct TextureQualitySettings {
    bool enabled          = true;
    f32  fullRadius       = 35.0f;   // full resolution within this distance (meters)
    f32  falloffDistance = 100.0f;  // linear decay over this range beyond fullRadius
    f32  minScale         = 0.25f;  // minimum resolution scale at far edge (0.25 = 1/4 res)
};

/// 0 = full, 1 = half, 2 = quarter, 3 = eighth (min 16px).
inline u32 kTextureQualityMaxTier = 3;

inline f32 textureQualityScale(f32 distance, const TextureQualitySettings& settings) {
    if (!settings.enabled) return 1.0f;
    if (distance <= settings.fullRadius) return 1.0f;
    if (settings.falloffDistance <= 1e-4f) {
        return std::clamp(settings.minScale, 0.0625f, 1.0f);
    }
    const f32 t =
        std::clamp((distance - settings.fullRadius) / settings.falloffDistance, 0.0f, 1.0f);
    const f32 minScale = std::clamp(settings.minScale, 0.0625f, 1.0f);
    return 1.0f - t * (1.0f - minScale);
}

inline u32 textureQualityTier(f32 distance, const TextureQualitySettings& settings) {
    const f32 scale = textureQualityScale(distance, settings);
    if (scale >= 0.75f) return 0;
    if (scale >= 0.375f) return 1;
    if (scale >= 0.1875f) return 2;
    return kTextureQualityMaxTier;
}

inline f32 distanceToNearestViewer(const Vec3& worldPos, const std::vector<Vec3>& viewers) {
    if (viewers.empty()) return 0.0f;
    f32 best = 1e30f;
    for (const Vec3& viewer : viewers) {
        best = std::min(best, (worldPos - viewer).length());
    }
    return best;
}

inline std::string textureCacheKeyForTier(const std::string& baseKey, u32 tier) {
    if (tier == 0) return baseKey;
    return baseKey + "#q" + std::to_string(tier);
}

inline u32 textureDimensionForTier(u32 fullDim, u32 tier) {
    u32 dim = std::max(1u, fullDim);
    for (u32 i = 0; i < tier; ++i) {
        dim = std::max(16u, dim / 2);
    }
    return dim;
}

}  // namespace Caffeine::Render
