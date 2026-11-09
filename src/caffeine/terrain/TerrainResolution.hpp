#pragma once

#include "core/Types.hpp"
#include "ecs/TerrainComponents.hpp"

#include <algorithm>

namespace Caffeine::Terrain {

// Splat grid shares terrain corners; interior samples are scaled by splatResolutionScale.
inline u32 splatResolutionFor(u32 heightResolution, u32 scale) {
    scale = std::max(1u, scale);
    if (heightResolution <= 1u) return 2u;
    return (heightResolution - 1u) * scale + 1u;
}

inline u32 splatResolutionX(const ECS::TerrainComponent& terrain) {
    return splatResolutionFor(terrain.resolutionX, terrain.splatResolutionScale);
}

inline u32 splatResolutionZ(const ECS::TerrainComponent& terrain) {
    return splatResolutionFor(terrain.resolutionZ, terrain.splatResolutionScale);
}

inline u32 inferSplatResolutionScale(u32 heightResolution, u32 splatResolution) {
    if (heightResolution <= 1u) return 1u;
    const u32 heightSpan = heightResolution - 1u;
    const u32 splatSpan = splatResolution > 1u ? splatResolution - 1u : 1u;
    return std::max(1u, (splatSpan + heightSpan - 1u) / heightSpan);
}

}  // namespace Caffeine::Terrain
