#include "terrain/TerrainSculptor.hpp"

#include <algorithm>
#include <cmath>

namespace Caffeine::Terrain {
namespace {

f32 smoothstep(f32 t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

bool intersectSlab(f32 origin, f32 direction, f32 minBound, f32 maxBound, f32& tEnter, f32& tExit) {
    if (std::abs(direction) < 1e-6f) {
        if (origin < minBound || origin > maxBound) return false;
        return true;
    }

    const f32 t0 = (minBound - origin) / direction;
    const f32 t1 = (maxBound - origin) / direction;
    const f32 tMin = std::min(t0, t1);
    const f32 tMax = std::max(t0, t1);
    tEnter = std::max(tEnter, tMin);
    tExit = std::min(tExit, tMax);
    return tEnter <= tExit;
}

}  // namespace

bool TerrainSculptor::raycast(const Mat4& worldMatrix,
                              const TerrainHeightmap& heightmap,
                              const ECS::TerrainComponent& settings,
                              const Vec3& rayOriginWorld,
                              const Vec3& rayDirWorld,
                              Vec3& hitWorldOut) {
    if (heightmap.empty()) return false;

    const Mat4 inv = worldMatrix.inverted();
    const Vec3 origin = inv.transformPoint(rayOriginWorld);
    Vec3 dir = inv.transformVector(rayDirWorld);
    const f32 dirLen = dir.length();
    if (dirLen < 1e-6f) return false;
    dir = dir / dirLen;

    const f32 halfX = settings.worldSizeX * 0.5f;
    const f32 halfZ = settings.worldSizeZ * 0.5f;
    const f32 maxY = settings.maxHeight + 1.0f;

    f32 tEnter = 0.0f;
    f32 tExit = 10000.0f;
    if (!intersectSlab(origin.x, dir.x, -halfX, halfX, tEnter, tExit)) return false;
    if (!intersectSlab(origin.y, dir.y, -1.0f, maxY, tEnter, tExit)) return false;
    if (!intersectSlab(origin.z, dir.z, -halfZ, halfZ, tEnter, tExit)) return false;
    if (tEnter > tExit || tExit < 0.0f) return false;

    tEnter = std::max(0.0f, tEnter);
    const f32 step = std::min(settings.worldSizeX, settings.worldSizeZ) /
                     static_cast<f32>(std::max(settings.resolutionX, settings.resolutionZ)) * 0.5f;

    f32 prevT = tEnter;
    Vec3 prevPoint = origin + dir * prevT;
    f32 prevU = (prevPoint.x + halfX) / settings.worldSizeX;
    f32 prevV = (prevPoint.z + halfZ) / settings.worldSizeZ;
    f32 prevTerrainY = heightmap.sampleBilinear(prevU, prevV) * settings.maxHeight;
    bool prevAbove = prevPoint.y >= prevTerrainY;

    for (f32 t = tEnter + step; t <= tExit + step; t += step) {
        const Vec3 point = origin + dir * t;
        if (point.x < -halfX || point.x > halfX || point.z < -halfZ || point.z > halfZ) {
            prevT = t;
            prevPoint = point;
            continue;
        }

        const f32 u = std::clamp((point.x + halfX) / settings.worldSizeX, 0.0f, 1.0f);
        const f32 v = std::clamp((point.z + halfZ) / settings.worldSizeZ, 0.0f, 1.0f);
        const f32 terrainY = heightmap.sampleBilinear(u, v) * settings.maxHeight;
        const bool above = point.y >= terrainY;

        if (prevAbove && !above) {
            const f32 denom = (prevPoint.y - point.y);
            const f32 alpha = std::abs(denom) > 1e-5f ? (prevPoint.y - prevTerrainY) / denom : 0.0f;
            const f32 hitT = prevT + (t - prevT) * std::clamp(alpha, 0.0f, 1.0f);
            const Vec3 hitLocal = origin + dir * hitT;
            const f32 hitU = std::clamp((hitLocal.x + halfX) / settings.worldSizeX, 0.0f, 1.0f);
            const f32 hitV = std::clamp((hitLocal.z + halfZ) / settings.worldSizeZ, 0.0f, 1.0f);
            const f32 hitY = heightmap.sampleBilinear(hitU, hitV) * settings.maxHeight;
            hitWorldOut = worldMatrix.transformPoint(Vec3(hitLocal.x, hitY, hitLocal.z));
            return true;
        }

        prevT = t;
        prevPoint = point;
        prevU = u;
        prevV = v;
        prevTerrainY = terrainY;
        prevAbove = above;
    }

    return false;
}

void TerrainSculptor::applyBrush(TerrainHeightmap& heightmap,
                                 const ECS::TerrainComponent& settings,
                                 const Mat4& worldMatrix,
                                 const Vec3& centerWorld,
                                 const TerrainBrushSettings& brush,
                                 f32 deltaTime,
                                 TerrainVertexBounds* outBounds) {
    if (heightmap.empty() || brush.radius <= 0.0f || brush.strength <= 0.0f) return;

    const Mat4 inv = worldMatrix.inverted();
    const Vec3 centerLocal = inv.transformPoint(centerWorld);

    const f32 halfX = settings.worldSizeX * 0.5f;
    const f32 halfZ = settings.worldSizeZ * 0.5f;
    const u32 resX = settings.resolutionX;
    const u32 resZ = settings.resolutionZ;
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

            f32 height = heightmap.sampleNormalized(x, z);
            switch (brush.mode) {
                case TerrainBrushMode::Raise:
                    height += amount;
                    break;
                case TerrainBrushMode::Lower:
                    height -= amount;
                    break;
                case TerrainBrushMode::Smooth: {
                    f32 sum = 0.0f;
                    u32 count = 0;
                    for (int dzOff = -1; dzOff <= 1; ++dzOff) {
                        for (int dxOff = -1; dxOff <= 1; ++dxOff) {
                            const int sx = static_cast<int>(x) + dxOff;
                            const int sz = static_cast<int>(z) + dzOff;
                            if (sx < 0 || sz < 0 ||
                                sx >= static_cast<int>(resX) || sz >= static_cast<int>(resZ)) {
                                continue;
                            }
                            sum += heightmap.sampleNormalized(static_cast<u32>(sx), static_cast<u32>(sz));
                            ++count;
                        }
                    }
                    if (count > 0) {
                        const f32 avg = sum / static_cast<f32>(count);
                        height += (avg - height) * amount;
                    }
                    break;
                }
            }
            heightmap.setNormalized(x, z, height);

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
