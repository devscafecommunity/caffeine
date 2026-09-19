#pragma once

#include "core/Types.hpp"
#include "terrain/generation/TerrainNoise.hpp"

namespace Caffeine::ECS {

enum class TerrainGenStyle : u8 {
    Realistic = 0,
    LowPoly = 1,
    Stylized = 2,
    Custom = 3,
    UltraRealistic = 4,
};

enum class TerrainEnvironment : u8 {
    Custom = 0,
    TropicalWet = 1,
    AridDesert = 2,
    AlpineGlacial = 3,
    Coastal = 4,
    Volcanic = 5,
};

struct TerrainSimulationSettings {
    bool enabled = false;
    TerrainEnvironment environment = TerrainEnvironment::TropicalWet;
    u32 totalIterations = 250;
    u32 dropletsPerIteration = 12;
    u32 maxDropletSteps = 80;
    f32 convergenceThreshold = 0.00008f;
    bool autoConvergence = true;

    f32 tectonicActivity = 0.55f;
    f32 erosionWater = 0.85f;
    f32 erosionThermal = 0.5f;
    f32 erosionGlacial = 0.0f;
    f32 erosionWind = 0.1f;
    f32 erosionBiological = 0.35f;

    f32 temperature = 0.75f;
    f32 humidity = 0.8f;
    f32 windDirection = 180.0f;
    f32 rainfallBase = 0.04f;
};

struct TerrainClimateSettings {
    f32 prevailingWindAngle = 225.0f;
    f32 baseHumidity = 0.7f;
    f32 temperature = 0.6f;
    f32 seaLevel = 0.38f;
};

struct TerrainHydrologySettings {
    bool traceRivers = false;
    u32 maxRiverSources = 48;
    f32 riverSourceMinHeight = 0.35f;
    f32 riverSourceMaxHeight = 0.55f;
    f32 riverCarveStrength = 0.00012f;
    u32 rainShadowSteps = 20;
    f32 waterMoistureRadius = 40.0f;
    f32 sedimentDepositStrength = 0.00008f;
};

struct TerrainGenerationSettings {
    u32 seed = 1337;
    f32 noiseScale = 0.025f;
    u32 octaves = 5;
    f32 persistence = 0.45f;
    f32 lacunarity = 2.0f;
    f32 amplitude = 0.38f;
    f32 baseHeight = 0.28f;
    Terrain::TerrainNoiseAlgorithm noiseAlgorithm = Terrain::TerrainNoiseAlgorithm::Simplex;
    Terrain::TerrainHeightModel heightModel = Terrain::TerrainHeightModel::RollingHills;
    bool useRidgedNoise = false;
    f32 ridgedBlend = 0.25f;
    f32 ridgeOffset = 1.0f;
    f32 ridgeGain = 1.5f;
    f32 multiplicativeContrast = 1.2f;
    f32 fractalRoughness = 0.55f;
    f32 spectralExponent = 2.0f;
    Terrain::TerrainHeightModel multiplyLayerA = Terrain::TerrainHeightModel::RollingHills;
    Terrain::TerrainHeightModel multiplyLayerB = Terrain::TerrainHeightModel::RidgedMountains;
    bool domainWarp = true;
    bool fractalDomainWarp = true;
    f32 domainWarpStrength = 0.12f;
    f32 domainWarpScale = 48.0f;
    u32 domainWarpPasses = 2;
    bool slopeWeighting = false;
    f32 slopeWeightAlpha = 0.06f;
    bool thermalErosion = true;
    u32 thermalIterations = 10;
    f32 thermalTalus = 0.01f;
    bool hydraulicErosion = false;
    u32 hydraulicIterations = 10000;
    u32 hydraulicMaxSteps = 100;
    f32 hydraulicRain = 0.01f;
    f32 hydraulicErode = 0.25f;
    f32 hydraulicDeposit = 0.25f;
    f32 hydraulicInertia = 0.85f;
    f32 hydraulicEvaporation = 0.05f;
    bool microSculpt = true;
    f32 microSculptStrength = 0.42f;
    f32 microSculptSoftness = 0.35f;
    f32 microRiverCarve = 0.0f;
    f32 plateauSharpen = 0.55f;
    bool smoothPass = true;
    u32 smoothIterations = 1;
    u32 postSimSmoothIterations = 4;
    f32 heightQuantize = 0.0f;
    bool autoSplat = true;
    f32 splatBlendRange = 0.28f;
    u32 splatBlurPasses = 2;
    bool useClimateBiomes = true;
    TerrainClimateSettings climate;
    TerrainHydrologySettings hydrology;
    TerrainGenStyle style = TerrainGenStyle::Realistic;
    TerrainSimulationSettings simulation;
};

}  // namespace Caffeine::ECS
