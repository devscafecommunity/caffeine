#include "terrain/spline/TerrainSpline.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Caffeine::Terrain {
namespace {

struct Vec2 {
    f64 x, z;
};

f64 smoothstep(f64 t) {
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

Vec2 catmull(const Vec2& p0, const Vec2& p1, const Vec2& p2, const Vec2& p3, f64 t) {
    const f64 t2 = t * t, t3 = t2 * t;
    auto axis = [&](f64 a, f64 b, f64 c, f64 d) {
        return 0.5 * ((2.0 * b) + (-a + c) * t + (2.0 * a - 5.0 * b + 4.0 * c - d) * t2 + (-a + 3.0 * b - 3.0 * c + d) * t3);
    };
    return {axis(p0.x, p1.x, p2.x, p3.x), axis(p0.z, p1.z, p2.z, p3.z)};
}

f32 groundMetres(const TerrainFrame& frame, f64 x, f64 z) {
    const f32 u = static_cast<f32>(std::clamp(x / frame.sizeX, 0.0, 1.0));
    const f32 v = static_cast<f32>(std::clamp(z / frame.sizeZ, 0.0, 1.0));
    return frame.heightmap->sampleBilinear(u, v) * frame.maxHeight;
}

/// The path as dense points (width, no height yet).
std::vector<SplineSample> densePath(const SplineDesc& desc, f32 stepM) {
    std::vector<SplineSample> out;
    const usize n = desc.points.size();
    auto widthOf = [&](usize i) { return desc.points[i].widthM > 0.0f ? desc.points[i].widthM : desc.widthM; };
    auto at = [&](i64 i) {
        i = std::clamp<i64>(i, 0, static_cast<i64>(n) - 1);
        return Vec2{desc.points[static_cast<usize>(i)].x, desc.points[static_cast<usize>(i)].z};
    };
    f64 along = 0.0;
    Vec2 last{desc.points[0].x, desc.points[0].z};
    out.push_back({static_cast<f32>(last.x), static_cast<f32>(last.z), widthOf(0), 0.0f, 0.0f});
    for (usize i = 0; i + 1 < n; ++i) {
        const Vec2 a = at(static_cast<i64>(i)), b = at(static_cast<i64>(i) + 1);
        const f64 chord = std::hypot(b.x - a.x, b.z - a.z);
        const u32 pieces = std::max<u32>(4, static_cast<u32>(std::ceil(chord / std::max(stepM * 0.5f, 0.01f))));
        for (u32 k = 1; k <= pieces; ++k) {
            const f64 t = static_cast<f64>(k) / pieces;
            const Vec2 p = catmull(at(static_cast<i64>(i) - 1), a, b, at(static_cast<i64>(i) + 2), t);
            const f64 step = std::hypot(p.x - last.x, p.z - last.z);
            if (step <= 0.0) continue;
            along += step;
            last = p;
            const f32 width = widthOf(i) + (widthOf(i + 1) - widthOf(i)) * static_cast<f32>(t);
            out.push_back({static_cast<f32>(p.x), static_cast<f32>(p.z), width, 0.0f, static_cast<f32>(along)});
        }
    }
    // Resample at stepM by arc length.
    std::vector<SplineSample> even;
    even.push_back(out.front());
    f32 next = stepM;
    for (usize i = 1; i < out.size(); ++i) {
        while (out[i].along >= next) {
            const SplineSample& a = out[i - 1];
            const SplineSample& b = out[i];
            const f32 span = std::max(b.along - a.along, 1e-6f);
            const f32 t = (next - a.along) / span;
            even.push_back({a.x + (b.x - a.x) * t, a.z + (b.z - a.z) * t, a.width + (b.width - a.width) * t, 0.0f, next});
            next += stepM;
        }
    }
    if (even.back().along < out.back().along - stepM * 0.05f) even.push_back(out.back());
    if (even.size() < 2) even.push_back(out.back());
    return even;
}

/// Where the corridor of a path reaches on one grid: for every texel inside the box, the distance to the path, the
/// profile height and the half width there.
struct Coverage {
    u32 minX = 0, minZ = 0, maxX = 0, maxZ = 0;
    bool any = false;
    u32 width = 0;
    std::vector<f32> distance, height, half;
    usize index(u32 x, u32 z) const { return static_cast<usize>(z - minZ) * width + (x - minX); }
};

Coverage rasterize(const std::vector<SplineSample>& path, u32 resX, u32 resZ, f64 sizeX, f64 sizeZ, f64 reachExtra) {
    Coverage cov;
    const f64 stepX = sizeX / (std::max(resX, 2u) - 1), stepZ = sizeZ / (std::max(resZ, 2u) - 1);
    f64 lox = 1e30, hix = -1e30, loz = 1e30, hiz = -1e30, maxReach = 0.0;
    for (const SplineSample& s : path) {
        const f64 reach = s.width * 0.5 + reachExtra;
        maxReach = std::max(maxReach, reach);
        lox = std::min<f64>(lox, s.x - reach);
        hix = std::max<f64>(hix, s.x + reach);
        loz = std::min<f64>(loz, s.z - reach);
        hiz = std::max<f64>(hiz, s.z + reach);
    }
    const i64 x0 = std::max<i64>(0, static_cast<i64>(std::floor(lox / stepX))), x1 = std::min<i64>(resX - 1, static_cast<i64>(std::ceil(hix / stepX)));
    const i64 z0 = std::max<i64>(0, static_cast<i64>(std::floor(loz / stepZ))), z1 = std::min<i64>(resZ - 1, static_cast<i64>(std::ceil(hiz / stepZ)));
    if (x0 > x1 || z0 > z1) return cov;
    cov.minX = static_cast<u32>(x0);
    cov.maxX = static_cast<u32>(x1);
    cov.minZ = static_cast<u32>(z0);
    cov.maxZ = static_cast<u32>(z1);
    cov.width = cov.maxX - cov.minX + 1;
    const usize count = static_cast<usize>(cov.width) * (cov.maxZ - cov.minZ + 1);
    cov.distance.assign(count, std::numeric_limits<f32>::max());
    cov.height.assign(count, 0.0f);
    cov.half.assign(count, 0.0f);
    for (usize i = 0; i + 1 < path.size(); ++i) {
        const SplineSample& a = path[i];
        const SplineSample& b = path[i + 1];
        const f64 reach = std::max(a.width, b.width) * 0.5 + reachExtra;
        const i64 bx0 = std::max<i64>(cov.minX, static_cast<i64>(std::floor((std::min(a.x, b.x) - reach) / stepX)));
        const i64 bx1 = std::min<i64>(cov.maxX, static_cast<i64>(std::ceil((std::max(a.x, b.x) + reach) / stepX)));
        const i64 bz0 = std::max<i64>(cov.minZ, static_cast<i64>(std::floor((std::min(a.z, b.z) - reach) / stepZ)));
        const i64 bz1 = std::min<i64>(cov.maxZ, static_cast<i64>(std::ceil((std::max(a.z, b.z) + reach) / stepZ)));
        const f64 dx = b.x - a.x, dz = b.z - a.z;
        const f64 length2 = std::max(dx * dx + dz * dz, 1e-12);
        for (i64 z = bz0; z <= bz1; ++z) {
            for (i64 x = bx0; x <= bx1; ++x) {
                const f64 px = static_cast<f64>(x) * stepX - a.x, pz = static_cast<f64>(z) * stepZ - a.z;
                const f64 t = std::clamp((px * dx + pz * dz) / length2, 0.0, 1.0);
                const f64 d = std::hypot(px - dx * t, pz - dz * t);
                const usize k = cov.index(static_cast<u32>(x), static_cast<u32>(z));
                if (d >= cov.distance[k]) continue;
                cov.distance[k] = static_cast<f32>(d);
                cov.height[k] = a.height + (b.height - a.height) * static_cast<f32>(t);
                cov.half[k] = (a.width + (b.width - a.width) * static_cast<f32>(t)) * 0.5f;
                cov.any = true;
            }
        }
    }
    return cov;
}

/// 1 inside the half width, falling to 0 over `edge` metres outside it.
f32 corridorWeight(f32 distance, f32 half, f32 edge) {
    if (distance <= half) return 1.0f;
    if (edge <= 0.0f || distance >= half + edge) return 0.0f;
    return 1.0f - static_cast<f32>(smoothstep((distance - half) / edge));
}

std::vector<f32> movingAverage(const std::vector<f32>& values, f32 stepM, f32 windowM) {
    const i64 radius = std::max<i64>(0, static_cast<i64>(std::lround(windowM * 0.5f / std::max(stepM, 1e-4f))));
    if (radius == 0) return values;
    std::vector<f32> out(values.size());
    for (i64 i = 0; i < static_cast<i64>(values.size()); ++i) {
        const i64 lo = std::max<i64>(0, i - radius), hi = std::min<i64>(static_cast<i64>(values.size()) - 1, i + radius);
        f64 sum = 0.0;
        for (i64 j = lo; j <= hi; ++j) sum += values[static_cast<usize>(j)];
        out[static_cast<usize>(i)] = static_cast<f32>(sum / static_cast<f64>(hi - lo + 1));
    }
    return out;
}

}  // namespace

usize SplineRecord::bytes() const { return height.bytes() + layers.bytes() + mask.bytes(); }

bool validateSpline(const SplineDesc& desc, const TerrainFrame& frame, std::string* error) {
    auto fail = [&](const char* reason) {
        if (error) *error = reason;
        return false;
    };
    if (desc.points.size() < 2) return fail("a spline needs at least two points");
    if (desc.points.size() > kMaxSplinePoints) return fail("a spline holds at most 256 points");
    if (!(desc.widthM > 0.0f) || desc.widthM > 500.0f) return fail("the width must be between 0 and 500 m");
    if (desc.shoulderM < 0.0f || desc.shoulderM > 500.0f) return fail("the shoulder must be between 0 and 500 m");
    if (desc.depthM < 0.0f || desc.depthM > 200.0f) return fail("the depth must be between 0 and 200 m");
    if (desc.smoothM < 0.0f) return fail("the smoothing window cannot be negative");
    if (desc.kind == SplineKind::Road && !(desc.maxGrade > 0.0f)) return fail("the steepest grade of a road must be above 0");
    if (!desc.mask.empty() && (!TerrainMasks::validName(desc.mask) || TerrainMasks::isBuiltin(desc.mask))) {
        return fail("the mask name is 1 to 31 letters, digits, '_' or '-' and not a built-in mask");
    }
    bool inside = false;
    for (const SplinePoint& p : desc.points) {
        if (!std::isfinite(p.x) || !std::isfinite(p.z)) return fail("a point is not a number");
        if (p.x >= 0.0f && p.z >= 0.0f && p.x <= frame.sizeX && p.z <= frame.sizeZ) inside = true;
    }
    if (!inside) return fail("no point of the spline is on the terrain");
    return true;
}

std::vector<SplineSample> sampleSpline(const SplineDesc& desc, const TerrainFrame& frame, f32 stepM) {
    std::vector<SplineSample> path = densePath(desc, std::max(stepM, 0.05f));
    std::vector<f32> profile(path.size());
    for (usize i = 0; i < path.size(); ++i) profile[i] = groundMetres(frame, path[i].x, path[i].z);
    const f32 step = path.size() > 1 ? path[1].along - path[0].along : stepM;
    if (desc.kind == SplineKind::Road) {
        profile = movingAverage(profile, step, desc.smoothM);
        // Limit the grade: from both ends, a point may differ from the one before by at most `grade * distance`.
        for (usize i = 1; i < profile.size(); ++i) {
            const f32 reach = desc.maxGrade * (path[i].along - path[i - 1].along);
            profile[i] = std::clamp(profile[i], profile[i - 1] - reach, profile[i - 1] + reach);
        }
        for (usize i = profile.size() - 1; i-- > 0;) {
            const f32 reach = desc.maxGrade * (path[i + 1].along - path[i].along);
            profile[i] = std::clamp(profile[i], profile[i + 1] - reach, profile[i + 1] + reach);
        }
    } else {
        profile = movingAverage(profile, step, std::max(desc.smoothM, step * 4.0f));
        for (usize i = 1; i < profile.size(); ++i) profile[i] = std::min(profile[i], profile[i - 1]);  // water runs downhill
    }
    for (usize i = 0; i < path.size(); ++i) path[i].height = profile[i];
    return path;
}

bool applySpline(TerrainHeightmap& heightmap, TerrainLayerMap* layers, TerrainMasks* masks, const ECS::TerrainComponent& settings,
                 const SplineDesc& desc, SplineRecord& record, std::string* error) {
    record = SplineRecord{};
    if (heightmap.empty()) {
        if (error) *error = "the terrain has no heights";
        return false;
    }
    const f64 sizeX = settings.worldSizeX, sizeZ = settings.worldSizeZ;
    const f32 maxHeight = std::max(settings.maxHeight, 0.001f);
    const TerrainFrame frame{&heightmap, static_cast<f32>(sizeX), static_cast<f32>(sizeZ), maxHeight};
    if (!validateSpline(desc, frame, error)) return false;
    if (desc.layer >= 0 && (!layers || layers->empty() || desc.layer >= static_cast<i32>(TerrainLayerMap::kMaxLayers))) {
        if (error) *error = "painting a layer needs the terrain's layered painting on and a layer that exists";
        return false;
    }
    if (!desc.mask.empty() && !masks) {
        if (error) *error = "the terrain has no masks";
        return false;
    }

    const u32 resX = heightmap.resolutionX(), resZ = heightmap.resolutionZ();
    const f64 cell = std::min(sizeX / (resX - 1), sizeZ / (resZ - 1));
    const std::vector<SplineSample> path = sampleSpline(desc, frame, static_cast<f32>(std::max(cell * 0.5, 0.05)));
    record.lengthM = path.back().along;

    // Heights.
    const std::vector<f32> before = heightmap.heights();
    {
        const Coverage cov = rasterize(path, resX, resZ, sizeX, sizeZ, desc.shoulderM);
        for (u32 z = cov.minZ; cov.any && z <= cov.maxZ; ++z) {
            for (u32 x = cov.minX; x <= cov.maxX; ++x) {
                const usize k = cov.index(x, z);
                const f32 d = cov.distance[k];
                if (d == std::numeric_limits<f32>::max()) continue;
                const f32 half = std::max(cov.half[k], 0.01f);
                const f32 w = corridorWeight(d, half, desc.shoulderM);
                if (w <= 0.0f) continue;
                f32& value = heightmap.heights()[static_cast<usize>(z) * resX + x];
                const f32 ground = value * maxHeight;
                f32 target = cov.height[k];
                f32 weight = w;
                if (desc.kind == SplineKind::River && d <= half) {
                    const f32 t = d / half;
                    target -= desc.depthM * (1.0f - t * t);  // a rounded bed: deepest in the middle, level at the bank
                    weight = 1.0f;
                }
                value = std::clamp((ground + (target - ground) * weight) / maxHeight, 0.0f, 1.0f);
            }
        }
    }
    // The record of the heights: the tiles whose vertices differ.
    {
        TileGrid grid(resX, resZ, sizeX, sizeZ);
        for (u32 index = 0; index < grid.tileCount(); ++index) {
            const TileCoord tile = grid.coordOf(index);
            const TerrainVertexBounds rect = grid.owned(tile);
            TileDelta delta;
            delta.tile = tile;
            delta.rect = rect;
            const u32 width = rect.maxX - rect.minX + 1;
            bool changed = false;
            for (u32 z = rect.minZ; z <= rect.maxZ; ++z) {
                for (u32 x = rect.minX; x <= rect.maxX; ++x) {
                    const usize i = static_cast<usize>(z) * resX + x;
                    delta.before.push_back(before[i]);
                    delta.after.push_back(heightmap.heights()[i]);
                    if (before[i] != heightmap.heights()[i]) {
                        changed = true;
                        TerrainVertexBounds& r = record.height.region;
                        if (!r.valid) {
                            r.minX = r.maxX = x;
                            r.minZ = r.maxZ = z;
                            r.valid = true;
                        } else {
                            r.minX = std::min(r.minX, x);
                            r.maxX = std::max(r.maxX, x);
                            r.minZ = std::min(r.minZ, z);
                            r.maxZ = std::max(r.maxZ, z);
                        }
                    }
                    (void)width;
                }
            }
            if (changed) record.height.tiles.push_back(std::move(delta));
        }
        record.height.stamps = 1;
    }

    // The layer.
    if (desc.layer >= 0) {
        const TerrainLayerMap beforeMap = *layers;
        const Coverage cov = rasterize(path, layers->resolutionX(), layers->resolutionZ(), sizeX, sizeZ, desc.shoulderM * 0.5f);
        for (u32 z = cov.minZ; cov.any && z <= cov.maxZ; ++z) {
            for (u32 x = cov.minX; x <= cov.maxX; ++x) {
                const usize k = cov.index(x, z);
                if (cov.distance[k] == std::numeric_limits<f32>::max()) continue;
                const f32 w = corridorWeight(cov.distance[k], std::max(cov.half[k], 0.01f), desc.shoulderM * 0.5f);
                const f32 amount = std::clamp(w * desc.layerStrength, 0.0f, 1.0f);
                if (amount > 0.0f) layers->blendToward(x, z, static_cast<u32>(desc.layer), amount, 0);
            }
        }
        record.layers = diffLayerMaps(beforeMap, *layers);
    }

    // The mask.
    if (!desc.mask.empty()) {
        TerrainMasks::Stored* stored = masks->findStored(desc.mask);
        if (!stored) {
            std::vector<u8> zero(static_cast<usize>(resX) * resZ, 0);
            if (!masks->set(desc.mask, resX, resZ, zero.data())) {
                if (error) *error = "the mask cannot be made (a terrain holds at most 16)";
                // Nothing may stay changed: put the heights and the layers back.
                record.height.applyBefore(heightmap);
                if (desc.layer >= 0) record.layers.applyBefore(*layers);
                record = SplineRecord{};
                return false;
            }
            stored = masks->findStored(desc.mask);
            record.madeMask = true;
        }
        const std::vector<u8> beforeData = stored->data;
        const Coverage cov = rasterize(path, stored->resolutionX, stored->resolutionZ, sizeX, sizeZ, desc.shoulderM);
        for (u32 z = cov.minZ; cov.any && z <= cov.maxZ; ++z) {
            for (u32 x = cov.minX; x <= cov.maxX; ++x) {
                const usize k = cov.index(x, z);
                if (cov.distance[k] == std::numeric_limits<f32>::max()) continue;
                const f32 w = corridorWeight(cov.distance[k], std::max(cov.half[k], 0.01f), desc.shoulderM);
                u8& texel = stored->data[static_cast<usize>(z) * stored->resolutionX + x];
                texel = std::max(texel, static_cast<u8>(std::lround(std::clamp(w, 0.0f, 1.0f) * 255.0f)));
            }
        }
        record.mask.mask = desc.mask;
        if (record.madeMask) {
            record.mask.whole = true;
            record.mask.wholeBefore = MaskSnapshot{};
            record.mask.wholeAfter = MaskSnapshot{true, stored->resolutionX, stored->resolutionZ, stored->data};
        } else {
            const u32 block = TerrainMaskStroke::kBlock;
            for (u32 bz = 0; bz * block < stored->resolutionZ; ++bz) {
                for (u32 bx = 0; bx * block < stored->resolutionX; ++bx) {
                    MaskBlock entry;
                    entry.minX = bx * block;
                    entry.minZ = bz * block;
                    entry.maxX = std::min(entry.minX + block - 1, stored->resolutionX - 1);
                    entry.maxZ = std::min(entry.minZ + block - 1, stored->resolutionZ - 1);
                    bool changed = false;
                    u32 loX = ~0u, loZ = ~0u, hiX = 0, hiZ = 0;
                    for (u32 z = entry.minZ; z <= entry.maxZ; ++z) {
                        for (u32 x = entry.minX; x <= entry.maxX; ++x) {
                            const usize i = static_cast<usize>(z) * stored->resolutionX + x;
                            entry.before.push_back(beforeData[i]);
                            entry.after.push_back(stored->data[i]);
                            if (beforeData[i] != stored->data[i]) {
                                changed = true;
                                loX = std::min(loX, x);
                                loZ = std::min(loZ, z);
                                hiX = std::max(hiX, x);
                                hiZ = std::max(hiZ, z);
                            }
                        }
                    }
                    if (!changed) continue;
                    LayerBounds& r = record.mask.region;
                    if (!r.valid) {
                        r = {loX, loZ, hiX, hiZ, true};
                    } else {
                        r.minX = std::min(r.minX, loX);
                        r.minZ = std::min(r.minZ, loZ);
                        r.maxX = std::max(r.maxX, hiX);
                        r.maxZ = std::max(r.maxZ, hiZ);
                    }
                    record.mask.blocks.push_back(std::move(entry));
                }
            }
        }
    }
    return true;
}

SplineRegions revertSpline(const SplineRecord& record, TerrainHeightmap& heightmap, TerrainLayerMap* layers, TerrainMasks* masks) {
    SplineRegions regions;
    if (!record.height.empty()) regions.height = record.height.applyBefore(heightmap);
    if (layers && !record.layers.empty()) regions.layers = record.layers.applyBefore(*layers);
    if (masks && !record.mask.empty()) {
        record.mask.applyBefore(*masks);
        regions.maskWhole = record.mask.whole;
        regions.mask = record.mask.region;
    }
    return regions;
}

SplineRegions reapplySpline(const SplineRecord& record, TerrainHeightmap& heightmap, TerrainLayerMap* layers, TerrainMasks* masks) {
    SplineRegions regions;
    if (!record.height.empty()) regions.height = record.height.applyAfter(heightmap);
    if (layers && !record.layers.empty()) regions.layers = record.layers.applyAfter(*layers);
    if (masks && !record.mask.empty()) {
        record.mask.applyAfter(*masks);
        regions.maskWhole = record.mask.whole;
        regions.mask = record.mask.region;
    }
    return regions;
}

}  // namespace Caffeine::Terrain
