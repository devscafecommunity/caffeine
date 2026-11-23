#include "terrain/edit/TerrainStroke.hpp"

#include <algorithm>
#include <cmath>

namespace Caffeine::Terrain {
namespace {

f32 smoothstep01(f32 t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

f32 falloffWeight(FalloffCurve curve, f32 t) {  // t = 1 at the centre, 0 at the edge
    t = std::clamp(t, 0.0f, 1.0f);
    switch (curve) {
        case FalloffCurve::Smooth: return smoothstep01(t);
        case FalloffCurve::Linear: return t;
        case FalloffCurve::Sharp: return t * t;
        case FalloffCurve::Constant: return t > 0.0f ? 1.0f : 0.0f;
    }
    return t;
}

/// Deterministic value in -1..1 from a vertex, a seed and the stamp's number.
f32 noiseValue(u32 x, u32 z, u64 seed, u32 stampIndex) {
    u64 h = seed ^ (static_cast<u64>(x) * 0x9E3779B97F4A7C15ull) ^ (static_cast<u64>(z) * 0xC2B2AE3D27D4EB4Full) ^
            (static_cast<u64>(stampIndex) * 0x165667B19E3779F9ull);
    h ^= h >> 33;
    h *= 0xFF51AFD7ED558CCDull;
    h ^= h >> 33;
    h *= 0xC4CEB9FE1A85EC53ull;
    h ^= h >> 33;
    return static_cast<f32>(h & 0xFFFFFFu) / 8388607.5f - 1.0f;
}

}  // namespace

f32 BrushStamp::sample(f32 u, f32 v) const {
    if (empty()) return 1.0f;
    if (u < -1.0f || u > 1.0f || v < -1.0f || v > 1.0f) return 0.0f;
    const f32 fx = (u * 0.5f + 0.5f) * static_cast<f32>(size - 1);
    const f32 fz = (v * 0.5f + 0.5f) * static_cast<f32>(size - 1);
    const u32 x0 = static_cast<u32>(fx), z0 = static_cast<u32>(fz);
    const u32 x1 = std::min(x0 + 1, size - 1), z1 = std::min(z0 + 1, size - 1);
    const f32 tx = fx - static_cast<f32>(x0), tz = fz - static_cast<f32>(z0);
    auto at = [&](u32 x, u32 z) { return alpha[static_cast<size_t>(z) * size + x]; };
    const f32 top = at(x0, z0) + (at(x1, z0) - at(x0, z0)) * tx;
    const f32 bottom = at(x0, z1) + (at(x1, z1) - at(x0, z1)) * tx;
    return std::clamp(top + (bottom - top) * tz, 0.0f, 1.0f);
}

size_t StrokeRecord::bytes() const {
    size_t total = 0;
    for (const TileDelta& delta : tiles) total += (delta.before.size() + delta.after.size()) * sizeof(f32);
    return total;
}

namespace {

void writeRect(TerrainHeightmap& map, const TileDelta& delta, const std::vector<f32>& values) {
    // A record made before the terrain was resized no longer fits: leave such a terrain alone.
    if (delta.rect.maxX >= map.resolutionX() || delta.rect.maxZ >= map.resolutionZ()) return;
    const u32 width = delta.rect.maxX - delta.rect.minX + 1;
    for (u32 z = delta.rect.minZ; z <= delta.rect.maxZ; ++z) {
        for (u32 x = delta.rect.minX; x <= delta.rect.maxX; ++x) {
            map.heights()[static_cast<size_t>(z) * map.resolutionX() + x] =
                values[static_cast<size_t>(z - delta.rect.minZ) * width + (x - delta.rect.minX)];
        }
    }
}

}  // namespace

TerrainVertexBounds StrokeRecord::applyBefore(TerrainHeightmap& heightmap) const {
    for (const TileDelta& delta : tiles) writeRect(heightmap, delta, delta.before);
    return region;
}

TerrainVertexBounds StrokeRecord::applyAfter(TerrainHeightmap& heightmap) const {
    for (const TileDelta& delta : tiles) writeRect(heightmap, delta, delta.after);
    return region;
}

TerrainStroke::TerrainStroke(TerrainHeightmap& heightmap, const ECS::TerrainComponent& settings, StrokeBrush brush)
    : m_map(heightmap),
      m_grid(heightmap.resolutionX(), heightmap.resolutionZ(), settings.worldSizeX, settings.worldSizeZ),
      m_sizeX(settings.worldSizeX),
      m_sizeZ(settings.worldSizeZ),
      m_maxHeight(std::max(settings.maxHeight, 0.001f)),
      m_brush(std::move(brush)),
      m_dirty(m_grid) {
    m_brush.radiusM = std::max(m_brush.radiusM, 0.01f);
    if (m_brush.spacingM <= 0.0f) m_brush.spacingM = m_brush.radiusM * 0.25f;
    m_brush.spacingM = std::max(m_brush.spacingM, 0.001f);
}

u32 TerrainStroke::addPoint(const StrokePoint& point) {
    if (m_finished || m_map.empty() || !m_grid.valid()) return 0;
    if (!m_hasLast) {
        m_hasLast = true;
        m_lastX = point.x;
        m_lastZ = point.z;
        m_lastPressure = point.pressure;
        m_carry = 0.0;
        stamp(point.x, point.z, point.pressure);
        return 1;
    }
    const f64 dx = point.x - m_lastX;
    const f64 dz = point.z - m_lastZ;
    const f64 length = std::sqrt(dx * dx + dz * dz);
    u32 laid = 0;
    if (length > 0.0) {
        const f64 spacing = static_cast<f64>(m_brush.spacingM);
        f64 along = spacing - m_carry;  // distance from the last point to the next stamp
        f64 lastStamp = -m_carry;
        while (along <= length) {
            const f64 t = along / length;
            stamp(m_lastX + dx * t, m_lastZ + dz * t,
                  m_lastPressure + (point.pressure - m_lastPressure) * static_cast<f32>(t));
            ++laid;
            lastStamp = along;
            along += spacing;
        }
        m_carry = length - lastStamp;
    }
    m_lastX = point.x;
    m_lastZ = point.z;
    m_lastPressure = point.pressure;
    return laid;
}

void TerrainStroke::touch(TileCoord tile) {
    const u32 key = m_grid.index(tile);
    if (m_touched.count(key)) return;
    TileDelta delta;
    delta.tile = tile;
    delta.rect = m_grid.owned(tile);
    const u32 width = delta.rect.maxX - delta.rect.minX + 1;
    const u32 height = delta.rect.maxZ - delta.rect.minZ + 1;
    delta.before.resize(static_cast<size_t>(width) * height);
    for (u32 z = 0; z < height; ++z) {
        for (u32 x = 0; x < width; ++x) {
            delta.before[static_cast<size_t>(z) * width + x] =
                m_map.heights()[static_cast<size_t>(delta.rect.minZ + z) * m_map.resolutionX() + delta.rect.minX + x];
        }
    }
    m_touched.emplace(key, std::move(delta));
}

void TerrainStroke::stamp(f64 x, f64 z, f32 pressure) {
    const u32 resX = m_map.resolutionX();
    const u32 resZ = m_map.resolutionZ();
    const f64 stepX = m_grid.cellSizeX();
    const f64 stepZ = m_grid.cellSizeZ();
    const f64 gridX = (x + m_sizeX * 0.5) / stepX;
    const f64 gridZ = (z + m_sizeZ * 0.5) / stepZ;
    const f64 radius = static_cast<f64>(m_brush.radiusM);

    const i64 minX = std::max<i64>(0, static_cast<i64>(std::floor(gridX - radius / stepX)));
    const i64 maxX = std::min<i64>(static_cast<i64>(resX) - 1, static_cast<i64>(std::ceil(gridX + radius / stepX)));
    const i64 minZ = std::max<i64>(0, static_cast<i64>(std::floor(gridZ - radius / stepZ)));
    const i64 maxZ = std::min<i64>(static_cast<i64>(resZ) - 1, static_cast<i64>(std::ceil(gridZ + radius / stepZ)));
    if (minX > maxX || minZ > maxZ) {
        ++m_stamps;
        return;
    }

    if (m_brush.mode == TerrainBrushMode::Flatten && !m_flattenSet) {
        m_flattenTarget = m_brush.targetHeightM >= 0.0f
                              ? m_brush.targetHeightM / m_maxHeight
                              : m_map.sampleBilinear(static_cast<f32>(std::clamp((x + m_sizeX * 0.5) / m_sizeX, 0.0, 1.0)),
                                                     static_cast<f32>(std::clamp((z + m_sizeZ * 0.5) / m_sizeZ, 0.0, 1.0)));
        m_flattenSet = true;
    }

    // Smooth reads the neighbours: from a copy of the region, so the result does not depend on the order of vertices.
    std::vector<f32> source;
    const i64 srcMinX = std::max<i64>(0, minX - 1), srcMaxX = std::min<i64>(resX - 1, maxX + 1);
    const i64 srcMinZ = std::max<i64>(0, minZ - 1), srcMaxZ = std::min<i64>(resZ - 1, maxZ + 1);
    const i64 srcW = srcMaxX - srcMinX + 1;
    if (m_brush.mode == TerrainBrushMode::Smooth) {
        source.resize(static_cast<size_t>(srcW * (srcMaxZ - srcMinZ + 1)));
        for (i64 sz = srcMinZ; sz <= srcMaxZ; ++sz) {
            for (i64 sx = srcMinX; sx <= srcMaxX; ++sx) {
                source[static_cast<size_t>((sz - srcMinZ) * srcW + (sx - srcMinX))] =
                    m_map.heights()[static_cast<size_t>(sz) * resX + static_cast<size_t>(sx)];
            }
        }
    }

    const f32 rad = m_brush.rotationDegrees * 3.14159265f / 180.0f;
    const f32 cosR = std::cos(rad), sinR = std::sin(rad);
    const f32 pressureFactor = 1.0f + (std::clamp(pressure, 0.0f, 1.0f) - 1.0f) * std::clamp(m_brush.pressureToStrength, 0.0f, 1.0f);
    const f32 strength = m_brush.strength * pressureFactor;

    TerrainVertexBounds changed;
    TileCoord lastTile{-1, -1};
    for (i64 vz = minZ; vz <= maxZ; ++vz) {
        for (i64 vx = minX; vx <= maxX; ++vx) {
            const f64 dx = (static_cast<f64>(vx) - gridX) * stepX;
            const f64 dz = (static_cast<f64>(vz) - gridZ) * stepZ;
            const f64 dist = std::sqrt(dx * dx + dz * dz);
            if (dist > radius) continue;
            f32 weight = falloffWeight(m_brush.falloff, static_cast<f32>(1.0 - dist / radius));
            if (!m_brush.stamp.empty()) {
                const f32 u = static_cast<f32>(dx / radius), v = static_cast<f32>(dz / radius);
                weight *= m_brush.stamp.sample(u * cosR + v * sinR, -u * sinR + v * cosR);
            }
            if (weight <= 0.0f) continue;

            const u32 ux = static_cast<u32>(vx), uz = static_cast<u32>(vz);
            const TileCoord tile = m_grid.tileOfVertex(ux, uz);
            if (!(tile == lastTile)) {
                touch(tile);
                lastTile = tile;
            }
            f32& cell = m_map.heights()[static_cast<size_t>(uz) * resX + ux];
            f32 height = cell;
            switch (m_brush.mode) {
                case TerrainBrushMode::Raise: height += strength * weight / m_maxHeight; break;
                case TerrainBrushMode::Lower: height -= strength * weight / m_maxHeight; break;
                case TerrainBrushMode::Smooth: {
                    f32 sum = 0.0f;
                    u32 count = 0;
                    for (i64 oz = -1; oz <= 1; ++oz) {
                        for (i64 ox = -1; ox <= 1; ++ox) {
                            const i64 sx = vx + ox, sz = vz + oz;
                            if (sx < srcMinX || sx > srcMaxX || sz < srcMinZ || sz > srcMaxZ) continue;
                            sum += source[static_cast<size_t>((sz - srcMinZ) * srcW + (sx - srcMinX))];
                            ++count;
                        }
                    }
                    if (count > 0) height += (sum / static_cast<f32>(count) - height) * std::clamp(strength * weight, 0.0f, 1.0f);
                    break;
                }
                case TerrainBrushMode::Flatten:
                    height += (m_flattenTarget - height) * std::clamp(strength * weight, 0.0f, 1.0f);
                    break;
                case TerrainBrushMode::Noise:
                    height += noiseValue(ux, uz, m_brush.seed, m_stamps) * strength * weight / m_maxHeight;
                    break;
            }
            cell = std::clamp(height, 0.0f, 1.0f);

            if (!changed.valid) {
                changed.minX = changed.maxX = ux;
                changed.minZ = changed.maxZ = uz;
                changed.valid = true;
            } else {
                changed.minX = std::min(changed.minX, ux);
                changed.maxX = std::max(changed.maxX, ux);
                changed.minZ = std::min(changed.minZ, uz);
                changed.maxZ = std::max(changed.maxZ, uz);
            }
        }
    }
    m_dirty.mark(changed);
    ++m_stamps;
}

StrokeRecord TerrainStroke::finish() {
    m_finished = true;
    StrokeRecord record;
    record.stamps = m_stamps;
    std::vector<u32> keys;
    keys.reserve(m_touched.size());
    for (const auto& [key, _] : m_touched) keys.push_back(key);
    std::sort(keys.begin(), keys.end());
    for (const u32 key : keys) {
        TileDelta delta = std::move(m_touched[key]);
        const u32 width = delta.rect.maxX - delta.rect.minX + 1;
        const u32 height = delta.rect.maxZ - delta.rect.minZ + 1;
        delta.after.resize(delta.before.size());
        bool same = true;
        for (u32 z = 0; z < height; ++z) {
            for (u32 x = 0; x < width; ++x) {
                const f32 now = m_map.heights()[static_cast<size_t>(delta.rect.minZ + z) * m_map.resolutionX() + delta.rect.minX + x];
                delta.after[static_cast<size_t>(z) * width + x] = now;
                if (now != delta.before[static_cast<size_t>(z) * width + x]) same = false;
            }
        }
        if (same) continue;  // the stroke passed over it and left it as it was
        // The vertices that really differ, as a tight rectangle: the mesh and the GI grow it by one themselves.
        for (u32 z = 0; z < height; ++z) {
            for (u32 x = 0; x < width; ++x) {
                const size_t i = static_cast<size_t>(z) * width + x;
                if (delta.after[i] == delta.before[i]) continue;
                const u32 vx = delta.rect.minX + x, vz = delta.rect.minZ + z;
                if (!record.region.valid) {
                    record.region.minX = record.region.maxX = vx;
                    record.region.minZ = record.region.maxZ = vz;
                    record.region.valid = true;
                } else {
                    record.region.minX = std::min(record.region.minX, vx);
                    record.region.maxX = std::max(record.region.maxX, vx);
                    record.region.minZ = std::min(record.region.minZ, vz);
                    record.region.maxZ = std::max(record.region.maxZ, vz);
                }
            }
        }
        record.tiles.push_back(std::move(delta));
    }
    m_touched.clear();
    return record;
}

}  // namespace Caffeine::Terrain
