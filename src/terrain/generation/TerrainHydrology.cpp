#include "terrain/generation/TerrainHydrology.hpp"

#include "terrain/generation/TerrainNoise.hpp"

#include <algorithm>
#include <cmath>
#include <queue>
#include <random>

namespace Caffeine::Terrain {
namespace {

f32 smoothstep(f32 edge0, f32 edge1, f32 x) {
    const f32 t = std::clamp((x - edge0) / std::max(edge1 - edge0, 0.0001f), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

u32 cellIndex(u32 x, u32 z, u32 resX) { return z * resX + x; }

bool isSteep(const TerrainHeightmap& heightmap, u32 x, u32 z) {
    const u32 resX = heightmap.resolutionX();
    const u32 resZ = heightmap.resolutionZ();
    const f32 center = heightmap.sampleNormalized(x, z);
    f32 maxDiff = 0.0f;
    for (i32 dz = -1; dz <= 1; ++dz) {
        for (i32 dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dz == 0) continue;
            const i32 nx = static_cast<i32>(x) + dx;
            const i32 nz = static_cast<i32>(z) + dz;
            if (nx < 0 || nz < 0 || nx >= static_cast<i32>(resX) || nz >= static_cast<i32>(resZ)) {
                continue;
            }
            maxDiff = std::max(maxDiff, std::abs(center - heightmap.sampleNormalized(static_cast<u32>(nx),
                                                                                      static_cast<u32>(nz))));
        }
    }
    return maxDiff > 0.04f;
}

std::pair<u32, u32> findSteepestDownhill(const TerrainHeightmap& heightmap, u32 x, u32 z) {
    const u32 resX = heightmap.resolutionX();
    const u32 resZ = heightmap.resolutionZ();
    const f32 height = heightmap.sampleNormalized(x, z);
    u32 bestX = x;
    u32 bestZ = z;
    f32 lowest = height;

    for (i32 dz = -1; dz <= 1; ++dz) {
        for (i32 dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dz == 0) continue;
            const i32 nx = static_cast<i32>(x) + dx;
            const i32 nz = static_cast<i32>(z) + dz;
            if (nx < 0 || nz < 0 || nx >= static_cast<i32>(resX) || nz >= static_cast<i32>(resZ)) {
                continue;
            }
            const f32 neighbor = heightmap.sampleNormalized(static_cast<u32>(nx), static_cast<u32>(nz));
            if (neighbor < lowest) {
                lowest = neighbor;
                bestX = static_cast<u32>(nx);
                bestZ = static_cast<u32>(nz);
            }
        }
    }

    return {bestX, bestZ};
}

std::vector<f32> buildWaterDistanceField(const TerrainHeightmap& heightmap, f32 seaLevel) {
    const u32 resX = heightmap.resolutionX();
    const u32 resZ = heightmap.resolutionZ();
    const u32 cellCount = resX * resZ;
    std::vector<f32> distance(cellCount, 1e6f);
    std::queue<std::pair<u32, u32>> queue;

    for (u32 z = 0; z < resZ; ++z) {
        for (u32 x = 0; x < resX; ++x) {
            if (heightmap.sampleNormalized(x, z) <= seaLevel) {
                distance[cellIndex(x, z, resX)] = 0.0f;
                queue.emplace(x, z);
            }
        }
    }

    while (!queue.empty()) {
        const auto [cx, cz] = queue.front();
        queue.pop();
        const u32 idx = cellIndex(cx, cz, resX);
        const f32 current = distance[idx];

        for (i32 dz = -1; dz <= 1; ++dz) {
            for (i32 dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dz == 0) continue;
                const i32 nx = static_cast<i32>(cx) + dx;
                const i32 nz = static_cast<i32>(cz) + dz;
                if (nx < 0 || nz < 0 || nx >= static_cast<i32>(resX) || nz >= static_cast<i32>(resZ)) {
                    continue;
                }
                const u32 nidx = cellIndex(static_cast<u32>(nx), static_cast<u32>(nz), resX);
                const f32 step = (dx != 0 && dz != 0) ? 1.414f : 1.0f;
                if (current + step < distance[nidx]) {
                    distance[nidx] = current + step;
                    queue.emplace(static_cast<u32>(nx), static_cast<u32>(nz));
                }
            }
        }
    }

