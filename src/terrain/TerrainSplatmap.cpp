#include "terrain/TerrainSplatmap.hpp"

#include <algorithm>
#include <cmath>

namespace Caffeine::Terrain {
namespace {

Vec4 normalizeWeights(Vec4 w) {
    const f32 sum = w.x + w.y + w.z + w.w;
    if (sum <= 1e-6f) return Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    return Vec4(w.x / sum, w.y / sum, w.z / sum, w.w / sum);
}

}  // namespace

TerrainSplatmap::TerrainSplatmap(u32 resolutionX, u32 resolutionZ) {
    resize(resolutionX, resolutionZ);
}

void TerrainSplatmap::resize(u32 resolutionX, u32 resolutionZ) {
    m_resolutionX = std::max(2u, resolutionX);
    m_resolutionZ = std::max(2u, resolutionZ);
    m_weights.assign(static_cast<size_t>(m_resolutionX) * static_cast<size_t>(m_resolutionZ),
                     Vec4(0.0f, 0.0f, 0.0f, 1.0f));
}

void TerrainSplatmap::fillLayer(u32 layer, f32 weight) {
    weight = std::clamp(weight, 0.0f, 1.0f);
    Vec4 fill(0.0f, 0.0f, 0.0f, 0.0f);
    if (layer == 0) fill.x = weight;
    else if (layer == 1) fill.y = weight;
    else if (layer == 2) fill.z = weight;
    else fill.w = weight;
    fill = normalizeWeights(fill);
    std::fill(m_weights.begin(), m_weights.end(), fill);
}

u32 TerrainSplatmap::index(u32 x, u32 z) const {
    return z * m_resolutionX + x;
}

Vec4 TerrainSplatmap::sample(u32 x, u32 z) const {
    if (m_weights.empty()) return Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    x = std::min(x, m_resolutionX - 1);
    z = std::min(z, m_resolutionZ - 1);
    return m_weights[index(x, z)];
}

void TerrainSplatmap::set(u32 x, u32 z, const Vec4& weights) {
    if (m_weights.empty()) return;
    x = std::min(x, m_resolutionX - 1);
    z = std::min(z, m_resolutionZ - 1);
    m_weights[index(x, z)] = normalizeWeights(weights);
}

Vec4 TerrainSplatmap::sampleBilinear(f32 u, f32 v) const {
    if (m_weights.empty()) return Vec4(0.0f, 0.0f, 0.0f, 1.0f);

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

    const Vec4 h00 = sample(x0, z0);
    const Vec4 h10 = sample(x1, z0);
    const Vec4 h01 = sample(x0, z1);
    const Vec4 h11 = sample(x1, z1);

    const Vec4 hx0 = h00 + (h10 - h00) * tx;
    const Vec4 hx1 = h01 + (h11 - h01) * tx;
    return normalizeWeights(hx0 + (hx1 - hx0) * tz);
}

Vec4 TerrainSplatmap::sampleWorldXZ(f32 localX, f32 localZ, f32 worldSizeX, f32 worldSizeZ) const {
    const f32 halfX = worldSizeX * 0.5f;
    const f32 halfZ = worldSizeZ * 0.5f;
    const f32 u = std::clamp((localX + halfX) / std::max(worldSizeX, 0.0001f), 0.0f, 1.0f);
    const f32 v = std::clamp((localZ + halfZ) / std::max(worldSizeZ, 0.0001f), 0.0f, 1.0f);
    return sampleBilinear(u, v);
}

}  // namespace Caffeine::Terrain
