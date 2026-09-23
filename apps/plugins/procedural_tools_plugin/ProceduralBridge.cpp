#include "ProceduralBridge.hpp"

#include "ecs/CameraComponents.hpp"
#include "ecs/Components.hpp"
#include "ecs/MeshComponents.hpp"
#include "ecs/ProceduralComponents.hpp"
#include "ecs/TerrainComponents.hpp"
#include "procedural/ProceduralGenerator.hpp"
#include "procedural/ProceduralProfiles.hpp"
#include "procedural/ProceduralStreamer.hpp"
#include "script/ScriptTypes.hpp"
#include "terrain/TerrainCache.hpp"

#include <cstring>
#include <fstream>

namespace Caffeine::Editor {
namespace {

std::filesystem::path engineSourceRoot() {
#ifdef CAFFEINE_SOURCE_DIR
    return std::filesystem::path(CAFFEINE_SOURCE_DIR);
#else
    return {};
#endif
}

bool copyFileIfMissing(const std::filesystem::path& src, const std::filesystem::path& dst) {
    std::error_code ec;
    if (std::filesystem::exists(dst, ec)) return true;
    std::filesystem::create_directories(dst.parent_path(), ec);
    std::filesystem::copy_file(src, dst, ec);
    return !ec;
}

f32 maxHeightForTerrainProfile(const char* terrainProfile) {
    if (terrainProfile && std::strcmp(terrainProfile, "flat") == 0) {
        return 4.0f;
    }
    return 32.0f;
}

}  // namespace

std::filesystem::path ProceduralBridge::bundledScriptsDirectory() {
    const auto fromSource =
        engineSourceRoot() / "assets" / "plugins" / "procedural" / "scripts";
    if (!fromSource.empty() && std::filesystem::exists(fromSource)) {
        return fromSource;
    }
    return std::filesystem::path("assets/plugins/procedural/scripts");
}

bool ProceduralBridge::installScriptTemplates(const std::filesystem::path& projectRoot,
                                              std::string& errorOut) {
    if (projectRoot.empty()) {
        errorOut = "Project root is empty";
        return false;
    }

    const std::filesystem::path src = bundledScriptsDirectory();
    if (!std::filesystem::exists(src)) {
        errorOut = "Bundled procedural scripts not found: " + src.string();
        return false;
    }

    const std::filesystem::path dst = projectRoot / "scripts" / "procedural";
    std::error_code ec;
    std::filesystem::create_directories(dst, ec);

    for (const auto& entry : std::filesystem::directory_iterator(src)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".lua") continue;
        if (!copyFileIfMissing(entry.path(), dst / entry.path().filename())) {
            errorOut = "Failed to copy " + entry.path().filename().string();
            return false;
        }
    }
    return true;
}

ECS::Entity ProceduralBridge::addStreamingTerrain(ECS::World& world, f32 chunkSize, u32 viewRadius,
                                                  const char* terrainProfile,
                                                  std::string& errorOut) {
    (void)errorOut;
    const char* profile = terrainProfile && terrainProfile[0] ? terrainProfile : "hills";

    ECS::Entity terrain = world.create("ProceduralTerrain");
    ECS::TerrainComponent tc;
    tc.resolutionX = 257;
    tc.resolutionZ = 257;
    tc.worldSizeX = chunkSize * static_cast<f32>(viewRadius * 2 + 1);
    tc.worldSizeZ = tc.worldSizeX;
    tc.maxHeight = maxHeightForTerrainProfile(profile);
    tc.useChunks = true;
    terrain.add<ECS::TerrainComponent>(tc);
    Terrain::TerrainCache::instance().initializeEntity(world, terrain);
    return terrain;
}

