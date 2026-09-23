#pragma once

#include "core/Types.hpp"
#include "ecs/Entity.hpp"
#include "ecs/World.hpp"

#include <filesystem>
#include <string>

namespace Caffeine::Editor {

/// Host-side bridge used by the terrain plugin to run the ultra-realistic JS generator.
class TerrainBridge {
public:
    static std::filesystem::path resolveAlgorithmDirectory();

    static bool ensureAlgorithmDependencies(const std::filesystem::path& algorithmDir,
                                            std::string& errorOut);

    static bool generateUltraRealistic(ECS::World& world, ECS::Entity entity,
                                       const char* preset, u32 seed,
                                       f32 worldSizeMeters, u32 gridSize,
                                       std::string& errorOut);

    static bool importHeightmap(ECS::World& world, ECS::Entity entity,
                                const std::filesystem::path& heightmapPath,
                                f32 worldSizeX, f32 worldSizeZ,
                                std::string& errorOut);
};

}  // namespace Caffeine::Editor
