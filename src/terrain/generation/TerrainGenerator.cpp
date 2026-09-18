#include "terrain/generation/TerrainGenerator.hpp"

#include <algorithm>
#include <cmath>
#include <random>

namespace Caffeine::Terrain {
namespace {

class BaseNoiseModule final : public ITerrainGeneratorModule {
public:
    const char* name() const override { return "Base Noise"; }

    void apply(TerrainGeneratorContext& ctx) override {
        const auto& terrain = ctx.terrain;
        const auto& gen = ctx.settings;
        auto& heightmap = ctx.heightmap;

        heightmap.resize(terrain.resolutionX, terrain.resolutionZ);
        const f32 scale = std::max(gen.noiseScale, 0.0001f);
        const f32 amplitude = std::clamp(gen.amplitude, 0.0f, 1.0f);
        const f32 base = std::clamp(gen.baseHeight, 0.0f, 1.0f);

        for (u32 z = 0; z < heightmap.resolutionZ(); ++z) {
            for (u32 x = 0; x < heightmap.resolutionX(); ++x) {
                f32 nx = static_cast<f32>(x) * scale;
                f32 nz = static_cast<f32>(z) * scale;

                if (gen.domainWarp) {
                    warpDomain2D(nx, nz, gen.seed + 17u, gen.domainWarpStrength, nx, nz);
                }

                f32 noise = sampleFbm2D(gen.noiseAlgorithm, nx, nz, gen.seed, gen.octaves,
                                        gen.persistence, gen.lacunarity);
                heightmap.setNormalized(x, z, std::clamp(base + (noise - 0.5f) * amplitude * 2.0f,
                                                          0.0f, 1.0f));
            }
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

        std::mt19937 rng(ctx.settings.seed + 4099u);
        std::uniform_int_distribution<u32> distX(1, resX - 2);
        std::uniform_int_distribution<u32> distZ(1, resZ - 2);

        for (u32 droplet = 0; droplet < ctx.settings.hydraulicIterations; ++droplet) {
            f32 x = static_cast<f32>(distX(rng));
            f32 z = static_cast<f32>(distZ(rng));
            f32 water = rain;
            f32 sediment = 0.0f;
            f32 speed = 1.0f;

            for (u32 step = 0; step < 64; ++step) {
                const u32 ix = static_cast<u32>(std::clamp(x, 0.0f, static_cast<f32>(resX - 1)));
                const u32 iz = static_cast<u32>(std::clamp(z, 0.0f, static_cast<f32>(resZ - 1)));
                const f32 height = heightmap.sampleNormalized(ix, iz);

                i32 bestDx = 0;
                i32 bestDz = 0;
                f32 lowest = height;

                for (i32 dz = -1; dz <= 1; ++dz) {
                    for (i32 dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dz == 0) continue;
                        const i32 nx = static_cast<i32>(ix) + dx;
                        const i32 nz = static_cast<i32>(iz) + dz;
                        if (nx < 0 || nz < 0 || nx >= static_cast<i32>(resX) ||
                            nz >= static_cast<i32>(resZ)) {
                            continue;
                        }
                        const f32 neighbor =
                            heightmap.sampleNormalized(static_cast<u32>(nx), static_cast<u32>(nz));
                        if (neighbor < lowest) {
                            lowest = neighbor;
                            bestDx = dx;
                            bestDz = dz;
                        }
                    }
                }

                if (bestDx == 0 && bestDz == 0) {
                    const f32 depositAmount = sediment * deposit;
                    heightmap.setNormalized(ix, iz, height + depositAmount);
                    break;
                }

                const f32 slope = height - lowest;
                const f32 capacity = std::max(slope, 0.0001f) * speed * water;
                const f32 erodeAmount = std::min((capacity - sediment) * erode, slope * 0.25f);
                heightmap.setNormalized(ix, iz, height - erodeAmount);
                sediment += erodeAmount;

                x += static_cast<f32>(bestDx);
                z += static_cast<f32>(bestDz);
                water *= 0.98f;
                speed = std::sqrt(speed * speed + slope * 4.0f);
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

class AutoSplatModule final : public ITerrainGeneratorModule {
public:
    const char* name() const override { return "Auto Splat"; }

    void apply(TerrainGeneratorContext& ctx) override {
        if (!ctx.settings.autoSplat) return;

        const auto& terrain = ctx.terrain;
        auto& heightmap = ctx.heightmap;
        auto& splatmap = ctx.splatmap;

        splatmap.resize(terrain.resolutionX, terrain.resolutionZ);

        for (u32 z = 0; z < heightmap.resolutionZ(); ++z) {
            for (u32 x = 0; x < heightmap.resolutionX(); ++x) {
                const f32 u = static_cast<f32>(x) / static_cast<f32>(heightmap.resolutionX() - 1);
                const f32 v = static_cast<f32>(z) / static_cast<f32>(heightmap.resolutionZ() - 1);
                const f32 height = heightmap.sampleNormalized(x, z);
                const Vec3 normal =
                    heightmap.sampleNormalBilinear(u, v, terrain.worldSizeX, terrain.worldSizeZ,
                                                   terrain.maxHeight);
                const f32 slope = 1.0f - std::clamp(normal.y, 0.0f, 1.0f);

                f32 grass = 0.0f;
                f32 rock = 0.0f;
                f32 sand = 0.0f;
                f32 dirt = 0.0f;

                if (height < 0.12f) {
                    sand = 1.0f;
                } else if (slope > 0.45f || height > 0.78f) {
                    rock = 1.0f;
                } else if (height > 0.62f) {
                    dirt = 1.0f;
                } else {
                    grass = 1.0f;
                }

                const f32 blend = std::clamp((slope - 0.35f) * 4.0f, 0.0f, 1.0f);
                grass *= (1.0f - blend);
                rock = std::max(rock, blend * 0.85f);

                const f32 sum = grass + rock + sand + dirt;
                if (sum > 0.0001f) {
                    splatmap.set(x, z, Vec4(grass / sum, rock / sum, sand / sum, dirt / sum));
                } else {
                    splatmap.set(x, z, Vec4(0.0f, 0.0f, 0.0f, 1.0f));
                }
            }
        }
    }
};

BaseNoiseModule g_baseNoise;
SmoothFilterModule g_smooth;
ThermalErosionModule g_thermal;
HydraulicErosionModule g_hydraulic;
HeightQuantizeModule g_quantize;
StylizedCurveModule g_stylized;
AutoSplatModule g_autoSplat;

}  // namespace

void applyGenerationStylePreset(ECS::TerrainGenerationSettings& settings, ECS::TerrainGenStyle style) {
    settings.style = style;
    switch (style) {
        case ECS::TerrainGenStyle::Realistic:
            settings.noiseAlgorithm = TerrainNoiseAlgorithm::Simplex;
            settings.octaves = 6;
            settings.persistence = 0.5f;
            settings.lacunarity = 2.0f;
            settings.amplitude = 0.42f;
            settings.baseHeight = 0.22f;
            settings.domainWarp = true;
            settings.domainWarpStrength = 0.4f;
            settings.thermalErosion = true;
            settings.thermalIterations = 30;
            settings.hydraulicErosion = true;
            settings.hydraulicIterations = 60;
            settings.smoothPass = false;
            settings.heightQuantize = 0.0f;
            settings.autoSplat = true;
            break;
        case ECS::TerrainGenStyle::LowPoly:
            settings.noiseAlgorithm = TerrainNoiseAlgorithm::Value;
            settings.octaves = 3;
            settings.persistence = 0.45f;
            settings.lacunarity = 2.0f;
            settings.amplitude = 0.35f;
            settings.baseHeight = 0.2f;
            settings.domainWarp = false;
            settings.thermalErosion = false;
            settings.hydraulicErosion = false;
            settings.smoothPass = false;
            settings.heightQuantize = 12.0f;
            settings.autoSplat = true;
            break;
        case ECS::TerrainGenStyle::Stylized:
            settings.noiseAlgorithm = TerrainNoiseAlgorithm::Perlin;
            settings.octaves = 4;
            settings.persistence = 0.55f;
            settings.lacunarity = 2.1f;
            settings.amplitude = 0.5f;
            settings.baseHeight = 0.18f;
            settings.domainWarp = false;
            settings.thermalErosion = false;
            settings.hydraulicErosion = false;
            settings.smoothPass = true;
            settings.smoothIterations = 2;
            settings.heightQuantize = 0.0f;
            settings.autoSplat = true;
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
    TerrainGeneratorContext ctx{heightmap, splatmap, terrain, settings};

    TerrainGeneratorPipeline pipeline;
    pipeline.addModule(g_baseNoise);
    pipeline.addModule(g_stylized);
    pipeline.addModule(g_thermal);
    pipeline.addModule(g_hydraulic);
    pipeline.addModule(g_smooth);
    pipeline.addModule(g_quantize);
    pipeline.addModule(g_autoSplat);
    pipeline.run(ctx);
}

}  // namespace Caffeine::Terrain
