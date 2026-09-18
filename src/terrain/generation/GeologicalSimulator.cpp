#include "terrain/generation/GeologicalSimulator.hpp"

#include "terrain/generation/TerrainNoise.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace Caffeine::Terrain {
namespace {

constexpr f32 kPi = 3.14159265f;

f32 smoothstep(f32 edge0, f32 edge1, f32 x) {
    const f32 t = std::clamp((x - edge0) / std::max(edge1 - edge0, 0.0001f), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

u32 cellIndex(u32 x, u32 z, u32 resX) { return z * resX + x; }

void addHeightBilinear(TerrainHeightmap& heightmap, f32 u, f32 v, f32 delta) {
    const u32 resX = heightmap.resolutionX();
    const u32 resZ = heightmap.resolutionZ();
    if (resX < 2 || resZ < 2) return;

    u = std::clamp(u, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);
    const f32 fx = u * static_cast<f32>(resX - 1);
    const f32 fz = v * static_cast<f32>(resZ - 1);
    const u32 x0 = static_cast<u32>(std::floor(fx));
    const u32 z0 = static_cast<u32>(std::floor(fz));
    const u32 x1 = std::min(x0 + 1, resX - 1);
    const u32 z1 = std::min(z0 + 1, resZ - 1);
    const f32 tx = fx - static_cast<f32>(x0);
    const f32 tz = fz - static_cast<f32>(z0);

    const f32 w00 = (1.0f - tx) * (1.0f - tz);
    const f32 w10 = tx * (1.0f - tz);
    const f32 w01 = (1.0f - tx) * tz;
    const f32 w11 = tx * tz;

    heightmap.setNormalized(x0, z0, heightmap.sampleNormalized(x0, z0) + delta * w00);
    heightmap.setNormalized(x1, z0, heightmap.sampleNormalized(x1, z0) + delta * w10);
    heightmap.setNormalized(x0, z1, heightmap.sampleNormalized(x0, z1) + delta * w01);
    heightmap.setNormalized(x1, z1, heightmap.sampleNormalized(x1, z1) + delta * w11);
}

f32 sampleHeightBilinear(const TerrainHeightmap& heightmap, f32 u, f32 v) {
    return heightmap.sampleBilinear(std::clamp(u, 0.0f, 1.0f), std::clamp(v, 0.0f, 1.0f));
}

void computeGradient(const TerrainHeightmap& heightmap, f32 u, f32 v, f32& gradU, f32& gradV) {
    const f32 step = 1.0f / static_cast<f32>(std::max(heightmap.resolutionX(), heightmap.resolutionZ()));
    gradU = (sampleHeightBilinear(heightmap, u + step, v) - sampleHeightBilinear(heightmap, u - step, v)) *
            0.5f;
    gradV = (sampleHeightBilinear(heightmap, u, v + step) - sampleHeightBilinear(heightmap, u, v - step)) *
            0.5f;
}

void applyThermalStep(TerrainHeightmap& heightmap, f32 talus, f32 intensity) {
    if (intensity <= 0.0f) return;

    const u32 resX = heightmap.resolutionX();
    const u32 resZ = heightmap.resolutionZ();
    std::vector<f32> scratch(heightmap.heights().size());

    for (u32 z = 1; z + 1 < resZ; ++z) {
        for (u32 x = 1; x + 1 < resX; ++x) {
            const f32 center = heightmap.sampleNormalized(x, z);
            f32 totalDiff = 0.0f;
            f32 neighborCount = 0.0f;

            for (i32 dz = -1; dz <= 1; ++dz) {
                for (i32 dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dz == 0) continue;
                    const u32 nx = static_cast<u32>(static_cast<i32>(x) + dx);
                    const u32 nz = static_cast<u32>(static_cast<i32>(z) + dz);
                    const f32 neighbor = heightmap.sampleNormalized(nx, nz);
                    const f32 diff = center - neighbor;
                    if (diff > talus) {
                        totalDiff += diff - talus;
                        ++neighborCount;
                    }
                }
            }

            f32 moved = 0.0f;
            if (neighborCount > 0.0f) {
                moved = totalDiff * 0.25f * intensity / neighborCount;
            }
            scratch[cellIndex(x, z, resX)] = center - moved;
        }
    }

    for (u32 z = 1; z + 1 < resZ; ++z) {
        for (u32 x = 1; x + 1 < resX; ++x) {
            heightmap.setNormalized(x, z, scratch[cellIndex(x, z, resX)]);
        }
    }
}

void simulateHydraulicDroplets(TerrainHeightmap& heightmap, const ECS::TerrainGenerationSettings& settings,
                               const ECS::TerrainSimulationSettings& sim, u32 dropletCount, f32 waterScale) {
    if (dropletCount == 0 || waterScale <= 0.0f) return;

    const u32 resX = heightmap.resolutionX();
    const u32 resZ = heightmap.resolutionZ();
    const f32 erode = std::clamp(settings.hydraulicErode, 0.0f, 1.0f);
    const f32 deposit = std::clamp(settings.hydraulicDeposit, 0.0f, 1.0f);
    const f32 rain = std::max(settings.hydraulicRain, 0.0001f);
    const f32 inertia = std::clamp(settings.hydraulicInertia, 0.0f, 1.0f);
    const f32 evaporation = std::clamp(settings.hydraulicEvaporation, 0.001f, 0.5f);
    const u32 maxSteps = std::max(sim.maxDropletSteps, settings.hydraulicMaxSteps);

    std::mt19937 rng(settings.seed + dropletCount * 17u + static_cast<u32>(waterScale * 1000.0f));
    std::uniform_real_distribution<f32> distU(0.02f, 0.98f);
    std::uniform_real_distribution<f32> distV(0.02f, 0.98f);

    for (u32 droplet = 0; droplet < dropletCount; ++droplet) {
        f32 u = distU(rng);
        f32 v = distV(rng);
        f32 water = rain * (0.5f + sim.humidity);
        f32 sediment = 0.0f;
        f32 speed = 1.0f;
        f32 dirU = 0.0f;
        f32 dirV = 0.0f;

        for (u32 step = 0; step < maxSteps; ++step) {
            const f32 height = sampleHeightBilinear(heightmap, u, v);
            f32 gradU = 0.0f;
            f32 gradV = 0.0f;
            computeGradient(heightmap, u, v, gradU, gradV);

            f32 downhillU = -gradU;
            f32 downhillV = -gradV;
            const f32 downhillLen = std::sqrt(downhillU * downhillU + downhillV * downhillV);
            if (downhillLen > 0.0001f) {
                downhillU /= downhillLen;
                downhillV /= downhillLen;
            }

            dirU = dirU * inertia + downhillU * (1.0f - inertia);
            dirV = dirV * inertia + downhillV * (1.0f - inertia);
            const f32 dirLen = std::sqrt(dirU * dirU + dirV * dirV);
            if (dirLen > 0.0001f) {
                dirU /= dirLen;
                dirV /= dirLen;
            }

            const f32 stepSize = 1.0f / static_cast<f32>(std::max(resX, resZ));
            const f32 nextU = u + dirU * stepSize * 2.5f;
            const f32 nextV = v + dirV * stepSize * 2.5f;
            if (nextU < 0.0f || nextU > 1.0f || nextV < 0.0f || nextV > 1.0f) {
                addHeightBilinear(heightmap, u, v, sediment * deposit);
                break;
            }

            const f32 nextHeight = sampleHeightBilinear(heightmap, nextU, nextV);
            const f32 slope = std::max(height - nextHeight, 0.0f);
            if (slope < 0.00001f && sediment > 0.0f) {
                addHeightBilinear(heightmap, u, v, sediment * deposit);
                break;
            }

            const f32 capacity = std::max(slope, 0.0001f) * speed * water * (0.3f + speed * 0.5f);
            if (capacity > sediment) {
                const f32 erodeAmount =
                    std::min((capacity - sediment) * erode * 0.5f, slope * 0.25f) * waterScale;
                addHeightBilinear(heightmap, u, v, -erodeAmount);
                sediment += erodeAmount;
            } else {
                const f32 depositAmount = (sediment - capacity) * deposit * 0.5f * waterScale;
                addHeightBilinear(heightmap, u, v, depositAmount);
                sediment -= depositAmount;
            }

            u = nextU;
            v = nextV;
            water *= (1.0f - evaporation);
            speed = std::sqrt(speed * speed + slope * 4.0f);
            if (water < 0.001f) break;
        }
    }
}

void applyTectonicPulse(TerrainHeightmap& heightmap, u32 seed, f32 activity, u32 iteration) {
    if (activity <= 0.0f) return;

    const u32 resX = heightmap.resolutionX();
    const u32 resZ = heightmap.resolutionZ();
    const f32 pulse = activity * 0.00002f * std::sin(static_cast<f32>(iteration) * 0.07f);

    for (u32 z = 0; z < resZ; ++z) {
        for (u32 x = 0; x < resX; ++x) {
            const f32 nx = static_cast<f32>(x) / static_cast<f32>(resX);
            const f32 nz = static_cast<f32>(z) / static_cast<f32>(resZ);
            const f32 plate =
                sampleNoise2D(TerrainNoiseAlgorithm::Perlin, nx * 3.0f, nz * 3.0f, seed + 701u);
            const f32 edge = 1.0f - smoothstep(0.2f, 0.45f, std::abs(plate - 0.5f) * 2.0f);
            const f32 uplift = pulse * edge;
            heightmap.setNormalized(x, z,
                                    std::clamp(heightmap.sampleNormalized(x, z) + uplift, 0.0f, 1.0f));
        }
    }
}

void applyWindErosion(TerrainHeightmap& heightmap, f32 windDirectionDeg, f32 intensity,
                      f32 humidity) {
    if (intensity <= 0.0f) return;

    const u32 resX = heightmap.resolutionX();
    const u32 resZ = heightmap.resolutionZ();
    const f32 windRad = windDirectionDeg * kPi / 180.0f;
    const f32 windX = std::cos(windRad);
    const f32 windZ = std::sin(windRad);
    const f32 dryFactor = 1.0f - std::clamp(humidity, 0.0f, 1.0f);

    for (u32 z = 1; z + 1 < resZ; ++z) {
        for (u32 x = 1; x + 1 < resX; ++x) {
            const f32 height = heightmap.sampleNormalized(x, z);
            if (height > 0.55f) continue;

            const f32 hL = heightmap.sampleNormalized(x - 1, z);
            const f32 hR = heightmap.sampleNormalized(x + 1, z);
            const f32 hD = heightmap.sampleNormalized(x, z - 1);
            const f32 hU = heightmap.sampleNormalized(x, z + 1);
            const f32 slopeX = (hR - hL) * 0.5f;
            const f32 slopeZ = (hU - hD) * 0.5f;
            const f32 exposure = std::max(slopeX * windX + slopeZ * windZ, 0.0f);

            if (exposure > 0.02f) {
                const f32 removed = exposure * intensity * dryFactor * 0.0004f;
                heightmap.setNormalized(x, z, std::clamp(height - removed, 0.0f, 1.0f));

                const i32 lx = static_cast<i32>(x) - static_cast<i32>(windX * 2.0f);
                const i32 lz = static_cast<i32>(z) - static_cast<i32>(windZ * 2.0f);
                if (lx >= 0 && lz >= 0 && lx < static_cast<i32>(resX) && lz < static_cast<i32>(resZ)) {
                    const u32 depositX = static_cast<u32>(lx);
                    const u32 depositZ = static_cast<u32>(lz);
                    heightmap.setNormalized(
                        depositX, depositZ,
                        std::clamp(heightmap.sampleNormalized(depositX, depositZ) + removed * 0.6f, 0.0f,
                                   1.0f));
                }
            }
        }
    }
}

void applyGlacialErosion(TerrainHeightmap& heightmap, f32 temperature, f32 intensity) {
    if (intensity <= 0.0f || temperature > 0.45f) return;

    const u32 resX = heightmap.resolutionX();
    const u32 resZ = heightmap.resolutionZ();
    const i32 radius = 4;

    for (u32 z = 0; z < resZ; ++z) {
        for (u32 x = 0; x < resX; ++x) {
            const f32 height = heightmap.sampleNormalized(x, z);
            if (height < 0.65f) continue;

            for (i32 dz = -radius; dz <= radius; ++dz) {
                for (i32 dx = -radius; dx <= radius; ++dx) {
                    const i32 nx = static_cast<i32>(x) + dx;
                    const i32 nz = static_cast<i32>(z) + dz;
                    if (nx < 0 || nz < 0 || nx >= static_cast<i32>(resX) || nz >= static_cast<i32>(resZ)) {
                        continue;
                    }
                    const f32 dist = std::sqrt(static_cast<f32>(dx * dx + dz * dz));
                    const f32 falloff = 1.0f - std::exp(-dist / static_cast<f32>(radius));
                    const f32 carve = falloff * intensity * 0.00006f * (height - 0.55f);
                    heightmap.setNormalized(
                        static_cast<u32>(nx), static_cast<u32>(nz),
                        std::clamp(heightmap.sampleNormalized(static_cast<u32>(nx), static_cast<u32>(nz)) -
                                       carve,
                                   0.0f, 1.0f));
                }
            }
        }
    }
}

void carveRiverNetwork(TerrainHeightmap& heightmap, const std::vector<f32>& precipitation,
                       f32 intensity) {
    if (intensity <= 0.0f) return;

    const u32 resX = heightmap.resolutionX();
    const u32 resZ = heightmap.resolutionZ();
    const u32 cellCount = resX * resZ;
    if (precipitation.size() != cellCount) return;

    std::vector<u32> flowDirection(cellCount, UINT32_MAX);
    std::vector<f32> flowAccum(cellCount, 1.0f);

    for (u32 z = 0; z < resZ; ++z) {
        for (u32 x = 0; x < resX; ++x) {
            const u32 idx = cellIndex(x, z, resX);
            const f32 height = heightmap.sampleNormalized(x, z);
            f32 lowest = height;
            u32 best = UINT32_MAX;

            for (i32 dz = -1; dz <= 1; ++dz) {
                for (i32 dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dz == 0) continue;
                    const i32 nx = static_cast<i32>(x) + dx;
                    const i32 nz = static_cast<i32>(z) + dz;
                    if (nx < 0 || nz < 0 || nx >= static_cast<i32>(resX) ||
                        nz >= static_cast<i32>(resZ)) {
                        continue;
                    }
                    const f32 neighbor =
                        heightmap.sampleNormalized(static_cast<u32>(nx), static_cast<u32>(nz));
                    if (neighbor < lowest) {
                        lowest = neighbor;
                        best = cellIndex(static_cast<u32>(nx), static_cast<u32>(nz), resX);
                    }
                }
            }
            flowDirection[idx] = best;
        }
    }

    std::vector<u32> order(cellCount);
    for (u32 i = 0; i < cellCount; ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](u32 a, u32 b) {
        return heightmap.heights()[a] > heightmap.heights()[b];
    });

