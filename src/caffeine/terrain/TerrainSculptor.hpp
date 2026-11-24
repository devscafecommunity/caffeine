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
    // Terrain editing, phase 1 (`TerrainStroke`; the legacy sculptor below only does the first five).
    Ramp,            ///< grade a slope from where the stroke began to where the cursor is
    Terrace,         ///< round heights toward steps of `stepM`
    Pinch,           ///< pull the relief toward the stamp's centre
    Bulge,           ///< push the relief away from the stamp's centre
    StampAbsolute,   ///< replace the height under the stamp by the stamp's shape on top of the ground at its centre
    StampMax,        ///< keep the higher of the ground and the stamp's shape
    StampMin,        ///< keep the lower of the ground and the stamp's shape, cut down from the centre
    Clone,           ///< copy the relief from `offset` metres away
    Smudge,          ///< drag the relief along the stroke
    ErodeThermal,    ///< slopes steeper than the talus angle slide down
    ErodeHydraulic,  ///< droplets carry material downhill
};

struct TerrainBrushSettings {
    TerrainBrushMode mode = TerrainBrushMode::Raise;
    f32 radius = 6.0f;
    f32 strength = 0.12f;
    /// Normalized height used by the Flatten brush. Negative samples the
    /// height under the brush centre instead.
    f32 targetHeight = -1.0f;
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
