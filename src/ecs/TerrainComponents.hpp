#pragma once

#include "core/Types.hpp"
#include "core/WorldUnits.hpp"
#include "terrain/generation/TerrainGeneratorTypes.hpp"
#include <cstring>

namespace Caffeine::ECS {

inline constexpr u32 kTerrainSplatLayerCount = 4;

struct TerrainComponent {
    u32 resolutionX = 257;
    u32 resolutionZ = 257;
    u32 splatResolutionScale = 2;
    f32 worldSizeX = Caffeine::WorldUnits::kDefaultTerrainSizeM;
    f32 worldSizeZ = Caffeine::WorldUnits::kDefaultTerrainSizeM;
    f32 maxHeight = Caffeine::WorldUnits::kDefaultTerrainHeightM;
    u32 dataRevision = 1;
    u32 meshRevision = 0;
    u32 splatRevision = 1;
    bool castShadows = true;
    bool receiveShadows = true;

    char texturePath[256] = "kenney_prototype-textures/PNG/Light/texture_07.png";
    f32 textureTileSize = 8.0f;

    bool useSplatmap = true;
    char splatLayerPaths[kTerrainSplatLayerCount][256] = {
        "kenney_prototype-textures/PNG/Green/texture_07.png",
        "kenney_prototype-textures/PNG/Dark/texture_07.png",
        "kenney_prototype-textures/PNG/Orange/texture_07.png",
        "kenney_prototype-textures/PNG/Light/texture_07.png",
    };
    f32 splatTileSize = 8.0f;

    bool useChunks = true;
    u32 chunkVertexCount = 33;
    u32 maxLodLevels = 2;
    bool frustumCull = true;
    f32 lodDistanceScale = 520.0f;
    f32 lodHysteresis = 0.25f;

    char terrainDataPath[256] = {};

    TerrainGenerationSettings generation;
};

}  // namespace Caffeine::ECS
