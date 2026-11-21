#pragma once

#include "terrain/TerrainHeightmap.hpp"
#include "terrain/TerrainMasks.hpp"
#include "terrain/TerrainSplatmap.hpp"

#include <filesystem>

namespace Caffeine::Terrain {

class TerrainSerializer {
public:
    static constexpr u32 kSignature = 0x4E525443;  // "CTRN"
    /// Version 3 adds the masks section; a terrain without stored masks is still written as version 2.
    static constexpr u32 kVersion = 3;
    static constexpr u32 kFlagHasSplat = 1u << 0;
    static constexpr u32 kFlagHasMasks = 1u << 1;

    static bool save(const std::filesystem::path& path,
                     const TerrainHeightmap& heightmap,
                     const TerrainSplatmap& splatmap,
                     bool includeSplat,
                     const TerrainMasks* masks = nullptr);

    static bool load(const std::filesystem::path& path,
                     TerrainHeightmap& heightmap,
                     TerrainSplatmap& splatmap,
                     bool* hadSplat = nullptr,
                     TerrainMasks* masks = nullptr);
};

}  // namespace Caffeine::Terrain
