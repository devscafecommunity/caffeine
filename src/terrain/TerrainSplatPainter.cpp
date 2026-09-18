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
    const f32 frameStrength = brush.strength * std::max(deltaTime, 1.0f / 120.0f) * 60.0f;

    const f32 gridX = (centerLocal.x + halfX) / stepX;
    const f32 gridZ = (centerLocal.z + halfZ) / stepZ;
    const f32 radiusCellsX = brush.radius / stepX;
    const f32 radiusCellsZ = brush.radius / stepZ;

    const i32 minX = std::max(0, static_cast<i32>(std::floor(gridX - radiusCellsX)) - 1);
    const i32 maxX =
        std::min(static_cast<i32>(resX) - 1, static_cast<i32>(std::ceil(gridX + radiusCellsX)) + 1);
    const i32 minZ = std::max(0, static_cast<i32>(std::floor(gridZ - radiusCellsZ)) - 1);
    const i32 maxZ =
        std::min(static_cast<i32>(resZ) - 1, static_cast<i32>(std::ceil(gridZ + radiusCellsZ)) + 1);

    TerrainVertexBounds bounds;
    bounds.valid = false;

    for (i32 z = minZ; z <= maxZ; ++z) {
        for (i32 x = minX; x <= maxX; ++x) {
            const f32 px = -halfX + static_cast<f32>(x) * stepX;
            const f32 pz = -halfZ + static_cast<f32>(z) * stepZ;
            const f32 dx = px - centerLocal.x;
            const f32 dz = pz - centerLocal.z;
            const f32 dist = std::sqrt(dx * dx + dz * dz);
            if (dist > brush.radius) continue;

            const f32 falloff = smoothstep(1.0f - dist / brush.radius);
            const f32 amount = frameStrength * falloff;

            const u32 ux = static_cast<u32>(x);
            const u32 uz = static_cast<u32>(z);
            Vec4 weights = splatmap.sample(ux, uz);
            splatmap.set(ux, uz, addLayerWeight(weights, brush.targetLayer, amount));

            if (!bounds.valid) {
                bounds.minX = bounds.maxX = ux;
                bounds.minZ = bounds.maxZ = uz;
                bounds.valid = true;
            } else {
                bounds.minX = std::min(bounds.minX, ux);
                bounds.minZ = std::min(bounds.minZ, uz);
                bounds.maxX = std::max(bounds.maxX, ux);
                bounds.maxZ = std::max(bounds.maxZ, uz);
            }
        }
    }

    if (outBounds && bounds.valid) {
        *outBounds = bounds;
    }
}

}  // namespace Caffeine::Terrain
