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
        m_pointX = point.x;
        m_pointZ = point.z;
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
        m_dirX = static_cast<f32>(dx / length);
        m_dirZ = static_cast<f32>(dz / length);
        m_pointX = point.x;
        m_pointZ = point.z;
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

namespace {

/// A deterministic number in 0..1 for a stamp (its index in the stroke) and a purpose.
f32 unitHash(u64 seed, u32 index, u32 salt) {
    u64 h = seed ^ (static_cast<u64>(index) * 0x9E3779B97F4A7C15ull) ^ (static_cast<u64>(salt) * 0xC2B2AE3D27D4EB4Full);
    h ^= h >> 33;
    h *= 0xFF51AFD7ED558CCDull;
    h ^= h >> 33;
    h *= 0xC4CEB9FE1A85EC53ull;
    h ^= h >> 33;
    return static_cast<f32>(h & 0xFFFFFFu) / 16777216.0f;
}

/// A copy of a rectangle of heights, read while the live ones change (smoothing, relief that moves, droplets).
struct Snapshot {
    i64 minX = 0, minZ = 0, width = 0, height = 0;
    std::vector<f32> data;

    f32 at(i64 x, i64 z) const {
        x = std::clamp<i64>(x, minX, minX + width - 1);
        z = std::clamp<i64>(z, minZ, minZ + height - 1);
        return data[static_cast<size_t>((z - minZ) * width + (x - minX))];
    }
    f32 bilinear(f64 gx, f64 gz) const {
        const f64 fx = std::floor(gx), fz = std::floor(gz);
        const f32 tx = static_cast<f32>(gx - fx), tz = static_cast<f32>(gz - fz);
        const i64 x0 = static_cast<i64>(fx), z0 = static_cast<i64>(fz);
        const f32 top = at(x0, z0) + (at(x0 + 1, z0) - at(x0, z0)) * tx;
        const f32 bottom = at(x0, z0 + 1) + (at(x0 + 1, z0 + 1) - at(x0, z0 + 1)) * tx;
        return top + (bottom - top) * tz;
    }
};

Snapshot takeSnapshot(const TerrainHeightmap& map, i64 minX, i64 minZ, i64 maxX, i64 maxZ) {
    Snapshot snap;
    snap.minX = std::max<i64>(0, minX);
    snap.minZ = std::max<i64>(0, minZ);
    maxX = std::min<i64>(static_cast<i64>(map.resolutionX()) - 1, maxX);
    maxZ = std::min<i64>(static_cast<i64>(map.resolutionZ()) - 1, maxZ);
    snap.width = std::max<i64>(1, maxX - snap.minX + 1);
    snap.height = std::max<i64>(1, maxZ - snap.minZ + 1);
    snap.data.resize(static_cast<size_t>(snap.width * snap.height));
    for (i64 z = 0; z < snap.height; ++z) {
        for (i64 x = 0; x < snap.width; ++x) {
            snap.data[static_cast<size_t>(z * snap.width + x)] =
                map.heights()[static_cast<size_t>(snap.minZ + z) * map.resolutionX() + static_cast<size_t>(snap.minX + x)];
        }
    }
    return snap;
}

}  // namespace

