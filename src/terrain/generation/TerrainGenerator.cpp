#include "terrain/generation/TerrainGenerator.hpp"

#include "terrain/generation/GeologicalSimulator.hpp"
#include "terrain/generation/TerrainFractal.hpp"
#include "terrain/generation/TerrainHydrology.hpp"
#include "terrain/TerrainResolution.hpp"

#include <algorithm>
#include <cmath>
#include <random>

namespace Caffeine::Terrain {
namespace {

f32 smoothstep(f32 edge0, f32 edge1, f32 x) {
    const f32 t = std::clamp((x - edge0) / std::max(edge1 - edge0, 0.0001f), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

void sanitizeGenerationSettings(ECS::TerrainGenerationSettings& settings) {
    settings.noiseScale = std::clamp(settings.noiseScale, 0.005f, 0.25f);
    settings.amplitude = std::clamp(settings.amplitude, 0.08f, 1.0f);
    settings.baseHeight = std::clamp(settings.baseHeight, 0.0f, 0.85f);
    settings.octaves = std::max(1u, settings.octaves);
    settings.slopeWeightAlpha = std::clamp(settings.slopeWeightAlpha, 0.0f, 0.35f);
    if (settings.amplitude < 0.08f) {
        settings.amplitude = 0.28f;
    }
    settings.fractalRoughness = std::clamp(settings.fractalRoughness, 0.05f, 0.95f);
    settings.spectralExponent = std::clamp(settings.spectralExponent, 0.5f, 4.0f);
}

f32 samplePointHeightNoise(const ECS::TerrainGenerationSettings& gen, TerrainHeightModel model, f32 nx,
                           f32 nz) {
    if (gen.useRidgedNoise && model == TerrainHeightModel::RollingHills) {
        model = TerrainHeightModel::Hybrid;
    }
    return sampleHeightModel2D(model, gen.noiseAlgorithm, nx, nz, gen.seed, gen.octaves,
                               gen.persistence, gen.lacunarity, gen.ridgedBlend, gen.ridgeOffset,
                               gen.ridgeGain, gen.multiplicativeContrast);
}

void applyBaseHeightFromNoise(TerrainHeightmap& heightmap, const ECS::TerrainGenerationSettings& gen,
                              f32 minNoise, f32 maxNoise) {
    const f32 amplitude = std::clamp(gen.amplitude, 0.08f, 1.0f);
    const f32 base = std::clamp(gen.baseHeight, 0.0f, 1.0f);
    const f32 noiseSpan = std::max(maxNoise - minNoise, 0.0001f);

    for (f32& h : heightmap.heights()) {
        const f32 normalized = (h - minNoise) / noiseSpan;
        h = std::clamp(base + (normalized - 0.5f) * amplitude * 2.0f, 0.0f, 1.0f);
    }
}

class BaseNoiseModule final : public ITerrainGeneratorModule {
public:
    const char* name() const override { return "Base Noise"; }

    void apply(TerrainGeneratorContext& ctx) override {
        const auto& terrain = ctx.terrain;
        const auto& gen = ctx.settings;
        auto& heightmap = ctx.heightmap;

        heightmap.resize(terrain.resolutionX, terrain.resolutionZ);
        const u32 resX = heightmap.resolutionX();
        const u32 resZ = heightmap.resolutionZ();
        const f32 scale = std::clamp(gen.noiseScale, 0.005f, 0.25f);
        const f32 amplitude = std::clamp(gen.amplitude, 0.08f, 1.0f);
        const f32 base = std::clamp(gen.baseHeight, 0.0f, 1.0f);
        const f32 spanX = static_cast<f32>(std::max(1u, resX - 1));
        const f32 spanZ = static_cast<f32>(std::max(1u, resZ - 1));

        TerrainHeightModel model = gen.heightModel;
        if (gen.useRidgedNoise && model == TerrainHeightModel::RollingHills) {
            model = TerrainHeightModel::Hybrid;
        }

        f32 minHeight = 1.0f;
        f32 maxHeight = 0.0f;

        if (model == TerrainHeightModel::DiamondSquare) {
            generateDiamondSquare(heightmap, gen.seed, gen.fractalRoughness);
            for (f32& h : heightmap.heights()) {
                h = std::clamp(base + (h - 0.5f) * amplitude * 2.0f, 0.0f, 1.0f);
                minHeight = std::min(minHeight, h);
                maxHeight = std::max(maxHeight, h);
            }
        } else if (model == TerrainHeightModel::SpectralFFT) {
            generateSpectralFft(heightmap, gen.seed + 31u, gen.spectralExponent);
            for (f32& h : heightmap.heights()) {
                h = std::clamp(base + (h - 0.5f) * amplitude * 2.0f, 0.0f, 1.0f);
                minHeight = std::min(minHeight, h);
                maxHeight = std::max(maxHeight, h);
            }
        } else if (model == TerrainHeightModel::HeightfieldMultiply) {
            f32 minNoise = 1.0f;
            f32 maxNoise = 0.0f;
            for (u32 z = 0; z < resZ; ++z) {
                for (u32 x = 0; x < resX; ++x) {
                    const f32 u = static_cast<f32>(x) / spanX;
                    const f32 v = static_cast<f32>(z) / spanZ;
                    f32 nx = u * spanX * scale;
                    f32 nz = v * spanZ * scale;

                    if (gen.domainWarp) {
                        if (gen.fractalDomainWarp) {
                            warpDomainFractal2D(nx, nz, gen.seed + 17u,
                                                std::max(gen.domainWarpScale, 1.0f),
                                                gen.domainWarpStrength, std::max(2u, gen.octaves), nx,
                                                nz);
                        } else {
                            warpDomain2DMultiPass(nx, nz, gen.seed + 17u, gen.domainWarpStrength,
                                                  std::max(1u, gen.domainWarpPasses), nx, nz);
                        }
                    }

                    const f32 layerA =
                        samplePointHeightNoise(gen, gen.multiplyLayerA, nx, nz);
                    const f32 layerB =
                        samplePointHeightNoise(gen, gen.multiplyLayerB, nx + 113.0f, nz - 47.0f);
                    const f32 contrast = std::clamp(gen.multiplicativeContrast, 0.5f, 3.0f);
                    const f32 product =
                        std::pow(std::clamp(layerA * layerB, 0.0f, 1.0f), 1.0f / contrast);
                    heightmap.setNormalized(x, z, product);
                    minNoise = std::min(minNoise, product);
                    maxNoise = std::max(maxNoise, product);
                }
            }
            applyBaseHeightFromNoise(heightmap, gen, minNoise, maxNoise);
            minHeight = *std::min_element(heightmap.heights().begin(), heightmap.heights().end());
            maxHeight = *std::max_element(heightmap.heights().begin(), heightmap.heights().end());
        } else {
            for (u32 z = 0; z < resZ; ++z) {
                for (u32 x = 0; x < resX; ++x) {
                    const f32 u = static_cast<f32>(x) / spanX;
                    const f32 v = static_cast<f32>(z) / spanZ;
                    f32 nx = u * spanX * scale;
                    f32 nz = v * spanZ * scale;

                    if (gen.domainWarp) {
                        if (gen.fractalDomainWarp) {
                            warpDomainFractal2D(nx, nz, gen.seed + 17u,
                                                std::max(gen.domainWarpScale, 1.0f),
                                                gen.domainWarpStrength, std::max(2u, gen.octaves), nx,
                                                nz);
                        } else {
                            warpDomain2DMultiPass(nx, nz, gen.seed + 17u, gen.domainWarpStrength,
                                                  std::max(1u, gen.domainWarpPasses), nx, nz);
                        }
                    }

                    const f32 noise = samplePointHeightNoise(gen, model, nx, nz);
                    const f32 height =
                        std::clamp(base + (noise - 0.5f) * amplitude * 2.0f, 0.0f, 1.0f);
                    heightmap.setNormalized(x, z, height);
                    minHeight = std::min(minHeight, height);
                    maxHeight = std::max(maxHeight, height);
                }
            }
        }

        if (maxHeight - minHeight < 0.002f) {
            heightmap.applyProceduralNoise(amplitude, gen.seed + 17u);
        }
    }
};

class SmoothFilterModule final : public ITerrainGeneratorModule {
public:
    const char* name() const override { return "Smooth"; }

    void apply(TerrainGeneratorContext& ctx) override {
        if (!ctx.settings.smoothPass || ctx.settings.smoothIterations == 0) return;

        auto& heightmap = ctx.heightmap;
        std::vector<f32> scratch(heightmap.heights().size());

        for (u32 iter = 0; iter < ctx.settings.smoothIterations; ++iter) {
            for (u32 z = 0; z < heightmap.resolutionZ(); ++z) {
                for (u32 x = 0; x < heightmap.resolutionX(); ++x) {
                    f32 sum = 0.0f;
                    u32 count = 0;
                    for (i32 dz = -1; dz <= 1; ++dz) {
                        for (i32 dx = -1; dx <= 1; ++dx) {
                            const i32 sx = static_cast<i32>(x) + dx;
                            const i32 sz = static_cast<i32>(z) + dz;
                            if (sx < 0 || sz < 0 ||
                                sx >= static_cast<i32>(heightmap.resolutionX()) ||
                                sz >= static_cast<i32>(heightmap.resolutionZ())) {
                                continue;
                            }
                            sum += heightmap.sampleNormalized(static_cast<u32>(sx),
                                                              static_cast<u32>(sz));
                            ++count;
                        }
                    }
                    scratch[z * heightmap.resolutionX() + x] =
                        count > 0 ? sum / static_cast<f32>(count) : 0.0f;
                }
            }
            heightmap.heights() = scratch;
        }
    }
};

class ThermalErosionModule final : public ITerrainGeneratorModule {
public:
    const char* name() const override { return "Thermal Erosion"; }

    void apply(TerrainGeneratorContext& ctx) override {
        if (!ctx.settings.thermalErosion || ctx.settings.thermalIterations == 0) return;

        auto& heightmap = ctx.heightmap;
        const f32 talus = std::max(ctx.settings.thermalTalus, 0.0001f);
        const u32 resX = heightmap.resolutionX();
        const u32 resZ = heightmap.resolutionZ();

        for (u32 iter = 0; iter < ctx.settings.thermalIterations; ++iter) {
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

                    if (neighborCount > 0.0f) {
                        const f32 moved = totalDiff * 0.25f / neighborCount;
                        heightmap.setNormalized(x, z, center - moved);
                    }
                }
            }
        }
    }
};

class HydraulicErosionModule final : public ITerrainGeneratorModule {
public:
    const char* name() const override { return "Hydraulic Erosion"; }