    for (u32 idx : order) {
        const u32 downstream = flowDirection[idx];
        if (downstream == UINT32_MAX) continue;
        flowAccum[downstream] += flowAccum[idx] * precipitation[idx];
    }

    for (u32 z = 0; z < resZ; ++z) {
        for (u32 x = 0; x < resX; ++x) {
            const u32 idx = cellIndex(x, z, resX);
            if (flowAccum[idx] < 4.0f) continue;
            const f32 carve = std::log(flowAccum[idx]) * intensity * 0.00008f;
            heightmap.setNormalized(x, z,
                                    std::clamp(heightmap.sampleNormalized(x, z) - carve, 0.0f, 1.0f));
        }
    }
}

std::vector<f32> buildPrecipitationMap(const TerrainHeightmap& heightmap,
                                       const ECS::TerrainSimulationSettings& sim, u32 seed) {
    const u32 resX = heightmap.resolutionX();
    const u32 resZ = heightmap.resolutionZ();
    std::vector<f32> precipitation(resX * resZ, sim.rainfallBase);

    const f32 windRad = sim.windDirection * kPi / 180.0f;
    const f32 windX = std::cos(windRad);
    const f32 windZ = std::sin(windRad);

    for (u32 z = 0; z < resZ; ++z) {
        for (u32 x = 0; x < resX; ++x) {
            const f32 u = static_cast<f32>(x) / static_cast<f32>(resX - 1);
            const f32 v = static_cast<f32>(z) / static_cast<f32>(resZ - 1);
            const f32 height = heightmap.sampleNormalized(x, z);

            const f32 hL = heightmap.sampleNormalized(std::max(x, 1u) - 1, z);
            const f32 hR = heightmap.sampleNormalized(std::min(x + 1, resX - 1), z);
            const f32 hD = heightmap.sampleNormalized(x, std::max(z, 1u) - 1);
            const f32 hU = heightmap.sampleNormalized(x, std::min(z + 1, resZ - 1));
            const f32 slopeX = (hR - hL) * 0.5f;
            const f32 slopeZ = (hU - hD) * 0.5f;
            const f32 windward = std::max(slopeX * windX + slopeZ * windZ, 0.0f);

            const f32 altitude = smoothstep(0.0f, 0.7f, height);
            const f32 moistureNoise =
                sampleNoise2D(TerrainNoiseAlgorithm::Perlin, u * 6.0f, v * 6.0f, seed + 313u);

            f32 rain = sim.rainfallBase * sim.humidity;
            rain *= (0.55f + altitude * 0.45f);
            rain *= (0.5f + windward * 2.5f);
            rain *= (0.7f + moistureNoise * 0.6f);
            precipitation[cellIndex(x, z, resX)] = std::clamp(rain, 0.0f, 1.0f);
        }
    }

    return precipitation;
}

