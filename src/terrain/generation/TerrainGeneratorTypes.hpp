#pragma once

#include "core/Types.hpp"
#include "terrain/generation/TerrainNoise.hpp"

namespace Caffeine::ECS {

enum class TerrainGenStyle : u8 {
    Realistic = 0,
    LowPoly = 1,
    Stylized = 2,
    Custom = 3,
};

struct TerrainGenerationSettings {
    u32 seed = 1337;
    f32 noiseScale = 0.02f;
    u32 octaves = 6;
    f32 persistence = 0.5f;
    f32 lacunarity = 2.0f;
    f32 amplitude = 0.45f;
    f32 baseHeight = 0.25f;
    Terrain::TerrainNoiseAlgorithm noiseAlgorithm = Terrain::TerrainNoiseAlgorithm::Simplex;
    bool domainWarp = true;
    f32 domainWarpStrength = 0.35f;
    bool thermalErosion = true;
    u32 thermalIterations = 25;
    f32 thermalTalus = 0.015f;
    bool hydraulicErosion = true;
    u32 hydraulicIterations = 40;
    f32 hydraulicRain = 0.01f;
    f32 hydraulicErode = 0.3f;
    f32 hydraulicDeposit = 0.3f;
    bool smoothPass = false;
    u32 smoothIterations = 1;
    f32 heightQuantize = 0.0f;
    bool autoSplat = true;
    TerrainGenStyle style = TerrainGenStyle::Realistic;
};

}  // namespace Caffeine::ECS