    void apply(TerrainGeneratorContext& ctx) override {
        if (!ctx.settings.hydraulicErosion || ctx.settings.hydraulicIterations == 0) return;

        auto& heightmap = ctx.heightmap;
        const u32 resX = heightmap.resolutionX();
        const u32 resZ = heightmap.resolutionZ();
        const f32 erode = std::clamp(ctx.settings.hydraulicErode, 0.0f, 1.0f);
        const f32 deposit = std::clamp(ctx.settings.hydraulicDeposit, 0.0f, 1.0f);
        const f32 rain = std::max(ctx.settings.hydraulicRain, 0.0001f);
        const f32 inertia = std::clamp(ctx.settings.hydraulicInertia, 0.0f, 1.0f);
        const f32 evaporation = std::clamp(ctx.settings.hydraulicEvaporation, 0.001f, 0.5f);
        const u32 maxSteps = std::max(1u, ctx.settings.hydraulicMaxSteps);

        std::mt19937 rng(ctx.settings.seed + 4099u);
        std::uniform_real_distribution<f32> distU(0.02f, 0.98f);
        std::uniform_real_distribution<f32> distV(0.02f, 0.98f);

        for (u32 droplet = 0; droplet < ctx.settings.hydraulicIterations; ++droplet) {
            f32 u = distU(rng);
            f32 v = distV(rng);
            f32 water = rain;
            f32 sediment = 0.0f;
            f32 speed = 1.0f;
            f32 dirU = 0.0f;
            f32 dirV = 0.0f;

            for (u32 step = 0; step < maxSteps; ++step) {
                const f32 height = heightmap.sampleBilinear(u, v);
                const f32 stepSize = 1.0f / static_cast<f32>(std::max(resX, resZ));
                const f32 gradU =
                    (heightmap.sampleBilinear(std::min(u + stepSize, 1.0f), v) -
                     heightmap.sampleBilinear(std::max(u - stepSize, 0.0f), v)) *
                    0.5f;
                const f32 gradV =
                    (heightmap.sampleBilinear(u, std::min(v + stepSize, 1.0f)) -
                     heightmap.sampleBilinear(u, std::max(v - stepSize, 0.0f))) *
                    0.5f;

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

                const f32 nextU = u + dirU * stepSize * 2.5f;
                const f32 nextV = v + dirV * stepSize * 2.5f;
                if (nextU < 0.0f || nextU > 1.0f || nextV < 0.0f || nextV > 1.0f) break;

                const f32 nextHeight = heightmap.sampleBilinear(nextU, nextV);
                const f32 slope = std::max(height - nextHeight, 0.0f);
                if (slope < 0.00001f) break;

                const u32 ix = static_cast<u32>(std::clamp(u * static_cast<f32>(resX - 1), 0.0f,
                                                             static_cast<f32>(resX - 1)));
                const u32 iz = static_cast<u32>(std::clamp(v * static_cast<f32>(resZ - 1), 0.0f,
                                                             static_cast<f32>(resZ - 1)));

                const f32 capacity = std::max(slope, 0.0001f) * speed * water * (0.3f + speed * 0.5f);
                if (capacity > sediment) {
                    const f32 erodeAmount =
                        std::min((capacity - sediment) * erode * 0.5f, slope * 0.25f);
                    heightmap.setNormalized(ix, iz, heightmap.sampleNormalized(ix, iz) - erodeAmount);
                    sediment += erodeAmount;
                } else {
                    const f32 depositAmount = (sediment - capacity) * deposit * 0.5f;
                    heightmap.setNormalized(ix, iz,
                                            heightmap.sampleNormalized(ix, iz) + depositAmount);
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
};

class HeightQuantizeModule final : public ITerrainGeneratorModule {
public:
    const char* name() const override { return "Height Quantize"; }

    void apply(TerrainGeneratorContext& ctx) override {
        const f32 steps = ctx.settings.heightQuantize;
        if (steps <= 1.0f) return;

        auto& heightmap = ctx.heightmap;
        for (u32 z = 0; z < heightmap.resolutionZ(); ++z) {
            for (u32 x = 0; x < heightmap.resolutionX(); ++x) {
                const f32 h = heightmap.sampleNormalized(x, z);
                const f32 quantized = std::round(h * steps) / steps;
                heightmap.setNormalized(x, z, quantized);
            }
        }
    }
};

class StylizedCurveModule final : public ITerrainGeneratorModule {
public:
    const char* name() const override { return "Stylized Curve"; }

    void apply(TerrainGeneratorContext& ctx) override {
        if (ctx.settings.style != ECS::TerrainGenStyle::Stylized) return;

        auto& heightmap = ctx.heightmap;
        for (u32 z = 0; z < heightmap.resolutionZ(); ++z) {
            for (u32 x = 0; x < heightmap.resolutionX(); ++x) {
                f32 h = heightmap.sampleNormalized(x, z);
                h = h * h * (3.0f - 2.0f * h);
                if (h > 0.55f) {
                    h = 0.55f + (h - 0.55f) * 1.35f;
                }
                heightmap.setNormalized(x, z, std::clamp(h, 0.0f, 1.0f));
            }
        }
    }
};

void blurSplatmap(TerrainSplatmap& splatmap, u32 passes) {
    if (passes == 0 || splatmap.empty()) return;

    const u32 resX = splatmap.resolutionX();
    const u32 resZ = splatmap.resolutionZ();
    std::vector<Vec4> scratch(splatmap.weights().size());

    for (u32 pass = 0; pass < passes; ++pass) {
        for (u32 z = 0; z < resZ; ++z) {
            for (u32 x = 0; x < resX; ++x) {
                Vec4 sum(0.0f, 0.0f, 0.0f, 0.0f);
                u32 count = 0;
                for (i32 dz = -1; dz <= 1; ++dz) {
                    for (i32 dx = -1; dx <= 1; ++dx) {
                        const i32 sx = static_cast<i32>(x) + dx;
                        const i32 sz = static_cast<i32>(z) + dz;
                        if (sx < 0 || sz < 0 || sx >= static_cast<i32>(resX) ||
                            sz >= static_cast<i32>(resZ)) {
                            continue;
                        }
                        sum += splatmap.sample(static_cast<u32>(sx), static_cast<u32>(sz));
                        ++count;
                    }
                }
                Vec4 averaged = count > 0 ? sum / static_cast<f32>(count) : Vec4(0, 0, 0, 1);
                const f32 weightSum = averaged.x + averaged.y + averaged.z + averaged.w;
                if (weightSum > 0.0001f) {
                    averaged /= weightSum;
                }
                scratch[z * resX + x] = averaged;
            }
        }
        splatmap.weights() = scratch;
    }
}

void smoothHeightmap(TerrainHeightmap& heightmap, u32 iterations) {
    if (iterations == 0 || heightmap.empty()) return;

    const u32 resX = heightmap.resolutionX();
    const u32 resZ = heightmap.resolutionZ();
    std::vector<f32> scratch(heightmap.heights().size());

    for (u32 iter = 0; iter < iterations; ++iter) {
        for (u32 z = 0; z < resZ; ++z) {
            for (u32 x = 0; x < resX; ++x) {
                f32 sum = 0.0f;
                u32 count = 0;
                for (i32 dz = -1; dz <= 1; ++dz) {
                    for (i32 dx = -1; dx <= 1; ++dx) {
                        const i32 sx = static_cast<i32>(x) + dx;
                        const i32 sz = static_cast<i32>(z) + dz;
                        if (sx < 0 || sz < 0 || sx >= static_cast<i32>(resX) ||
                            sz >= static_cast<i32>(resZ)) {
                            continue;
                        }
                        sum += heightmap.sampleNormalized(static_cast<u32>(sx), static_cast<u32>(sz));
                        ++count;
                    }
                }
                scratch[z * resX + x] = count > 0 ? sum / static_cast<f32>(count) : 0.0f;
            }
        }
        heightmap.heights() = scratch;
    }
}

f32 sampleMoistureBilinear(const std::vector<f32>& moistureMap, u32 resX, u32 resZ, f32 u, f32 v) {
    if (moistureMap.size() != static_cast<size_t>(resX) * static_cast<size_t>(resZ) || resX < 2 ||
        resZ < 2) {
        return 0.5f;
    }

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

    const f32 h00 = moistureMap[z0 * resX + x0];
    const f32 h10 = moistureMap[z0 * resX + x1];
    const f32 h01 = moistureMap[z1 * resX + x0];
    const f32 h11 = moistureMap[z1 * resX + x1];
    const f32 ix0 = h00 + (h10 - h00) * tx;
    const f32 ix1 = h01 + (h11 - h01) * tx;
    return ix0 + (ix1 - ix0) * tz;
}

class RiverTracingModule final : public ITerrainGeneratorModule {
public:
    const char* name() const override { return "River Tracing"; }

    void apply(TerrainGeneratorContext& ctx) override {
        if (!ctx.settings.hydrology.traceRivers || !ctx.state) return;

        const auto& climate = ctx.settings.climate;
        const auto& hydrology = ctx.settings.hydrology;
        auto& heightmap = ctx.heightmap;

        ctx.state->moistureMap =
            buildMoistureMap(heightmap, climate, hydrology, {}, ctx.settings.seed);
        ctx.state->riverNetwork =
            traceRiverNetwork(heightmap, hydrology, ctx.state->moistureMap, ctx.settings.seed);
        carveRiverChannels(heightmap, ctx.state->riverNetwork, hydrology);
        depositSedimentInValleys(heightmap, ctx.state->riverNetwork, hydrology.sedimentDepositStrength);
        ctx.state->moistureMap =
            buildMoistureMap(heightmap, climate, hydrology, ctx.state->riverNetwork, ctx.settings.seed);
    }
};

class AutoSplatModule final : public ITerrainGeneratorModule {
public:
    const char* name() const override { return "Auto Splat"; }

    void apply(TerrainGeneratorContext& ctx) override {
        if (!ctx.settings.autoSplat) return;

        const auto& terrain = ctx.terrain;
        auto& heightmap = ctx.heightmap;
        auto& splatmap = ctx.splatmap;
        const auto& climate = ctx.settings.climate;

        const u32 splatResX = splatResolutionX(terrain);
        const u32 splatResZ = splatResolutionZ(terrain);
        splatmap.resize(splatResX, splatResZ);

        TerrainHeightmap heightForSplat = heightmap;
        smoothHeightmap(heightForSplat, 2);

        std::vector<f32> moistureMap;
        if (ctx.settings.useClimateBiomes) {
            if (ctx.state && !ctx.state->moistureMap.empty()) {
                moistureMap = ctx.state->moistureMap;
            } else {
                moistureMap = buildMoistureMap(heightmap, climate, ctx.settings.hydrology,
                                               ctx.state ? ctx.state->riverNetwork : RiverNetwork{},
                                               ctx.settings.seed);
            }
        }

        const u32 moistureResX = heightmap.resolutionX();
        const u32 moistureResZ = heightmap.resolutionZ();
        const f32 blend = std::max(ctx.settings.splatBlendRange, 0.05f);

        for (u32 z = 0; z < splatmap.resolutionZ(); ++z) {
            for (u32 x = 0; x < splatmap.resolutionX(); ++x) {
                const f32 u = static_cast<f32>(x) / static_cast<f32>(splatmap.resolutionX() - 1);
                const f32 v = static_cast<f32>(z) / static_cast<f32>(splatmap.resolutionZ() - 1);
                const f32 height = heightForSplat.sampleBilinear(u, v);
                const Vec3 normal =
                    heightForSplat.sampleNormalBilinear(u, v, terrain.worldSizeX, terrain.worldSizeZ,
                                                        terrain.maxHeight);
                const f32 slope = 1.0f - std::clamp(normal.y, 0.0f, 1.0f);

                Vec4 weights;
                if (ctx.settings.useClimateBiomes) {
                    const f32 moisture =
                        sampleMoistureBilinear(moistureMap, moistureResX, moistureResZ, u, v);
                    weights = assignBiomeSplatWeights(height, moisture, slope, climate.temperature,
                                                      blend, climate.seaLevel);
                } else {
                    const f32 slopeFactor = smoothstep(0.25f, 0.55f, slope);
                    f32 sand = 1.0f - smoothstep(0.06f, 0.06f + blend * 1.5f, height);
                    f32 grass = smoothstep(0.1f, 0.1f + blend * 1.5f, height) *
                                (1.0f - smoothstep(0.62f, 0.62f + blend * 1.5f, height)) *
                                (1.0f - slopeFactor * 0.85f);
                    f32 rock = slopeFactor * smoothstep(0.18f, 0.18f + blend, slope);
                    rock += smoothstep(0.45f, 0.45f + blend, height) * slopeFactor * 0.35f;
                    f32 snow = smoothstep(0.66f, 0.66f + blend * 1.5f, height) *
                               (1.0f - slopeFactor * 0.65f);
                    const f32 sum = grass + rock + sand + snow;
                    weights = sum > 0.0001f ? Vec4(grass / sum, rock / sum, sand / sum, snow / sum)
                                            : Vec4(0, 0, 0, 1);
                }

                splatmap.set(x, z, weights);
            }
        }

        blurSplatmap(splatmap, ctx.settings.splatBlurPasses);
    }
};

class PostSimulationSmoothModule final : public ITerrainGeneratorModule {
public:
    const char* name() const override { return "Post Simulation Smooth"; }

    void apply(TerrainGeneratorContext& ctx) override {
        if (!ctx.settings.simulation.enabled || ctx.settings.postSimSmoothIterations == 0) return;
        smoothHeightmap(ctx.heightmap, ctx.settings.postSimSmoothIterations);
    }
};

class ExponentialSlopeWeightingModule final : public ITerrainGeneratorModule {
public:
    const char* name() const override { return "Slope Weighting"; }

    void apply(TerrainGeneratorContext& ctx) override {
        if (!ctx.settings.slopeWeighting || ctx.settings.slopeWeightAlpha <= 0.0f) return;

        auto& heightmap = ctx.heightmap;
        const u32 resX = heightmap.resolutionX();
        const u32 resZ = heightmap.resolutionZ();
        if (resX < 3 || resZ < 3) return;

        const f32 alpha = ctx.settings.slopeWeightAlpha;
        std::vector<f32> scratch(heightmap.heights().size());

        for (u32 z = 0; z < resZ; ++z) {
            for (u32 x = 0; x < resX; ++x) {
                const f32 center = heightmap.sampleNormalized(x, z);
                const u32 xL = x > 0 ? x - 1 : x;
                const u32 xR = x + 1 < resX ? x + 1 : x;
                const u32 zD = z > 0 ? z - 1 : z;
                const u32 zU = z + 1 < resZ ? z + 1 : z;

                const f32 dhdx = (heightmap.sampleNormalized(xR, z) - heightmap.sampleNormalized(xL, z)) *
                                 0.5f;
                const f32 dhdz =
                    (heightmap.sampleNormalized(x, zU) - heightmap.sampleNormalized(x, zD)) * 0.5f;
                const f32 slope = std::sqrt(dhdx * dhdx + dhdz * dhdz);
                scratch[z * resX + x] = std::clamp(center * std::exp(-alpha * slope), 0.0f, 1.0f);
            }
        }

        heightmap.heights() = scratch;
    }
};

class GeologicalSimulationModule final : public ITerrainGeneratorModule {
public:
    const char* name() const override { return "Geological Simulation"; }

    void apply(TerrainGeneratorContext& ctx) override {
        if (!ctx.settings.simulation.enabled) return;
        GeologicalSimulator::run(ctx.heightmap, ctx.terrain, ctx.settings);
    }
};

BaseNoiseModule g_baseNoise;
SmoothFilterModule g_smooth;
ThermalErosionModule g_thermal;
HydraulicErosionModule g_hydraulic;
HeightQuantizeModule g_quantize;
StylizedCurveModule g_stylized;
AutoSplatModule g_autoSplat;
GeologicalSimulationModule g_geological;
PostSimulationSmoothModule g_postSimSmooth;
ExponentialSlopeWeightingModule g_slopeWeighting;
RiverTracingModule g_riverTracing;

}  // namespace

void applyGenerationStylePreset(ECS::TerrainGenerationSettings& settings, ECS::TerrainGenStyle style) {
    if (style == ECS::TerrainGenStyle::Custom) {
        sanitizeGenerationSettings(settings);
        settings.style = style;
        return;
    }

    const u32 seed = settings.seed;
    settings = ECS::TerrainGenerationSettings{};
    settings.seed = seed;
    settings.style = style;

    switch (style) {
        case ECS::TerrainGenStyle::Realistic:
            settings.noiseAlgorithm = TerrainNoiseAlgorithm::Simplex;
            settings.heightModel = TerrainHeightModel::RollingHills;
            settings.noiseScale = 0.025f;
            settings.octaves = 5;
            settings.persistence = 0.5f;
            settings.lacunarity = 2.0f;
            settings.amplitude = 0.38f;
            settings.baseHeight = 0.28f;
            settings.domainWarp = true;
            settings.fractalDomainWarp = true;
            settings.domainWarpStrength = 0.12f;
            settings.domainWarpScale = 32.0f;
            settings.slopeWeighting = true;
            settings.slopeWeightAlpha = 0.06f;
            settings.thermalErosion = true;
            settings.thermalIterations = 18;
            settings.hydraulicErosion = false;
            settings.hydrology.traceRivers = true;
            settings.hydrology.maxRiverSources = 32;
            settings.hydrology.riverCarveStrength = 0.00004f;
            settings.smoothPass = true;
            settings.smoothIterations = 1;
            settings.useClimateBiomes = true;
            settings.simulation.enabled = false;
            break;
        case ECS::TerrainGenStyle::LowPoly:
            settings.noiseAlgorithm = TerrainNoiseAlgorithm::Value;
            settings.heightModel = TerrainHeightModel::FractalFBM;
            settings.noiseScale = 0.03f;
            settings.octaves = 3;
            settings.persistence = 0.45f;
            settings.lacunarity = 2.0f;
            settings.amplitude = 0.42f;
            settings.baseHeight = 0.25f;
            settings.domainWarp = false;
            settings.slopeWeighting = false;
            settings.thermalErosion = false;
            settings.hydraulicErosion = false;
            settings.hydrology.traceRivers = false;
            settings.smoothPass = false;
            settings.heightQuantize = 12.0f;
            settings.simulation.enabled = false;
            break;
        case ECS::TerrainGenStyle::Stylized:
            settings.noiseAlgorithm = TerrainNoiseAlgorithm::Perlin;
            settings.heightModel = TerrainHeightModel::FractalFBM;
            settings.noiseScale = 0.028f;
            settings.octaves = 4;
            settings.persistence = 0.55f;
            settings.lacunarity = 2.1f;
            settings.amplitude = 0.52f;
            settings.baseHeight = 0.22f;
            settings.domainWarp = false;
            settings.slopeWeighting = false;
            settings.thermalErosion = false;
            settings.hydraulicErosion = false;
            settings.hydrology.traceRivers = false;
            settings.smoothPass = true;
            settings.smoothIterations = 2;
            settings.simulation.enabled = false;
            break;
        case ECS::TerrainGenStyle::Custom:
            break;
    }
}

void TerrainGeneratorPipeline::addModule(ITerrainGeneratorModule& module) {
    m_modules.push_back(&module);
}

void TerrainGeneratorPipeline::clear() { m_modules.clear(); }

void TerrainGeneratorPipeline::run(TerrainGeneratorContext& ctx) const {
    for (ITerrainGeneratorModule* module : m_modules) {
        if (module) module->apply(ctx);
    }
}

void TerrainGenerator::generate(TerrainHeightmap& heightmap, TerrainSplatmap& splatmap,
                                const ECS::TerrainComponent& terrain,
                                ECS::TerrainGenerationSettings& settings) {
    sanitizeGenerationSettings(settings);

    TerrainGenerationState state;
    TerrainGeneratorContext ctx{heightmap, splatmap, terrain, settings, &state};

    TerrainGeneratorPipeline pipeline;
    pipeline.addModule(g_baseNoise);
    pipeline.addModule(g_stylized);
    if (settings.simulation.enabled) {
        pipeline.addModule(g_geological);
        pipeline.addModule(g_postSimSmooth);
    } else {
        pipeline.addModule(g_thermal);
        if (settings.hydrology.traceRivers) {
            pipeline.addModule(g_riverTracing);
        } else {
            pipeline.addModule(g_hydraulic);
        }
    }
    pipeline.addModule(g_slopeWeighting);
    pipeline.addModule(g_smooth);
    pipeline.addModule(g_quantize);
    pipeline.addModule(g_autoSplat);
    pipeline.run(ctx);
}

}  // namespace Caffeine::Terrain
