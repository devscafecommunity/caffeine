#pragma once

#include "core/Types.hpp"
#include "core/WorldUnits.hpp"
#include "math/Vec3.hpp"
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

    char texturePath[256] = "hdr-assets-texture/Ground068_1K-JPG-dirt-rock-moss/Ground068_1K-JPG_Color.jpg";
    char normalMapPath[256] = "hdr-assets-texture/Ground068_1K-JPG-dirt-rock-moss/Ground068_1K-JPG_NormalGL.jpg";
    f32 textureTileSize = 8.0f;
    f32 shininess = 16.0f;
    /// Multiplies the sampled splat/terrain albedo. White keeps the source
    /// texture unchanged; use it to tint or darken a terrain.
    Vec3 tint{1.0f, 1.0f, 1.0f};

    bool useSplatmap = true;
    char splatLayerPaths[kTerrainSplatLayerCount][256] = {
        "hdr-assets-texture/Ground068_1K-JPG-dirt-rock-moss/Ground068_1K-JPG_Color.jpg",
        "hdr-assets-texture/Ground108_1K-JPG-dirt-with-rocks/Ground108_1K-JPG_Color.jpg",
        "hdr-assets-texture/Ground079L_1K-JPG-dirt/Ground079L_1K-JPG_Color.jpg",
        "hdr-assets-texture/Ground051_1K-JPG-mud/Ground051_1K-JPG_Color.jpg",
    };
    f32 splatTileSize = 8.0f;

    bool useChunks = true;
    u32 chunkVertexCount = 33;
    u32 maxLodLevels = 2;
    bool frustumCull = true;
    f32 lodDistanceScale = 520.0f;
    f32 lodHysteresis = 0.25f;

    /// Sample step for the collision mesh (1 = full resolution, 4 = ~16x fewer tris).
    u32 collisionSampleStep = 4;
    bool buildCollisionMesh = true;

    char terrainDataPath[256] = {};

    // --- layered painting (terrain editing roadmap, phase 2)
    /// On: the terrain is painted from a palette of up to 64 layers through a layer map (four blended per texel), and
    /// `splatLayerPaths` is ignored. Off: the four layers above and the splat map, as before.
    bool useLayers = false;
    u32 layerRevision = 1;
    /// Project-relative files of the palette (`.terrainpalette`) and the layer map (`.terrainlayers`); empty until saved.
    char palettePath[256] = {};
    char layerMapPath[256] = {};
    /// The layer array on the GPU may take this many megabytes; the sharpness of the layers falls by classes until it fits.
    u32 layerVramBudgetMb = 256;
};

}  // namespace Caffeine::ECS