    return distance;
}

}  // namespace

std::vector<f32> buildMoistureMap(const TerrainHeightmap& heightmap,
                                  const ECS::TerrainClimateSettings& climate,
                                  const ECS::TerrainHydrologySettings& hydrology,
                                  const RiverNetwork& rivers,
                                  u32 seed) {
    const u32 resX = heightmap.resolutionX();
    const u32 resZ = heightmap.resolutionZ();
    const u32 cellCount = resX * resZ;
    std::vector<f32> moisture(cellCount, climate.baseHumidity);
    const std::vector<f32> waterDistance = buildWaterDistanceField(heightmap, climate.seaLevel);

    const f32 windRad = climate.prevailingWindAngle * 3.14159265f / 180.0f;
    const f32 windX = std::cos(windRad);
    const f32 windZ = std::sin(windRad);
    const f32 radius = std::max(hydrology.waterMoistureRadius, 1.0f);

    std::vector<f32> riverProximity(cellCount, 0.0f);
    for (const RiverChannel& channel : rivers.channels) {
        for (const auto& [px, pz] : channel.points) {
            const u32 ix = static_cast<u32>(std::clamp(px, 0.0f, static_cast<f32>(resX - 1)));
            const u32 iz = static_cast<u32>(std::clamp(pz, 0.0f, static_cast<f32>(resZ - 1)));
            for (i32 dz = -3; dz <= 3; ++dz) {
                for (i32 dx = -3; dx <= 3; ++dx) {
                    const i32 nx = static_cast<i32>(ix) + dx;
                    const i32 nz = static_cast<i32>(iz) + dz;
                    if (nx < 0 || nz < 0 || nx >= static_cast<i32>(resX) || nz >= static_cast<i32>(resZ)) {
                        continue;
                    }
                    const u32 nidx = cellIndex(static_cast<u32>(nx), static_cast<u32>(nz), resX);
                    const f32 dist = std::sqrt(static_cast<f32>(dx * dx + dz * dz));
                    riverProximity[nidx] = std::max(riverProximity[nidx], 1.0f - dist / 4.0f);
                }
            }
        }
    }

    for (u32 z = 0; z < resZ; ++z) {
        for (u32 x = 0; x < resX; ++x) {
            const u32 idx = cellIndex(x, z, resX);
            const f32 height = heightmap.sampleNormalized(x, z);
            const f32 waterMoisture =
                std::clamp(1.0f - waterDistance[idx] / radius, 0.0f, 1.0f);
            const f32 elevationMoisture = height * 0.25f;

            f32 rainShadow = 0.0f;
            for (u32 step = 1; step <= hydrology.rainShadowSteps; ++step) {
                const i32 checkX = static_cast<i32>(x) - static_cast<i32>(windX * static_cast<f32>(step));
                const i32 checkZ = static_cast<i32>(z) - static_cast<i32>(windZ * static_cast<f32>(step));
                if (checkX < 0 || checkZ < 0 || checkX >= static_cast<i32>(resX) ||
                    checkZ >= static_cast<i32>(resZ)) {
                    break;
                }
                const f32 blocker = heightmap.sampleNormalized(static_cast<u32>(checkX), static_cast<u32>(checkZ));
                if (blocker > 0.7f) {
                    rainShadow = std::max(rainShadow, (blocker - 0.7f) * 0.5f);
                }
            }

            const f32 noise =
                sampleNoise2D(TerrainNoiseAlgorithm::Perlin, static_cast<f32>(x) * 0.08f,
                              static_cast<f32>(z) * 0.08f, seed + 811u);

            f32 value = waterMoisture * 0.35f + elevationMoisture * 0.2f +
                        (1.0f - rainShadow) * climate.baseHumidity * 0.25f +
                        riverProximity[idx] * 0.2f;
            value += (noise - 0.5f) * 0.08f;
            moisture[idx] = std::clamp(value, 0.0f, 1.0f);
        }
    }

    return moisture;
}