f32 measureAverageDelta(const TerrainHeightmap& before, const TerrainHeightmap& after) {
    const auto& a = before.heights();
    const auto& b = after.heights();
    if (a.size() != b.size() || a.empty()) return 0.0f;

    f32 sum = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
        sum += std::abs(b[i] - a[i]);
    }
    return sum / static_cast<f32>(a.size());
}

}  // namespace

void applyEnvironmentPreset(ECS::TerrainGenerationSettings& settings,
                              ECS::TerrainEnvironment environment) {
    settings.simulation.environment = environment;
    settings.simulation.enabled = true;
    settings.useRidgedNoise = true;
    settings.domainWarp = true;
    settings.domainWarpPasses = 2;

    switch (environment) {
        case ECS::TerrainEnvironment::TropicalWet:
            settings.simulation.totalIterations = 300;
            settings.simulation.tectonicActivity = 0.25f;
            settings.simulation.erosionWater = 0.75f;
            settings.simulation.erosionThermal = 0.35f;
            settings.simulation.erosionGlacial = 0.0f;
            settings.simulation.erosionWind = 0.08f;
            settings.simulation.erosionBiological = 0.5f;
            settings.simulation.temperature = 0.88f;
            settings.simulation.humidity = 0.92f;
            settings.simulation.windDirection = 45.0f;
            settings.simulation.rainfallBase = 0.06f;
            settings.simulation.dropletsPerIteration = 14;
            settings.octaves = 5;
            settings.persistence = 0.45f;
            settings.amplitude = 0.28f;
            settings.domainWarpStrength = 0.18f;
            settings.ridgedBlend = 0.35f;
            break;
        case ECS::TerrainEnvironment::AridDesert:
            settings.simulation.totalIterations = 220;
            settings.simulation.tectonicActivity = 0.15f;
            settings.simulation.erosionWater = 0.12f;
            settings.simulation.erosionThermal = 0.4f;
            settings.simulation.erosionGlacial = 0.0f;
            settings.simulation.erosionWind = 0.55f;
            settings.simulation.erosionBiological = 0.0f;
            settings.simulation.temperature = 0.75f;
            settings.simulation.humidity = 0.08f;
            settings.simulation.windDirection = 315.0f;
            settings.simulation.rainfallBase = 0.01f;
            settings.simulation.dropletsPerIteration = 6;
            settings.octaves = 5;
            settings.persistence = 0.45f;
            settings.amplitude = 0.26f;
            settings.domainWarpStrength = 0.15f;
            settings.ridgedBlend = 0.3f;
            break;
        case ECS::TerrainEnvironment::AlpineGlacial:
            settings.simulation.totalIterations = 350;
            settings.simulation.tectonicActivity = 0.35f;
            settings.simulation.erosionWater = 0.55f;
            settings.simulation.erosionThermal = 0.4f;
            settings.simulation.erosionGlacial = 0.35f;
            settings.simulation.erosionWind = 0.12f;
            settings.simulation.erosionBiological = 0.08f;
            settings.simulation.temperature = 0.25f;
            settings.simulation.humidity = 0.55f;
            settings.simulation.windDirection = 270.0f;
            settings.simulation.rainfallBase = 0.03f;
            settings.simulation.dropletsPerIteration = 12;
            settings.octaves = 5;
            settings.persistence = 0.42f;
            settings.amplitude = 0.3f;
            settings.domainWarpStrength = 0.2f;
            settings.ridgedBlend = 0.5f;
            break;
        case ECS::TerrainEnvironment::Coastal:
            settings.simulation.totalIterations = 260;
            settings.simulation.tectonicActivity = 0.4f;
            settings.simulation.erosionWater = 0.8f;
            settings.simulation.erosionThermal = 0.35f;
            settings.simulation.erosionGlacial = 0.0f;
            settings.simulation.erosionWind = 0.35f;
            settings.simulation.erosionBiological = 0.45f;
            settings.simulation.temperature = 0.5f;
            settings.simulation.humidity = 0.75f;
            settings.simulation.windDirection = 90.0f;
            settings.simulation.rainfallBase = 0.05f;
            settings.simulation.dropletsPerIteration = 20;
            settings.octaves = 7;
            settings.persistence = 0.42f;
            settings.amplitude = 0.33f;
            settings.domainWarpStrength = 0.22f;
            break;
        case ECS::TerrainEnvironment::Volcanic:
            settings.simulation.totalIterations = 280;
            settings.simulation.tectonicActivity = 0.45f;
            settings.simulation.erosionWater = 0.85f;
            settings.simulation.erosionThermal = 0.7f;
            settings.simulation.erosionGlacial = 0.0f;
            settings.simulation.erosionWind = 0.15f;
            settings.simulation.erosionBiological = 0.5f;
            settings.simulation.temperature = 0.85f;
            settings.simulation.humidity = 0.7f;
            settings.simulation.windDirection = 45.0f;
            settings.simulation.rainfallBase = 0.06f;
            settings.simulation.dropletsPerIteration = 26;
            settings.useRidgedNoise = false;
            settings.octaves = 7;
            settings.persistence = 0.5f;
            settings.amplitude = 0.48f;
            settings.domainWarpStrength = 0.35f;
            break;
        case ECS::TerrainEnvironment::Custom:
        default:
            break;
    }
}

