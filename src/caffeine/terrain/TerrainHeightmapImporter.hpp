#pragma once

#include "core/Types.hpp"
#include "ecs/Entity.hpp"
#include "ecs/World.hpp"
#include "terrain/TerrainHeightmap.hpp"

#include <filesystem>
#include <string>

namespace Caffeine::Terrain {

struct HeightmapImportResult {
    bool ok = false;
    std::string error;
    u32 gridSize = 0;
    f32 worldSize = 0.0f;
    f32 minHeight = 0.0f;
    f32 maxHeight = 0.0f;
};

/// Loads a whitespace-separated height grid (rows = Z, columns = X) in world meters.
HeightmapImportResult loadHeightmapTextFile(const std::filesystem::path& path,
                                            std::vector<f32>& absoluteHeights,
                                            u32& outGridX, u32& outGridZ);

/// Writes absolute heights into a terrain entity heightmap (normalized 0–1) and rebuilds meshes.
bool applyAbsoluteHeights(ECS::World& world, ECS::Entity entity,
                            const std::vector<f32>& absoluteHeights, u32 gridX, u32 gridZ,
                            f32 worldSizeX, f32 worldSizeZ, f32 minHeight, f32 maxHeight);

bool importHeightmapFile(ECS::World& world, ECS::Entity entity,
                         const std::filesystem::path& path,
                         f32 worldSizeX, f32 worldSizeZ);

}  // namespace Caffeine::Terrain