f32 TerrainStroke::weightAt(const StampParams& params, f64 dx, f64 dz, f64 dist, i64 vx, i64 vz) const {
    f32 weight = falloffWeight(m_brush.falloff, static_cast<f32>(1.0 - dist / params.radius));
    if (!m_brush.stamp.empty()) {
        const f32 u = static_cast<f32>(dx / params.radius) * params.flipU;
        const f32 v = static_cast<f32>(dz / params.radius) * params.flipV;
        const f32 cosR = std::cos(params.rotation), sinR = std::sin(params.rotation);
        f32 shape = m_brush.stamp.sample(u * cosR + v * sinR, -u * sinR + v * cosR);
        if (m_brush.signedStamp) shape = shape * 2.0f - 1.0f;
        weight *= shape;
    }
    if (!m_brush.protect.empty() && weight != 0.0f) {
        const TerrainFrame frame{&m_map, static_cast<f32>(m_sizeX), static_cast<f32>(m_sizeZ), m_maxHeight};
        const f32 x = static_cast<f32>(static_cast<f64>(vx) * m_grid.cellSizeX());
        const f32 z = static_cast<f32>(static_cast<f64>(vz) * m_grid.cellSizeZ());
        for (const BiomeTerm& term : m_brush.protect) {
            f32 value = 0.0f;
            // An unknown mask protects everything (the same rule as the biome table: nothing is placed on a name that
            // does not exist).
            if (!m_masks) {
                const TerrainMasks builtin;
                if (!builtin.sample(frame, term.mask, x, z, value)) return 0.0f;
            } else if (!m_masks->sample(frame, term.mask, x, z, value)) {
                return 0.0f;
            }
            weight *= term.weight(value);
            if (weight == 0.0f) return 0.0f;
        }
    }
    return weight;
}

void TerrainStroke::stamp(f64 x, f64 z, f32 pressure) {
    const u32 index = m_logical++;
    const f32 pressureFactor = 1.0f + (std::clamp(pressure, 0.0f, 1.0f) - 1.0f) * std::clamp(m_brush.pressureToStrength, 0.0f, 1.0f);

    StampParams base;
    base.x = x;
    base.z = z;
    base.radius = static_cast<f64>(m_brush.radiusM);
    base.rotation = m_brush.rotationDegrees * 3.14159265f / 180.0f;
    base.strength = m_brush.strength * pressureFactor;
    base.dirX = m_dirX;
    base.dirZ = m_dirZ;
    base.offsetX = m_brush.offsetX;
    base.offsetZ = m_brush.offsetZ;

    // Variation: the same stroke and seed always vary the same way.
    if (m_brush.positionJitter > 0.0f) {
        base.x += (unitHash(m_brush.seed, index, 11) * 2.0f - 1.0f) * m_brush.positionJitter * base.radius * 0.5;
        base.z += (unitHash(m_brush.seed, index, 12) * 2.0f - 1.0f) * m_brush.positionJitter * base.radius * 0.5;
    }
    if (m_brush.rotationJitterDegrees > 0.0f) {
        base.rotation += (unitHash(m_brush.seed, index, 13) * 2.0f - 1.0f) * m_brush.rotationJitterDegrees * 3.14159265f / 180.0f;
    }
    if (m_brush.sizeJitter > 0.0f) {
        base.radius *= 1.0 + (unitHash(m_brush.seed, index, 14) * 2.0f - 1.0f) * m_brush.sizeJitter * 0.5;
        base.radius = std::max(base.radius, 0.01);
    }
    if (m_brush.opacityJitter > 0.0f) {
        base.strength *= 1.0f - unitHash(m_brush.seed, index, 15) * std::clamp(m_brush.opacityJitter, 0.0f, 1.0f);
    }

    if (m_brush.symmetry == Symmetry::None) {
        applyStamp(base);
        return;
    }
    const f64 cx = m_brush.symmetryCenterX, cz = m_brush.symmetryCenterZ;
    std::vector<StampParams> copies;
    copies.push_back(base);
    auto mirrored = [&](bool flipX, bool flipZ) {
        StampParams copy = base;
        if (flipX) {
            copy.x = 2.0 * cx - base.x;
            copy.flipU = -copy.flipU;
            copy.dirX = -copy.dirX;
            copy.offsetX = -copy.offsetX;
        }
        if (flipZ) {
            copy.z = 2.0 * cz - base.z;
            copy.flipV = -copy.flipV;
            copy.dirZ = -copy.dirZ;
            copy.offsetZ = -copy.offsetZ;
        }
        // A mirror turns the stamp the other way.
        if (flipX != flipZ) copy.rotation = -copy.rotation;
        copies.push_back(copy);
    };
    switch (m_brush.symmetry) {
        case Symmetry::MirrorX: mirrored(true, false); break;
        case Symmetry::MirrorZ: mirrored(false, true); break;
        case Symmetry::MirrorBoth:
            mirrored(true, false);
            mirrored(false, true);
            mirrored(true, true);
            break;
        case Symmetry::Radial: {
            const u32 count = std::clamp(m_brush.radialCount, 2u, 16u);
            for (u32 k = 1; k < count; ++k) {
                const f32 angle = 6.28318531f * static_cast<f32>(k) / static_cast<f32>(count);
                const f32 c = std::cos(angle), s = std::sin(angle);
                StampParams copy = base;
                const f64 rx = base.x - cx, rz = base.z - cz;
                copy.x = cx + rx * c - rz * s;
                copy.z = cz + rx * s + rz * c;
                copy.rotation = base.rotation + angle;
                copy.dirX = base.dirX * c - base.dirZ * s;
                copy.dirZ = base.dirX * s + base.dirZ * c;
                copy.offsetX = base.offsetX * c - base.offsetZ * s;
                copy.offsetZ = base.offsetX * s + base.offsetZ * c;
                copies.push_back(copy);
            }
            break;
        }
        case Symmetry::None: break;
    }
    // Copies that land on the same spot (a stamp on the axis) are laid once.
    for (usize i = 0; i < copies.size(); ++i) {
        bool duplicate = false;
        for (usize j = 0; j < i && !duplicate; ++j) {
            // Without an image a mirrored stamp is the same stamp; with one, the mirror image is a different shape.
            duplicate = std::fabs(copies[i].x - copies[j].x) < 1e-6 && std::fabs(copies[i].z - copies[j].z) < 1e-6 &&
                        (m_brush.stamp.empty() || (copies[i].flipU == copies[j].flipU && copies[i].flipV == copies[j].flipV));
        }
        if (!duplicate) applyStamp(copies[i]);
    }
}