GeologicalSimulationResult GeologicalSimulator::run(TerrainHeightmap& heightmap,
                                                    const ECS::TerrainComponent& /*terrain*/,
                                                    ECS::TerrainGenerationSettings& settings) {
    GeologicalSimulationResult result;
    const auto& sim = settings.simulation;
    if (!sim.enabled || sim.totalIterations == 0) return result;

    const std::vector<f32> precipitation = buildPrecipitationMap(heightmap, sim, settings.seed);
    TerrainHeightmap snapshot = heightmap;

    for (u32 iteration = 0; iteration < sim.totalIterations; ++iteration) {
        if (sim.tectonicActivity > 0.0f && iteration % 60 == 0) {
            applyTectonicPulse(heightmap, settings.seed, sim.tectonicActivity, iteration);
        }

        if (settings.hydraulicErosion && sim.erosionWater > 0.0f) {
            simulateHydraulicDroplets(heightmap, settings, sim, sim.dropletsPerIteration,
                                      sim.erosionWater * 0.65f);
        }

        if (settings.thermalErosion && sim.erosionThermal > 0.0f && iteration % 2 == 0) {
            applyThermalStep(heightmap, settings.thermalTalus, sim.erosionThermal * 0.04f);
        }

        if (sim.erosionWind > 0.0f && iteration % 4 == 0) {
            applyWindErosion(heightmap, sim.windDirection, sim.erosionWind * 0.35f, sim.humidity);
        }

        if (sim.erosionGlacial > 0.0f && iteration % 8 == 0) {
            applyGlacialErosion(heightmap, sim.temperature, sim.erosionGlacial * 0.012f);
        }

        if (iteration % 80 == 0) {
            carveRiverNetwork(heightmap, precipitation, sim.erosionWater * 0.25f);
        }

        result.averageDelta = measureAverageDelta(snapshot, heightmap);
        snapshot = heightmap;
        result.iterationsRun = iteration + 1;

        if (sim.autoConvergence && result.averageDelta < sim.convergenceThreshold) {
            result.converged = true;
            break;
        }
    }

    return result;
}

}  // namespace Caffeine::Terrain