RiverNetwork traceRiverNetwork(TerrainHeightmap& heightmap,
                               const ECS::TerrainHydrologySettings& hydrology,
                               const std::vector<f32>& moistureMap, u32 seed) {
    RiverNetwork network;
    const u32 resX = heightmap.resolutionX();
    const u32 resZ = heightmap.resolutionZ();
    if (resX < 3 || resZ < 3) return network;

    std::vector<std::pair<u32, u32>> candidates;
    for (u32 z = 1; z + 1 < resZ; ++z) {
        for (u32 x = 1; x + 1 < resX; ++x) {
            const f32 height = heightmap.sampleNormalized(x, z);
            if (height < hydrology.riverSourceMinHeight || height > hydrology.riverSourceMaxHeight) {
                continue;
            }
            if (isSteep(heightmap, x, z)) continue;
            const f32 moisture = moistureMap.empty() ? 0.5f : moistureMap[cellIndex(x, z, resX)];
            if (moisture < 0.55f) continue;
            candidates.emplace_back(x, z);
        }
    }

    std::mt19937 rng(seed + 4243u);
    if (candidates.size() > hydrology.maxRiverSources) {
        std::shuffle(candidates.begin(), candidates.end(), rng);
        candidates.resize(hydrology.maxRiverSources);
    }

    std::vector<u8> visited(static_cast<size_t>(resX) * static_cast<size_t>(resZ), 0);

    for (const auto& [sx, sz] : candidates) {
        RiverChannel channel;
        u32 x = sx;
        u32 z = sz;
        f32 flow = moistureMap.empty() ? 0.08f : moistureMap[cellIndex(x, z, resX)] * 0.1f;

        for (u32 step = 0; step < resX + resZ; ++step) {
            channel.points.emplace_back(static_cast<f32>(x), static_cast<f32>(z));
            const u32 idx = cellIndex(x, z, resX);
            if (visited[idx] != 0) {
                flow += 0.02f;
            }
            visited[idx] = 1;

            const f32 height = heightmap.sampleNormalized(x, z);
            if (height <= hydrology.riverSourceMinHeight * 0.5f) break;

            const auto [nx, nz] = findSteepestDownhill(heightmap, x, z);
            if (nx == x && nz == z) break;

            heightmap.setNormalized(x, z, std::clamp(height - hydrology.riverCarveStrength * flow, 0.0f, 1.0f));
            x = nx;
            z = nz;
        }

        channel.flowRate = flow;
        if (channel.points.size() >= 4) {
            network.channels.push_back(std::move(channel));
        }
    }

    return network;
}

void carveRiverChannels(TerrainHeightmap& heightmap, const RiverNetwork& network,
                        const ECS::TerrainHydrologySettings& hydrology) {
    for (const RiverChannel& channel : network.channels) {
        for (const auto& [px, pz] : channel.points) {
            const u32 x = static_cast<u32>(std::clamp(px, 0.0f, static_cast<f32>(heightmap.resolutionX() - 1)));
            const u32 z = static_cast<u32>(std::clamp(pz, 0.0f, static_cast<f32>(heightmap.resolutionZ() - 1)));
            const f32 carve = hydrology.riverCarveStrength * channel.flowRate * 2.5f;
            heightmap.setNormalized(x, z,
                                    std::clamp(heightmap.sampleNormalized(x, z) - carve, 0.0f, 1.0f));
        }
    }
}

