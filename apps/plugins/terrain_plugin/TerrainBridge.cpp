#include "TerrainBridge.hpp"

#include "ecs/TerrainComponents.hpp"
#include "terrain/TerrainHeightmapImporter.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace Caffeine::Editor {
namespace {

std::filesystem::path engineSourceRoot() {
#ifdef CAFFEINE_SOURCE_DIR
    return std::filesystem::path(CAFFEINE_SOURCE_DIR);
#else
    return {};
#endif
}

int runShellCommand(const std::string& command) {
    return std::system(command.c_str());
}

bool readMetaFile(const std::filesystem::path& metaPath, f32& worldSize, u32& gridSize,
                  f32& minHeight, f32& maxHeight) {
    std::ifstream file(metaPath);
    if (!file.is_open()) return false;

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    auto findNumber = [&](const char* key, f32& out) -> bool {
        const std::string token = std::string("\"") + key + "\":";
        const size_t pos = content.find(token);
        if (pos == std::string::npos) return false;
        out = std::stof(content.substr(pos + token.size()));
        return true;
    };

    f32 gridF = 0.0f;
    if (!findNumber("worldSize", worldSize)) return false;
    if (!findNumber("gridSize", gridF)) return false;
    gridSize = static_cast<u32>(gridF);
    findNumber("minHeight", minHeight);
    findNumber("maxHeight", maxHeight);
    return gridSize >= 2;
}

}  // namespace

std::filesystem::path TerrainBridge::resolveAlgorithmDirectory() {
    const std::filesystem::path rel =
        "assets/general-ultra-realistic-terrain-algorithm";
    const auto fromSource = engineSourceRoot() / rel;
    if (!fromSource.empty() && std::filesystem::exists(fromSource / "caffeine-export.js")) {
        return fromSource;
    }
    return rel;
}

bool TerrainBridge::ensureAlgorithmDependencies(const std::filesystem::path& algorithmDir,
                                                std::string& errorOut) {
    if (!std::filesystem::exists(algorithmDir / "caffeine-export.js")) {
        errorOut = "Algorithm not found at: " + algorithmDir.string();
        return false;
    }
    if (std::filesystem::exists(algorithmDir / "node_modules/simplex-noise")) {
        return true;
    }

    const std::string installCmd =
        "cd \"" + algorithmDir.string() + "\" && npm install --omit=dev --silent";
    if (runShellCommand(installCmd) != 0) {
        errorOut = "npm install failed in " + algorithmDir.string();
        return false;
    }
    return true;
}

bool TerrainBridge::generateUltraRealistic(ECS::World& world, ECS::Entity entity,
                                           const char* preset, u32 seed,
                                           f32 worldSizeMeters, u32 gridSize,
                                           std::string& errorOut) {
    auto* terrain = world.get<ECS::TerrainComponent>(entity);
    if (!terrain) {
        errorOut = "Entity has no TerrainComponent";
        return false;
    }

    const std::filesystem::path algorithmDir = resolveAlgorithmDirectory();
    if (!ensureAlgorithmDependencies(algorithmDir, errorOut)) {
        return false;
    }

    const std::filesystem::path outDir =
        std::filesystem::temp_directory_path() / "caffeine-terrain-gen";
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);

    const std::filesystem::path heightmapPath = outDir / "heightmap.txt";
    const std::filesystem::path metaPath = outDir / "meta.json";

    std::ostringstream cmd;
    cmd << "cd \"" << algorithmDir.string() << "\" && node caffeine-export.js "
        << (preset ? preset : "default") << " --out \"" << heightmapPath.string() << "\" --meta \""
        << metaPath.string() << "\" --world-size " << worldSizeMeters << " --grid-size " << gridSize
        << " --seed " << seed;

    if (runShellCommand(cmd.str()) != 0) {
        errorOut = "Terrain generator process failed";
        return false;
    }

    f32 metaWorld = worldSizeMeters;
    u32 metaGrid = gridSize;
    f32 minH = 0.0f;
    f32 maxH = terrain->maxHeight;
    if (readMetaFile(metaPath, metaWorld, metaGrid, minH, maxH)) {
        worldSizeMeters = metaWorld;
        gridSize = metaGrid;
    }

    terrain->worldSizeX = worldSizeMeters;
    terrain->worldSizeZ = worldSizeMeters;
    if (maxH > minH) {
        terrain->maxHeight = maxH - minH;
    }

    return importHeightmap(world, entity, heightmapPath, terrain->worldSizeX, terrain->worldSizeZ,
                         errorOut);
}

bool TerrainBridge::importHeightmap(ECS::World& world, ECS::Entity entity,
                                    const std::filesystem::path& heightmapPath,
                                    f32 worldSizeX, f32 worldSizeZ,
                                    std::string& errorOut) {
    if (!std::filesystem::exists(heightmapPath)) {
        errorOut = "Heightmap not found: " + heightmapPath.string();
        return false;
    }
    if (!Terrain::importHeightmapFile(world, entity, heightmapPath, worldSizeX, worldSizeZ)) {
        errorOut = "Failed to import heightmap";
        return false;
    }
    return true;
}

}  // namespace Caffeine::Editor
