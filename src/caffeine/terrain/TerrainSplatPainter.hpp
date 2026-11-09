#pragma once

#include "core/Types.hpp"
#include "ecs/TerrainComponents.hpp"
#include "math/Mat4.hpp"
#include "math/Vec3.hpp"
#include "terrain/TerrainSplatmap.hpp"

namespace Caffeine::Terrain {

struct TerrainSplatBrushSettings {
    u32 targetLayer = 0;
    f32 radius = 6.0f;
    f32 strength = 0.2f;
};

struct TerrainVertexBounds {
    u32 minX = 0;
    u32 minZ = 0;
    u32 maxX = 0;
    u32 maxZ = 0;
    bool valid = false;
};

class TerrainSplatPainter {
public:
    static void applyBrush(TerrainSplatmap& splatmap,
                           const ECS::TerrainComponent& settings,
                           const Mat4& worldMatrix,
                           const Vec3& centerWorld,
                           const TerrainSplatBrushSettings& brush,
                           f32 deltaTime,
                           TerrainVertexBounds* outBounds = nullptr);
};

}  // namespace Caffeine::Terrain
