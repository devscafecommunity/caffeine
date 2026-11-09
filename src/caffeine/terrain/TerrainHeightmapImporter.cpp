#include "terrain/TerrainHeightmapImporter.hpp"

#include "ecs/TerrainComponents.hpp"
#include "terrain/TerrainCache.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace Caffeine::Terrain {
namespace {

bool parseHeightLine(const std::string& line, std::vector<f32>& row) {
    std::istringstream stream(line);
    f32 value = 0.0f;
    while (stream >> value) {
        row.push_back(value);
    }
    return !row.empty();
}

}  // namespace

HeightmapImportResult loadHeightmapTextFile(const std::filesystem::path& path,
                                            std::vector<f32>& absoluteHeights,
                                            u32& outGridX, u32& outGridZ) {
    HeightmapImportResult result;
    std::ifstream file(path);
    if (!file.is_open()) {
        result.error = "Failed to open heightmap file: " + path.string();
        return result;
    }

    std::vector<std::vector<f32>> rows;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::vector<f32> row;
        if (!parseHeightLine(line, row)) continue;
        rows.push_back(std::move(row));
    }

    if (rows.empty()) {
        result.error = "Heightmap file is empty: " + path.string();
        return result;
    }

    const u32 gridZ = static_cast<u32>(rows.size());
    const u32 gridX = static_cast<u32>(rows[0].size());
    if (gridX < 2 || gridZ < 2) {
        result.error = "Heightmap must be at least 2x2";
        return result;
    }

    for (const auto& row : rows) {
        if (row.size() != gridX) {
            result.error = "Inconsistent row width in heightmap";
            return result;
        }
    }

    absoluteHeights.resize(static_cast<size_t>(gridX) * static_cast<size_t>(gridZ));
    f32 minH = 1e9f;
    f32 maxH = -1e9f;
    for (u32 z = 0; z < gridZ; ++z) {
        for (u32 x = 0; x < gridX; ++x) {
            const f32 h = rows[z][x];
            absoluteHeights[static_cast<size_t>(z) * gridX + x] = h;
            minH = std::min(minH, h);
            maxH = std::max(maxH, h);
        }
    }

    outGridX = gridX;
    outGridZ = gridZ;
    result.ok = true;
    result.gridSize = std::max(gridX, gridZ);
    result.minHeight = minH;
    result.maxHeight = maxH;
    return result;
}

bool applyAbsoluteHeights(ECS::World& world, ECS::Entity entity,
                            const std::vector<f32>& absoluteHeights, u32 gridX, u32 gridZ,
                            f32 worldSizeX, f32 worldSizeZ, f32 minHeight, f32 maxHeight) {
    auto* terrain = world.get<ECS::TerrainComponent>(entity);
    if (!terrain || absoluteHeights.empty() || gridX < 2 || gridZ < 2) return false;

    const f32 range = std::max(maxHeight - minHeight, 0.001f);
    terrain->resolutionX = gridX;
    terrain->resolutionZ = gridZ;
    terrain->worldSizeX = worldSizeX;
    terrain->worldSizeZ = worldSizeZ;
    terrain->maxHeight = range;

    TerrainCache& cache = TerrainCache::instance();
    TerrainHeightmap* hm = cache.heightmapFor(entity);
    if (!hm) {
        cache.initializeEntity(world, entity);
        hm = cache.heightmapFor(entity);
    }
    if (!hm) return false;

    hm->resize(gridX, gridZ, false);
    for (u32 z = 0; z < gridZ; ++z) {
        for (u32 x = 0; x < gridX; ++x) {
            const f32 abs = absoluteHeights[static_cast<size_t>(z) * gridX + x];
            hm->setNormalized(x, z, (abs - minHeight) / range);
        }
    }

    terrain->dataRevision++;
    cache.syncEntity(world, entity);
    return true;
}

bool importHeightmapFile(ECS::World& world, ECS::Entity entity,
                         const std::filesystem::path& path,
                         f32 worldSizeX, f32 worldSizeZ) {
    std::vector<f32> heights;
    u32 gridX = 0;
    u32 gridZ = 0;
    const HeightmapImportResult loaded = loadHeightmapTextFile(path, heights, gridX, gridZ);
    if (!loaded.ok) return false;

    const f32 span = std::max(worldSizeX, worldSizeZ);
    if (worldSizeX <= 0.0f) worldSizeX = span > 0.0f ? span : static_cast<f32>(loaded.gridSize);
    if (worldSizeZ <= 0.0f) worldSizeZ = worldSizeX;

    return applyAbsoluteHeights(world, entity, heights, gridX, gridZ, worldSizeX, worldSizeZ,
                                loaded.minHeight, loaded.maxHeight);
}

}  // namespace Caffeine::Terrain
