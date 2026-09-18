#pragma once

#include "terrain/TerrainHeightmap.hpp"
#include "terrain/TerrainSplatmap.hpp"

#include <filesystem>

namespace Caffeine::Terrain {

class TerrainSerializer {
public:
    static constexpr u32 kSignature = 0x4E525443;  // "CTRN"
    static constexpr u32 kVersion = 1;
    static constexpr u32 kFlagHasSplat = 1u << 0;

    static bool save(const std::filesystem::path& path,
                     const TerrainHeightmap& heightmap,
                     const TerrainSplatmap& splatmap,
                     bool includeSplat);

    static bool load(const std::filesystem::path& path,
                     TerrainHeightmap& heightmap,
                     TerrainSplatmap& splatmap,
                     bool* hadSplat = nullptr);
};

}  // namespace Caffeine::Terrain
