#pragma once

#include "caffeine/plugin/PluginAPI.hpp"
#include <cstdint>

namespace Caffeine::Editor {

// Well-known service names (host may implement these when matching plugins are absent).
inline constexpr const char* kServiceTerrainGenerateUltra = "terrain.generateUltra";
inline constexpr const char* kServiceTerrainImportHeightmap = "terrain.importHeightmap";
inline constexpr const char* kServiceProceduralInstallScripts = "procedural.installScripts";
inline constexpr const char* kServiceProceduralCreateStreamingSetup = "procedural.createStreamingSetup";
inline constexpr const char* kServiceProceduralLoadBenchmark = "procedural.loadBenchmark";
/// @deprecated Use createStreamingSetup or loadBenchmark.
inline constexpr const char* kServiceProceduralCreateDirector = "procedural.createDirector";

struct TerrainGenerateUltraRequest {
    CaffeinePluginU32 entityId = 0;
    char preset[64] = "default";
    CaffeinePluginU32 seed = 42;
    float worldSizeMeters = 256.0f;
    CaffeinePluginU32 gridSize = 257;
};

struct TerrainImportHeightmapRequest {
    CaffeinePluginU32 entityId = 0;
    char heightmapPath[512] = {};
    float worldSizeX = 0.0f;
    float worldSizeZ = 0.0f;
};

struct ProceduralStreamingSetupRequest {
    char scriptPath[256] = "scripts/procedural/custom_director.lua";
    char terrainProfile[64] = "hills";
    char structureProfile[64] = "none";
    CaffeinePluginU32 seed = 42;
    float chunkSizeMeters = 64.0f;
    CaffeinePluginU32 viewRadiusChunks = 4;
};

struct ProceduralLoadBenchmarkRequest {
    char benchmarkName[64] = "exploration";
    CaffeinePluginU32 seed = 42;
    float chunkSizeMeters = 64.0f;
    CaffeinePluginU32 viewRadiusChunks = 4;
};

struct ProceduralSetupResponse {
    CaffeinePluginU32 directorEntityId = 0;
    CaffeinePluginU32 terrainEntityId = 0;
};

using ProceduralCreateDirectorRequest = ProceduralStreamingSetupRequest;
using ProceduralCreateDirectorResponse = ProceduralSetupResponse;

}  // namespace Caffeine::Editor