void depositSedimentInValleys(TerrainHeightmap& heightmap, const RiverNetwork& network, f32 strength) {
    if (strength <= 0.0f) return;
    const u32 resX = heightmap.resolutionX();
    const u32 resZ = heightmap.resolutionZ();

    for (const RiverChannel& channel : network.channels) {
        if (channel.points.size() < 2) continue;
        const auto& end = channel.points.back();
        const u32 x = static_cast<u32>(std::clamp(end.first, 0.0f, static_cast<f32>(resX - 1)));
        const u32 z = static_cast<u32>(std::clamp(end.second, 0.0f, static_cast<f32>(resZ - 1)));
        heightmap.setNormalized(x, z, std::clamp(heightmap.sampleNormalized(x, z) + strength * 0.5f, 0.0f, 1.0f));

        for (i32 dz = -2; dz <= 2; ++dz) {
            for (i32 dx = -2; dx <= 2; ++dx) {
                const i32 nx = static_cast<i32>(x) + dx;
                const i32 nz = static_cast<i32>(z) + dz;
                if (nx < 0 || nz < 0 || nx >= static_cast<i32>(resX) || nz >= static_cast<i32>(resZ)) {
                    continue;
                }
                const f32 dist = std::sqrt(static_cast<f32>(dx * dx + dz * dz));
                const f32 deposit = strength * (1.0f - dist / 3.0f) * 0.15f;
                if (deposit > 0.0f) {
                    heightmap.setNormalized(
                        static_cast<u32>(nx), static_cast<u32>(nz),
                        std::clamp(heightmap.sampleNormalized(static_cast<u32>(nx), static_cast<u32>(nz)) +
                                       deposit,
                                   0.0f, 1.0f));
                }
            }
        }
    }
}

Vec4 assignBiomeSplatWeights(f32 elevation, f32 moisture, f32 slope, f32 temperature, f32 blend,
                             f32 seaLevel) {
    const f32 b = std::max(blend, 0.05f);

    f32 sand = 0.0f;
    f32 grass = 0.0f;
    f32 rock = 0.0f;
    f32 snow = 0.0f;

    if (elevation < seaLevel + 0.04f) {
        sand = 1.0f;
    } else if (elevation < seaLevel + 0.08f) {
        sand = smoothstep(seaLevel, seaLevel + 0.08f, elevation);
        grass = 1.0f - sand;
    } else if (elevation > 0.95f) {
        snow = 1.0f;
    } else if (elevation > 0.78f) {
        snow = smoothstep(0.78f, 0.95f, elevation);
        rock = slope * 0.6f + (1.0f - moisture) * 0.2f;
        grass = std::max(0.0f, 1.0f - snow - rock) * moisture;
    } else if (moisture < 0.25f && elevation < 0.7f) {
        sand = smoothstep(0.25f, 0.1f, moisture) * (1.0f - slope * 0.5f);
        rock = slope * smoothstep(0.2f, 0.2f + b, slope);
        grass = std::max(0.0f, 1.0f - sand - rock) * smoothstep(0.15f, 0.35f, moisture);
    } else if (moisture > 0.6f && elevation < 0.7f) {
        grass = (1.0f - slope * 0.7f) * smoothstep(0.45f, 0.7f, moisture);
        rock = slope * smoothstep(0.25f, 0.25f + b, slope);
        sand = smoothstep(seaLevel + 0.02f, seaLevel + 0.1f, elevation) * 0.2f;
    } else {
        grass = (1.0f - slope * 0.55f) * smoothstep(0.35f, 0.7f, elevation) *
                (1.0f - smoothstep(0.68f, 0.78f, elevation));
        rock = slope * smoothstep(0.2f, 0.2f + b, slope) +
               smoothstep(0.65f, 0.75f, elevation) * 0.35f;
        sand = (1.0f - moisture) * 0.25f;
    }

    if (temperature < 0.35f && elevation > 0.62f) {
        const f32 coldSnow = smoothstep(0.62f, 0.82f, elevation) * (1.0f - temperature);
        snow = std::max(snow, coldSnow);
        grass *= (1.0f - coldSnow);
    }

    const f32 sum = grass + rock + sand + snow;
    if (sum > 0.0001f) {
        return Vec4(grass / sum, rock / sum, sand / sum, snow / sum);
    }
    return Vec4(0.0f, 0.0f, 0.0f, 1.0f);
}

}  // namespace Caffeine::Terrain
