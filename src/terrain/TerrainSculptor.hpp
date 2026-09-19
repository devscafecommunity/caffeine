#pragma once

#include "core/Types.hpp"
#include "ecs/TerrainComponents.hpp"
#include "math/Mat4.hpp"
#include "math/Vec3.hpp"
#include "terrain/TerrainHeightmap.hpp"
#include "terrain/TerrainSplatPainter.hpp"

namespace Caffeine::Terrain {

enum class TerrainBrushMode : u8 {
    Raise,
    Lower,
    Smooth,
    Flatten,
    Noise,
};

struct TerrainBrushSettings {
    TerrainBrushMode mode = TerrainBrushMode::Raise;
    f32 radius = 6.0f;
    f32 strength = 0.12f;
};

class TerrainSculptor {
public:
    static bool raycast(const Mat4& worldMatrix,
                        const TerrainHeightmap& heightmap,
                        const ECS::TerrainComponent& settings,
                        const Vec3& rayOriginWorld,
                        const Vec3& rayDirWorld,
                        Vec3& hitWorldOut);

    static void applyBrush(TerrainHeightmap& heightmap,
                           const ECS::TerrainComponent& settings,
                           const Mat4& worldMatrix,
                           const Vec3& centerWorld,
                           const TerrainBrushSettings& brush,
                           f32 deltaTime,
                           TerrainVertexBounds* outBounds = nullptr);
};

}  // namespace Caffeine::Terrain