void TerrainStroke::applyStamp(const StampParams& params) {
    const u32 resX = m_map.resolutionX();
    const u32 resZ = m_map.resolutionZ();
    const f64 stepX = m_grid.cellSizeX();
    const f64 stepZ = m_grid.cellSizeZ();
    const f64 gridX = (params.x + m_sizeX * 0.5) / stepX;
    const f64 gridZ = (params.z + m_sizeZ * 0.5) / stepZ;
    const f64 radius = params.radius;

    const i64 minX = std::max<i64>(0, static_cast<i64>(std::floor(gridX - radius / stepX)));
    const i64 maxX = std::min<i64>(static_cast<i64>(resX) - 1, static_cast<i64>(std::ceil(gridX + radius / stepX)));
    const i64 minZ = std::max<i64>(0, static_cast<i64>(std::floor(gridZ - radius / stepZ)));
    const i64 maxZ = std::min<i64>(static_cast<i64>(resZ) - 1, static_cast<i64>(std::ceil(gridZ + radius / stepZ)));
    if (minX > maxX || minZ > maxZ) {
        ++m_stamps;
        return;
    }
    const TerrainBrushMode mode = m_brush.mode;
    const f32 strength = params.strength;

    if (mode == TerrainBrushMode::ErodeThermal || mode == TerrainBrushMode::ErodeHydraulic) {
        erode(params, mode == TerrainBrushMode::ErodeThermal, minX, maxX, minZ, maxZ, gridX, gridZ);
        ++m_stamps;
        return;
    }

    if (mode == TerrainBrushMode::Flatten && !m_flattenSet) {
        m_flattenTarget = m_brush.targetHeightM >= 0.0f
                              ? m_brush.targetHeightM / m_maxHeight
                              : m_map.sampleBilinear(static_cast<f32>(std::clamp((params.x + m_sizeX * 0.5) / m_sizeX, 0.0, 1.0)),
                                                     static_cast<f32>(std::clamp((params.z + m_sizeZ * 0.5) / m_sizeZ, 0.0, 1.0)));
        m_flattenSet = true;
    }
    if (mode == TerrainBrushMode::Ramp && !m_rampSet) {
        m_rampSet = true;
        m_rampX = params.x;
        m_rampZ = params.z;
        m_rampHeight = m_map.sampleBilinear(static_cast<f32>(std::clamp((params.x + m_sizeX * 0.5) / m_sizeX, 0.0, 1.0)),
                                            static_cast<f32>(std::clamp((params.z + m_sizeZ * 0.5) / m_sizeZ, 0.0, 1.0)));
    }

    // What the mode reads while the live heights change: a copy of the area, with a margin for what it samples.
    Snapshot snap;
    const f64 offsetCellsX = static_cast<f64>(params.offsetX) / stepX, offsetCellsZ = static_cast<f64>(params.offsetZ) / stepZ;
    const bool wantsSnapshot = mode == TerrainBrushMode::Smooth || mode == TerrainBrushMode::Pinch ||
                               mode == TerrainBrushMode::Bulge || mode == TerrainBrushMode::Clone ||
                               mode == TerrainBrushMode::Smudge || mode == TerrainBrushMode::StampAbsolute ||
                               mode == TerrainBrushMode::StampMax || mode == TerrainBrushMode::StampMin;
    f32 centreHeight = 0.0f;  // the ground under the stamp's centre, before this stamp
    if (wantsSnapshot) {
        i64 sMinX = minX - 2, sMaxX = maxX + 2, sMinZ = minZ - 2, sMaxZ = maxZ + 2;
        const i64 reachX = static_cast<i64>(std::ceil(radius * 0.4 / stepX)) + 2;
        const i64 reachZ = static_cast<i64>(std::ceil(radius * 0.4 / stepZ)) + 2;
        if (mode == TerrainBrushMode::Pinch || mode == TerrainBrushMode::Bulge || mode == TerrainBrushMode::Smudge) {
            sMinX -= reachX; sMaxX += reachX; sMinZ -= reachZ; sMaxZ += reachZ;
        }
        if (mode == TerrainBrushMode::Clone) {
            sMinX = std::min(sMinX, minX + static_cast<i64>(std::floor(offsetCellsX)) - 2);
            sMaxX = std::max(sMaxX, maxX + static_cast<i64>(std::ceil(offsetCellsX)) + 2);
            sMinZ = std::min(sMinZ, minZ + static_cast<i64>(std::floor(offsetCellsZ)) - 2);
            sMaxZ = std::max(sMaxZ, maxZ + static_cast<i64>(std::ceil(offsetCellsZ)) + 2);
        }
        snap = takeSnapshot(m_map, sMinX, sMinZ, sMaxX, sMaxZ);
        centreHeight = snap.bilinear(gridX, gridZ);
    }

    // Ramp: the line from where the stroke began to where the cursor is.
    const f64 rampDx = m_pointX - m_rampX, rampDz = m_pointZ - m_rampZ;
    const f64 rampLength2 = rampDx * rampDx + rampDz * rampDz;
    f32 rampEnd = m_rampHeight;
    if (mode == TerrainBrushMode::Ramp && rampLength2 > 1e-12) {
        rampEnd = m_brush.targetHeightM >= 0.0f
                      ? m_brush.targetHeightM / m_maxHeight
                      : m_map.sampleBilinear(static_cast<f32>(std::clamp((m_pointX + m_sizeX * 0.5) / m_sizeX, 0.0, 1.0)),
                                             static_cast<f32>(std::clamp((m_pointZ + m_sizeZ * 0.5) / m_sizeZ, 0.0, 1.0)));
    }
    const f32 smudgeReach = static_cast<f32>(std::max(stepX, std::min(static_cast<f64>(m_brush.spacingM) * 0.8, radius * 0.5)));

    // The weights first, on the ground as it was before this stamp: a protection mask (slope, height) must not see the
    // stamp's own changes, which would make the result depend on the order the vertices are visited in.
    const i64 rectWidth = maxX - minX + 1;
    std::vector<f32> weights(static_cast<size_t>(rectWidth * (maxZ - minZ + 1)), 0.0f);
    for (i64 vz = minZ; vz <= maxZ; ++vz) {
        for (i64 vx = minX; vx <= maxX; ++vx) {
            const f64 dx = (static_cast<f64>(vx) - gridX) * stepX;
            const f64 dz = (static_cast<f64>(vz) - gridZ) * stepZ;
            const f64 dist = std::sqrt(dx * dx + dz * dz);
            if (dist > radius) continue;
            weights[static_cast<size_t>((vz - minZ) * rectWidth + (vx - minX))] = weightAt(params, dx, dz, dist, vx, vz);
        }
    }

    TerrainVertexBounds changed;
    TileCoord lastTile{-1, -1};
    for (i64 vz = minZ; vz <= maxZ; ++vz) {
        for (i64 vx = minX; vx <= maxX; ++vx) {
            const f64 dx = (static_cast<f64>(vx) - gridX) * stepX;
            const f64 dz = (static_cast<f64>(vz) - gridZ) * stepZ;
            const f64 dist = std::sqrt(dx * dx + dz * dz);
            if (dist > radius) continue;
            f32 weight = weights[static_cast<size_t>((vz - minZ) * rectWidth + (vx - minX))];
            if (m_brush.signedStamp && mode != TerrainBrushMode::Raise && mode != TerrainBrushMode::Lower &&
                mode != TerrainBrushMode::Noise) {
                weight = std::max(weight, 0.0f);  // only the raising and lowering modes use the sign
            }
            if (weight == 0.0f || (weight < 0.0f && !m_brush.signedStamp)) continue;

            const u32 ux = static_cast<u32>(vx), uz = static_cast<u32>(vz);
            const TileCoord tile = m_grid.tileOfVertex(ux, uz);
            if (!(tile == lastTile)) {
                touch(tile);
                lastTile = tile;
            }
            f32& cell = m_map.heights()[static_cast<size_t>(uz) * resX + ux];
            f32 height = cell;
            const f32 blend = std::clamp(strength * weight, 0.0f, 1.0f);
            switch (mode) {
                case TerrainBrushMode::Raise: height += strength * weight / m_maxHeight; break;
                case TerrainBrushMode::Lower: height -= strength * weight / m_maxHeight; break;
                case TerrainBrushMode::Smooth: {
                    f32 sum = 0.0f;
                    u32 count = 0;
                    for (i64 oz = -1; oz <= 1; ++oz) {
                        for (i64 ox = -1; ox <= 1; ++ox) {
                            const i64 sx = vx + ox, sz = vz + oz;
                            if (sx < 0 || sz < 0 || sx >= static_cast<i64>(resX) || sz >= static_cast<i64>(resZ)) continue;
                            sum += snap.at(sx, sz);
                            ++count;
                        }
                    }
                    if (count > 0) height += (sum / static_cast<f32>(count) - height) * blend;
                    break;
                }
                case TerrainBrushMode::Flatten: height += (m_flattenTarget - height) * blend; break;
                case TerrainBrushMode::Noise:
                    height += noiseValue(ux, uz, m_brush.seed, m_stamps) * strength * weight / m_maxHeight;
                    break;
                case TerrainBrushMode::Ramp: {
                    f32 t = 0.0f;
                    if (rampLength2 > 1e-12) {
                        const f64 px = static_cast<f64>(vx) * stepX - m_sizeX * 0.5 - m_rampX;
                        const f64 pz = static_cast<f64>(vz) * stepZ - m_sizeZ * 0.5 - m_rampZ;
                        t = static_cast<f32>(std::clamp((px * rampDx + pz * rampDz) / rampLength2, 0.0, 1.0));
                    }
                    height += (m_rampHeight + (rampEnd - m_rampHeight) * t - height) * blend;
                    break;
                }
                case TerrainBrushMode::Terrace: {
                    const f32 step = std::max(m_brush.stepM, 0.01f) / m_maxHeight;
                    const f32 t = height / step;
                    const f32 k = std::floor(t), f = t - k;
                    const f32 edge = 0.22f;  // the share of a step that is a riser
                    const f32 riser = std::clamp((f - (0.5f - edge)) / (2.0f * edge), 0.0f, 1.0f);
                    const f32 target = (k + riser * riser * (3.0f - 2.0f * riser)) * step;
                    height += (target - height) * blend;
                    break;
                }
                case TerrainBrushMode::Pinch:
                case TerrainBrushMode::Bulge: {
                    // The relief is read from farther out (pinch) or from nearer in (bulge): it moves the other way.
                    const f64 len = std::max(dist, 1e-6);
                    const f64 sign = mode == TerrainBrushMode::Pinch ? 1.0 : -1.0;
                    const f64 shift = sign * static_cast<f64>(blend) * radius * 0.3 / len;
                    height = snap.bilinear(static_cast<f64>(vx) + dx * shift / stepX, static_cast<f64>(vz) + dz * shift / stepZ);
                    break;
                }
                case TerrainBrushMode::StampAbsolute: {
                    const f32 target = centreHeight + strength * weight / m_maxHeight;
                    height += (target - height) * std::clamp(weight * 2.0f, 0.0f, 1.0f);
                    break;
                }
                case TerrainBrushMode::StampMax: height = std::max(height, centreHeight + strength * weight / m_maxHeight); break;
                case TerrainBrushMode::StampMin: height = std::min(height, centreHeight - strength * weight / m_maxHeight); break;
                case TerrainBrushMode::Clone: {
                    // The relief is copied as it is: where it stood, not lifted to the ground under the stamp.
                    const f32 source = snap.bilinear(static_cast<f64>(vx) + offsetCellsX, static_cast<f64>(vz) + offsetCellsZ);
                    height += (source - height) * blend;
                    break;
                }
                case TerrainBrushMode::Smudge: {
                    const f32 behind = snap.bilinear(static_cast<f64>(vx) - params.dirX * smudgeReach / stepX,
                                                     static_cast<f64>(vz) - params.dirZ * smudgeReach / stepZ);
                    height += (behind - height) * blend;
                    break;
                }
                case TerrainBrushMode::ErodeThermal:
                case TerrainBrushMode::ErodeHydraulic: break;  // handled by erode()
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

void TerrainStroke::erode(const StampParams& params, bool thermal, i64 minX, i64 maxX, i64 minZ, i64 maxZ, f64 gridX,
                          f64 gridZ) {
    const u32 resX = m_map.resolutionX();
    const f64 stepX = m_grid.cellSizeX(), stepZ = m_grid.cellSizeZ();
    const f64 radius = params.radius;
    // The area the material may move in: the brush's and a margin a droplet can run to.
    const i64 margin = thermal ? 2 : 22;
    const Snapshot snap = takeSnapshot(m_map, minX - margin, minZ - margin, maxX + margin, maxZ + margin);
    std::vector<f32> delta(snap.data.size(), 0.0f);  // normalised height to add
    auto idx = [&](i64 x, i64 z) { return static_cast<size_t>((z - snap.minZ) * snap.width + (x - snap.minX)); };
    auto inside = [&](i64 x, i64 z) { return x >= snap.minX && z >= snap.minZ && x < snap.minX + snap.width && z < snap.minZ + snap.height; };
    const f32 amount = std::clamp(params.strength, 0.0f, 1.0f);

    if (thermal) {
        const f32 talus = std::tan(std::clamp(m_brush.talusDegrees, 1.0f, 89.0f) * 3.14159265f / 180.0f);
        std::vector<f32> work = snap.data;
        const u32 passes = std::clamp(m_brush.iterations, 1u, 32u);
        for (u32 pass = 0; pass < passes; ++pass) {
            std::vector<f32> change(work.size(), 0.0f);
            for (i64 vz = minZ; vz <= maxZ; ++vz) {
                for (i64 vx = minX; vx <= maxX; ++vx) {
                    const f64 dx = (static_cast<f64>(vx) - gridX) * stepX, dz = (static_cast<f64>(vz) - gridZ) * stepZ;
                    const f64 dist = std::sqrt(dx * dx + dz * dz);
                    if (dist > radius || !inside(vx, vz)) continue;
                    const f32 weight = weightAt(params, dx, dz, dist, vx, vz);
                    if (weight <= 0.0f) continue;
                    const f32 here = work[idx(vx, vz)];
                    static const i64 kOffsets[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                    for (const auto& o : kOffsets) {
                        const i64 nx = vx + o[0], nz = vz + o[1];
                        if (!inside(nx, nz)) continue;
                        const f64 run = o[0] != 0 ? stepX : stepZ;
                        const f32 drop = (here - work[idx(nx, nz)]) * m_maxHeight;  // metres
                        const f32 excess = drop - talus * static_cast<f32>(run);
                        if (excess <= 0.0f) continue;
                        const f32 moved = excess * 0.25f * amount * std::min(weight, 1.0f) / m_maxHeight;
                        change[idx(vx, vz)] -= moved;
                        change[idx(nx, nz)] += moved;
                    }
                }
            }
            for (usize i = 0; i < work.size(); ++i) work[i] += change[i];
        }
        for (usize i = 0; i < work.size(); ++i) delta[i] = work[i] - snap.data[i];
    } else {
        const u32 count = std::clamp(m_brush.droplets, 1u, 512u);
        for (u32 d = 0; d < count; ++d) {
            // A start inside the brush, from the stamp's number and the droplet's.
            f64 sx = 0.0, sz = 0.0;
            for (u32 attempt = 0; attempt < 6; ++attempt) {
                sx = static_cast<f64>(unitHash(m_brush.seed, m_stamps * 4096u + d, 21 + attempt * 2) * 2.0f - 1.0f);
                sz = static_cast<f64>(unitHash(m_brush.seed, m_stamps * 4096u + d, 22 + attempt * 2) * 2.0f - 1.0f);
                if (sx * sx + sz * sz <= 1.0) break;
            }
            f64 px = gridX + sx * radius / stepX, pz = gridZ + sz * radius / stepZ;
            const f64 sdx = sx * radius, sdz = sz * radius;
            const f32 weight = weightAt(params, sdx, sdz, std::sqrt(sdx * sdx + sdz * sdz), static_cast<i64>(std::llround(px)),
                                        static_cast<i64>(std::llround(pz)));
            if (weight <= 0.0f || !inside(static_cast<i64>(std::floor(px)), static_cast<i64>(std::floor(pz)))) continue;
            f32 sediment = 0.0f, water = 1.0f, speed = 1.0f;
            f64 dirX = 0.0, dirZ = 0.0;
            for (u32 step = 0; step < 24; ++step) {
                const i64 ix = static_cast<i64>(std::floor(px)), iz = static_cast<i64>(std::floor(pz));
                if (!inside(ix, iz) || !inside(ix + 1, iz + 1)) break;
                const f32 fx = static_cast<f32>(px - static_cast<f64>(ix)), fz = static_cast<f32>(pz - static_cast<f64>(iz));
                const f32 h00 = snap.at(ix, iz) + delta[idx(ix, iz)], h10 = snap.at(ix + 1, iz) + delta[idx(ix + 1, iz)];
                const f32 h01 = snap.at(ix, iz + 1) + delta[idx(ix, iz + 1)], h11 = snap.at(ix + 1, iz + 1) + delta[idx(ix + 1, iz + 1)];
                const f32 gradX = (h10 - h00) * (1.0f - fz) + (h11 - h01) * fz;
                const f32 gradZ = (h01 - h00) * (1.0f - fx) + (h11 - h10) * fx;
                dirX = dirX * 0.3 - static_cast<f64>(gradX) * 0.7;
                dirZ = dirZ * 0.3 - static_cast<f64>(gradZ) * 0.7;
                const f64 length = std::sqrt(dirX * dirX + dirZ * dirZ);
                if (length < 1e-9) break;
                dirX /= length;
                dirZ /= length;
                const f32 heightHere = (h00 * (1.0f - fx) + h10 * fx) * (1.0f - fz) + (h01 * (1.0f - fx) + h11 * fx) * fz;
                px += dirX;
                pz += dirZ;
                const i64 jx = static_cast<i64>(std::floor(px)), jz = static_cast<i64>(std::floor(pz));
                if (!inside(jx, jz) || !inside(jx + 1, jz + 1)) break;
                const f32 gx = static_cast<f32>(px - static_cast<f64>(jx)), gz = static_cast<f32>(pz - static_cast<f64>(jz));
                const f32 heightThere = ((snap.at(jx, jz) + delta[idx(jx, jz)]) * (1.0f - gx) + (snap.at(jx + 1, jz) + delta[idx(jx + 1, jz)]) * gx) * (1.0f - gz) +
                                        ((snap.at(jx, jz + 1) + delta[idx(jx, jz + 1)]) * (1.0f - gx) + (snap.at(jx + 1, jz + 1) + delta[idx(jx + 1, jz + 1)]) * gx) * gz;
                const f32 drop = (heightHere - heightThere) * m_maxHeight;  // metres, positive going downhill
                const f32 capacity = std::max(drop, 0.05f) * speed * water * 0.35f;
                f32 change = 0.0f;  // metres added to the ground here (negative: eroded)
                if (sediment > capacity || drop < 0.0f) {
                    change = drop < 0.0f ? std::min(-drop, sediment) : (sediment - capacity) * 0.3f;
                    sediment -= change;
                } else {
                    change = -std::min((capacity - sediment) * 0.3f, std::max(drop, 0.0f)) * amount * std::min(weight, 1.0f);
                    sediment -= change;
                }
                const f32 share = change / m_maxHeight;
                delta[idx(ix, iz)] += share * (1.0f - fx) * (1.0f - fz);
                delta[idx(ix + 1, iz)] += share * fx * (1.0f - fz);
                delta[idx(ix, iz + 1)] += share * (1.0f - fx) * fz;
                delta[idx(ix + 1, iz + 1)] += share * fx * fz;
                speed = std::sqrt(std::max(speed * speed + drop * 0.5f, 0.01f));
                water *= 0.97f;
            }
        }
    }

    // Write what moved, touching every tile it reaches and noting the changed vertices.
    TerrainVertexBounds changed;
    TileCoord lastTile{-1, -1};
    for (i64 vz = snap.minZ; vz < snap.minZ + snap.height; ++vz) {
        for (i64 vx = snap.minX; vx < snap.minX + snap.width; ++vx) {
            const f32 change = delta[idx(vx, vz)];
            if (change == 0.0f) continue;
            const u32 ux = static_cast<u32>(vx), uz = static_cast<u32>(vz);
            const TileCoord tile = m_grid.tileOfVertex(ux, uz);
            if (!(tile == lastTile)) {
                touch(tile);
                lastTile = tile;
            }
            f32& cell = m_map.heights()[static_cast<size_t>(uz) * resX + ux];
            cell = std::clamp(cell + change, 0.0f, 1.0f);
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
