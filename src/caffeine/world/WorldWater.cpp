#include "world/WorldWater.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <utility>

namespace Caffeine::World {
namespace {

/// Spill height of every vertex: the lowest level water standing there would overflow at (priority-flood from the
/// border). Ties are broken by index so the result never depends on the queue implementation.
std::vector<f32> spillHeights(const std::vector<f32>& h, u32 nx, u32 nz) {
    const size_t count = static_cast<size_t>(nx) * nz;
    std::vector<f32> spill(count, std::numeric_limits<f32>::infinity());
    std::vector<u8> done(count, 0);
    using Entry = std::pair<f32, u32>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
    for (u32 z = 0; z < nz; ++z) {
        for (u32 x = 0; x < nx; ++x) {
            if (x != 0 && z != 0 && x + 1 != nx && z + 1 != nz) continue;
            const u32 i = z * nx + x;
            spill[i] = h[i];
            open.push({h[i], i});
        }
    }
    while (!open.empty()) {
        const auto [level, i] = open.top();
        open.pop();
        if (done[i]) continue;
        done[i] = 1;
        const u32 x = i % nx, z = i / nx;
        const auto visit = [&](u32 n) {
            if (done[n]) return;
            const f32 s = std::max(h[n], level);
            if (s < spill[n]) {
                spill[n] = s;
                open.push({s, n});
            }
        };
        if (x > 0) visit(i - 1);
        if (x + 1 < nx) visit(i + 1);
        if (z > 0) visit(i - nx);
        if (z + 1 < nz) visit(i + nx);
    }
    return spill;
}

/// Two-pass 3-4 chamfer distance (in thirds of a cell) to the nearest non-zero `water` vertex.
std::vector<u32> chamfer(const std::vector<u8>& water, u32 nx, u32 nz) {
    constexpr u32 kFar = std::numeric_limits<u32>::max() / 2;
    std::vector<u32> d(water.size(), kFar);
    for (size_t i = 0; i < water.size(); ++i) {
        if (water[i]) d[i] = 0;
    }
    const auto relax = [&](u32 i, i64 x, i64 z, u32 cost) {
        if (x < 0 || z < 0 || x >= nx || z >= nz) return;
        const u32 n = static_cast<u32>(z) * nx + static_cast<u32>(x);
        d[i] = std::min(d[i], d[n] + cost);
    };
    for (u32 z = 0; z < nz; ++z) {
        for (u32 x = 0; x < nx; ++x) {
            const u32 i = z * nx + x;
            relax(i, i64(x) - 1, z, 3);
            relax(i, x, i64(z) - 1, 3);
            relax(i, i64(x) - 1, i64(z) - 1, 4);
            relax(i, i64(x) + 1, i64(z) - 1, 4);
        }
    }
    for (u32 z = nz; z-- > 0;) {
        for (u32 x = nx; x-- > 0;) {
            const u32 i = z * nx + x;
            relax(i, i64(x) + 1, z, 3);
            relax(i, x, i64(z) + 1, 3);
            relax(i, i64(x) + 1, i64(z) + 1, 4);
            relax(i, i64(x) - 1, i64(z) + 1, 4);
        }
    }
    return d;
}

}  // namespace

WaterLayer buildWater(const WorldComposition& composition, Terrain::TerrainHeightmap& heights,
                      const Terrain::TerrainFrame& frame, const Terrain::TerrainMasks& masks,
                      std::vector<std::string>& errors) {
    WaterLayer layer;
    const u32 nx = heights.resolutionX(), nz = heights.resolutionZ();
    layer.resolutionX = nx;
    layer.resolutionZ = nz;
    if (nx < 2 || nz < 2) return layer;
    const size_t count = static_cast<size_t>(nx) * nz;
    const f32 stepX = frame.sizeX / static_cast<f32>(nx - 1), stepZ = frame.sizeZ / static_cast<f32>(nz - 1);
    const f32 maxHeight = std::max(frame.maxHeight, 1e-6f);
    std::vector<f32>& h = heights.heights();

    // 1-2. Rivers, carving as they go (each river reads its mask, which carving does not change).
    for (const WorldWater& water : composition.water) {
        if (water.fillBasins || water.follow.empty()) continue;
        if (!masks.has(water.follow)) {
            errors.push_back("water `" + water.ref + "`: the terrain has no `" + water.follow + "` mask");
            continue;
        }
        WaterBody body;
        body.ref = water.ref;
        body.coverage.assign(count, 0);
        body.surface.assign(count, 0.0f);
        const f32 depth = std::max(water.carve, 0.0f) / maxHeight;
        for (u32 z = 0; z < nz; ++z) {
            for (u32 x = 0; x < nx; ++x) {
                f32 value = 0.0f;
                masks.sample(frame, water.follow, static_cast<f32>(x) * stepX, static_cast<f32>(z) * stepZ, value);
                if (value < water.min) continue;
                const u32 i = z * nx + x;
                const f32 ground = h[i];
                if (depth > 0.0f) {
                    h[i] = std::max(ground - depth, 0.0f);
                    layer.carved = true;
                }
                body.coverage[i] = 255;
                body.surface[i] = (h[i] + (ground - h[i]) * 0.7f) * frame.maxHeight;
                ++body.cells;
            }
        }
        layer.bodies.push_back(std::move(body));
    }

    // 3. Lakes, over the carved ground.
    bool anyLake = false;
    for (const WorldWater& water : composition.water) anyLake = anyLake || water.fillBasins;
    if (anyLake) {
        const std::vector<f32> spill = spillHeights(h, nx, nz);
        for (const WorldWater& water : composition.water) {
            if (!water.fillBasins) continue;
            WaterBody body;
            body.ref = water.ref;
            body.lake = true;
            body.coverage.assign(count, 0);
            body.surface.assign(count, 0.0f);
            for (size_t i = 0; i < count; ++i) {
                if (h[i] < spill[i] - 1e-6f && h[i] < water.level) {
                    body.coverage[i] = 255;
                    body.surface[i] = std::min(spill[i], water.level) * frame.maxHeight;
                    ++body.cells;
                }
            }
            layer.bodies.push_back(std::move(body));
        }
    }

    // 4. Distance to the nearest water, as a mask.
    std::vector<u8> wet(count, 0);
    for (const WaterBody& body : layer.bodies) {
        for (size_t i = 0; i < count; ++i) wet[i] |= body.coverage[i];
    }
    const std::vector<u32> thirds = chamfer(wet, nx, nz);
    const f32 cell = (stepX + stepZ) * 0.5f;
    const f32 range = std::max(composition.waterDistance, 1e-3f);
    layer.distance.resize(count);
    for (size_t i = 0; i < count; ++i) {
        const f32 metres = static_cast<f32>(thirds[i]) * cell / 3.0f;
        layer.distance[i] = static_cast<u8>(std::lround(std::clamp(metres / range, 0.0f, 1.0f) * 255.0f));
    }
    return layer;
}

}  // namespace Caffeine::World
