#pragma once

#include "core/Types.hpp"
#include "ecs/Entity.hpp"
#include "ecs/World.hpp"

#include <filesystem>
#include <string>

namespace Caffeine::Editor {

struct ProceduralStreamingSetup {
    u32 seed = 42;
    f32 chunkSize = 64.0f;
    u32 viewRadius = 4;
    const char* scriptPath = "scripts/procedural/custom_director.lua";
    const char* terrainProfile = "hills";
    const char* structureProfile = "none";
    bool bakeInitialChunks = true;
    bool addCamera = true;
};

class ProceduralBridge {
public:
    static std::filesystem::path bundledScriptsDirectory();

    static bool installScriptTemplates(const std::filesystem::path& projectRoot,
                                       std::string& errorOut);

    static bool createStreamingSetup(ECS::World& world, const ProceduralStreamingSetup& setup,
                                     ECS::Entity& outDirector, ECS::Entity& outTerrain,
                                     std::string& errorOut);

    static bool loadBenchmark(ECS::World& world, const char* benchmarkName, u32 seed, f32 chunkSize,
                              u32 viewRadius, ECS::Entity& outDirector, ECS::Entity& outTerrain,
                              std::string& errorOut);

    /// @deprecated Use createStreamingSetup or loadBenchmark.
    static bool createDirector(ECS::World& world, const char* preset, u32 seed, f32 chunkSize,
                               u32 viewRadius, ECS::Entity& outDirector, ECS::Entity& outTerrain,
                               std::string& errorOut);

    static ECS::Entity addStreamingTerrain(ECS::World& world, f32 chunkSize, u32 viewRadius,
                                           const char* terrainProfile, std::string& errorOut);

    static ECS::Entity addDirector(ECS::World& world, const ProceduralStreamingSetup& setup,
                                   u32 terrainEntityId, std::string& errorOut);

    static bool bakeSimpleTerrain(ECS::World& world, ECS::Entity terrain, u32 seed,
                                  const char* terrainProfile, std::string& errorOut);
};

}  // namespace Caffeine::Editor
