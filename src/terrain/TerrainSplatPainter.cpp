#include "terrain/TerrainSplatPainter.hpp"

#include <algorithm>
#include <cmath>

namespace Caffeine::Terrain {
namespace {

f32 smoothstep(f32 t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

Vec4 addLayerWeight(const Vec4& weights, u32 layer, f32 amount) {
    Vec4 result = weights;
    if (layer == 0) result.x += amount;
    else if (layer == 1) result.y += amount;
    else if (layer == 2) result.z += amount;
    else result.w += amount;

    const f32 sum = result.x + result.y + result.z + result.w;
    if (sum <= 1e-6f) return Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    return Vec4(result.x / sum, result.y / sum, result.z / sum, result.w / sum);
}

}  // namespace

void TerrainSplatPainter::applyBrush(TerrainSplatmap& splatmap,
                                     const ECS::TerrainComponent& settings,
                                     const Mat4& worldMatrix,
                                     const Vec3& centerWorld,
                                     const TerrainSplatBrushSettings& brush,
                                     f32 deltaTime,
                                     TerrainVertexBounds* outBounds) {
    if (splatmap.empty() || brush.radius <= 0.0f || brush.strength <= 0.0f) return;
    if (brush.targetLayer >= TerrainSplatmap::kLayerCount) return;

    const Mat4 inv = worldMatrix.inverted();
    const Vec3 centerLocal = inv.transformPoint(centerWorld);

    const f32 halfX = settings.worldSizeX * 0.5f;
    const f32 halfZ = settings.worldSizeZ * 0.5f;
    const u32 resX = splatmap.resolutionX();
    const u32 resZ = splatmap.resolutionZ();
    const f32 stepX = settings.worldSizeX / static_cast<f32>(std::max(1u, resX - 1));
    const f32 stepZ = settings.worldSizeZ / static_cast<f32>(std::max(1u, resZ - 1));
    const f32 radiusSq = brush.radius * brush.radius;
    const f32 frameStrength = brush.strength * std::max(deltaTime, 1.0f / 120.0f) * 60.0f;

    TerrainVertexBounds bounds;
    bounds.valid = false;

    for (u32 z = 0; z < resZ; ++z) {
        for (u32 x = 0; x < resX; ++x) {
            const f32 px = -halfX + static_cast<f32>(x) * stepX;
            const f32 pz = -halfZ + static_cast<f32>(z) * stepZ;
            const f32 dx = px - centerLocal.x;
            const f32 dz = pz - centerLocal.z;
            const f32 distSq = dx * dx + dz * dz;
            if (distSq > radiusSq) continue;

            const f32 dist = std::sqrt(distSq);
            const f32 falloff = smoothstep(1.0f - dist / brush.radius);
            const f32 amount = frameStrength * falloff;

            Vec4 weights = splatmap.sample(x, z);
            splatmap.set(x, z, addLayerWeight(weights, brush.targetLayer, amount));

            if (!bounds.valid) {
                bounds.minX = bounds.maxX = x;
                bounds.minZ = bounds.maxZ = z;
                bounds.valid = true;
            } else {
                bounds.minX = std::min(bounds.minX, x);
                bounds.minZ = std::min(bounds.minZ, z);
                bounds.maxX = std::max(bounds.maxX, x);
                bounds.maxZ = std::max(bounds.maxZ, z);
            }
        }
    }

    if (outBounds && bounds.valid) {
        *outBounds = bounds;
    }
}

}  // namespace Caffeine::Terrain