ECS::Entity ProceduralBridge::addDirector(ECS::World& world, const ProceduralStreamingSetup& setup,
                                          u32 terrainEntityId, std::string& errorOut) {
    (void)errorOut;
    ECS::Entity director = world.create("ProceduralDirector");

    ECS::ProceduralWorldComponent proc;
    proc.seed = setup.seed;
    proc.chunkSize = setup.chunkSize;
    proc.viewRadius = setup.viewRadius;
    proc.terrainEntityId = terrainEntityId;
    if (setup.terrainProfile) {
        std::strncpy(proc.terrainProfile, setup.terrainProfile, sizeof(proc.terrainProfile) - 1);
    }
    if (setup.structureProfile) {
        std::strncpy(proc.structureProfile, setup.structureProfile,
                     sizeof(proc.structureProfile) - 1);
    }
    if (setup.scriptPath && setup.scriptPath[0]) {
        std::strncpy(proc.customScriptPath, setup.scriptPath, sizeof(proc.customScriptPath) - 1);
    }
    director.add<ECS::ProceduralWorldComponent>(proc);

    Script::ScriptComponent script;
    script.scriptPath = proc.customScriptPath;
    director.add<Script::ScriptComponent>(script);

    if (setup.addCamera) {
        ECS::Camera3DComponent cam;
        director.add<ECS::Camera3DComponent>(cam);
        ECS::CameraActiveComponent active;
        active.is2D = false;
        director.add<ECS::CameraActiveComponent>(active);
        ECS::Transform& t = director.getOrAdd<ECS::Transform>();
        t.position = Vec3(0.0f, 24.0f, -48.0f);
    }

    return director;
}

bool ProceduralBridge::createStreamingSetup(ECS::World& world, const ProceduralStreamingSetup& setup,
                                              ECS::Entity& outDirector, ECS::Entity& outTerrain,
                                              std::string& errorOut) {
    outTerrain = addStreamingTerrain(world, setup.chunkSize, setup.viewRadius, setup.terrainProfile,
                                     errorOut);
    outDirector = addDirector(world, setup, outTerrain.id(), errorOut);

    if (setup.bakeInitialChunks &&
        !bakeSimpleTerrain(world, outTerrain, setup.seed, setup.terrainProfile, errorOut)) {
        return false;
    }

    if (auto* procPtr = outDirector.get<ECS::ProceduralWorldComponent>()) {
        i32 cx = 0;
        i32 cz = 0;
        Procedural::ProceduralStreamer::instance().streamAround(world, outDirector, *procPtr, cx,
                                                              cz);
    }
    return true;
}

bool ProceduralBridge::loadBenchmark(ECS::World& world, const char* benchmarkName, u32 seed,
                                     f32 chunkSize, u32 viewRadius, ECS::Entity& outDirector,
                                     ECS::Entity& outTerrain, std::string& errorOut) {
    ProceduralStreamingSetup setup;
    setup.seed = seed;
    setup.chunkSize = chunkSize;
    setup.viewRadius = viewRadius;
    setup.bakeInitialChunks = true;
    setup.addCamera = true;

    ECS::ProceduralWorldComponent proc;
    char scriptPath[256] = {};
    Procedural::applyBenchmark(proc, benchmarkName, scriptPath, sizeof(scriptPath));
    setup.scriptPath = scriptPath;
    setup.terrainProfile = proc.terrainProfile;
    setup.structureProfile = proc.structureProfile;

    return createStreamingSetup(world, setup, outDirector, outTerrain, errorOut);
}

bool ProceduralBridge::createDirector(ECS::World& world, const char* preset, u32 seed, f32 chunkSize,
                                    u32 viewRadius, ECS::Entity& outDirector, ECS::Entity& outTerrain,
                                    std::string& errorOut) {
    return loadBenchmark(world, preset, seed, chunkSize, viewRadius, outDirector, outTerrain,
                         errorOut);
}

bool ProceduralBridge::bakeSimpleTerrain(ECS::World& world, ECS::Entity terrain, u32 seed,
                                         const char* terrainProfile, std::string& errorOut) {
    (void)errorOut;
    ECS::ProceduralWorldComponent settings;
    settings.seed = seed;
    settings.chunkSize = 64.0f;
    if (terrainProfile) {
        std::strncpy(settings.terrainProfile, terrainProfile, sizeof(settings.terrainProfile) - 1);
    }

    const u32 radius = 2;
    for (i32 cz = -radius; cz <= radius; ++cz) {
        for (i32 cx = -radius; cx <= radius; ++cx) {
            Procedural::ProceduralGenerator::generateTerrainChunk(world, terrain, cx, cz, settings);
        }
    }
    return true;
}

}  // namespace Caffeine::Editor
