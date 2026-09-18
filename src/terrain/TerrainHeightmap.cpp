#include "terrain/TerrainHeightmap.hpp"

#include <algorithm>
#include <cmath>

namespace Caffeine::Terrain {
namespace {

f32 hashNoise(u32 x, u32 z, u32 seed) {
    u32 n = x * 374761393u + z * 668265263u + seed * 362437u;
    n = (n ^ (n >> 13u)) * 1274126177u;
    n ^= n >> 16u;
    return static_cast<f32>(n & 0xFFFFu) / 65535.0f;
}

}  // namespace

TerrainHeightmap::TerrainHeightmap(u32 resolutionX, u32 resolutionZ) {
    resize(resolutionX, resolutionZ);
}

void TerrainHeightmap::resize(u32 resolutionX, u32 resolutionZ) {
    m_resolutionX = std::max(2u, resolutionX);
    m_resolutionZ = std::max(2u, resolutionZ);
    m_heights.assign(static_cast<size_t>(m_resolutionX) * static_cast<size_t>(m_resolutionZ), 0.0f);
}

void TerrainHeightmap::fill(f32 normalizedHeight) {
    normalizedHeight = std::clamp(normalizedHeight, 0.0f, 1.0f);
    std::fill(m_heights.begin(), m_heights.end(), normalizedHeight);
}

u32 TerrainHeightmap::index(u32 x, u32 z) const {
    return z * m_resolutionX + x;
}

f32 TerrainHeightmap::sampleNormalized(u32 x, u32 z) const {
    if (m_heights.empty()) return 0.0f;
    x = std::min(x, m_resolutionX - 1);
    z = std::min(z, m_resolutionZ - 1);
    return m_heights[index(x, z)];
}

void TerrainHeightmap::setNormalized(u32 x, u32 z, f32 value) {
    if (m_heights.empty()) return;
    x = std::min(x, m_resolutionX - 1);
    z = std::min(z, m_resolutionZ - 1);
    m_heights[index(x, z)] = std::clamp(value, 0.0f, 1.0f);
}

f32 TerrainHeightmap::sampleBilinear(f32 u, f32 v) const {
    if (m_heights.empty()) return 0.0f;

    u = std::clamp(u, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);

    const f32 fx = u * static_cast<f32>(m_resolutionX - 1);
    const f32 fz = v * static_cast<f32>(m_resolutionZ - 1);

    const u32 x0 = static_cast<u32>(fx);
    const u32 z0 = static_cast<u32>(fz);
    const u32 x1 = std::min(x0 + 1, m_resolutionX - 1);
    const u32 z1 = std::min(z0 + 1, m_resolutionZ - 1);

    const f32 tx = fx - static_cast<f32>(x0);
    const f32 tz = fz - static_cast<f32>(z0);

    const f32 h00 = sampleNormalized(x0, z0);
    const f32 h10 = sampleNormalized(x1, z0);
    const f32 h01 = sampleNormalized(x0, z1);
    const f32 h11 = sampleNormalized(x1, z1);

    const f32 hx0 = h00 + (h10 - h00) * tx;
    const f32 hx1 = h01 + (h11 - h01) * tx;
    return hx0 + (hx1 - hx0) * tz;
}

Vec3 TerrainHeightmap::sampleNormalBilinear(f32 u, f32 v, f32 worldSizeX, f32 worldSizeZ,
                                            f32 maxHeight) const {
    const f32 cellX = worldSizeX / static_cast<f32>(std::max(1u, m_resolutionX - 1));
    const f32 cellZ = worldSizeZ / static_cast<f32>(std::max(1u, m_resolutionZ - 1));
    const f32 du = cellX / std::max(worldSizeX, 0.0001f);
    const f32 dv = cellZ / std::max(worldSizeZ, 0.0001f);

    const f32 hL = sampleBilinear(std::max(0.0f, u - du), v);
    const f32 hR = sampleBilinear(std::min(1.0f, u + du), v);
    const f32 hD = sampleBilinear(u, std::max(0.0f, v - dv));
    const f32 hU = sampleBilinear(u, std::min(1.0f, v + dv));

    const f32 dhdx = (hR - hL) * maxHeight / std::max(cellX * 2.0f, 0.0001f);
    const f32 dhdz = (hU - hD) * maxHeight / std::max(cellZ * 2.0f, 0.0001f);

    Vec3 normal(-dhdx, 1.0f, -dhdz);
    const f32 len = normal.length();
    return len > 1e-6f ? normal / len : Vec3(0.0f, 1.0f, 0.0f);
}

void TerrainHeightmap::applyProceduralNoise(f32 amplitude, u32 seed) {
    amplitude = std::clamp(amplitude, 0.0f, 1.0f);
    if (m_heights.empty()) return;

    for (u32 z = 0; z < m_resolutionZ; ++z) {
        for (u32 x = 0; x < m_resolutionX; ++x) {
            const f32 base = sampleNormalized(x, z);
            const f32 noise = hashNoise(x, z, seed);
            setNormalized(x, z, std::clamp(base + (noise - 0.5f) * amplitude, 0.0f, 1.0f));
        }
    }
}

}  // namespace Caffeine::Terrain
