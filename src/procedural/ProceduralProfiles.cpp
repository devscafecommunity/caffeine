#include "procedural/ProceduralProfiles.hpp"

#include <cstring>
#include <string>

namespace Caffeine::Procedural {
namespace {

bool strEq(const char* a, const char* b) {
    return a && b && std::strcmp(a, b) == 0;
}

}  // namespace

const char* effectiveTerrainProfile(const ECS::ProceduralWorldComponent& settings) {
    if (settings.terrainProfile[0] != '\0') {
        return settings.terrainProfile;
    }
    if (strEq(settings.preset, "flat") || strEq(settings.preset, "backrooms") ||
        strEq(settings.preset, "endless_race")) {
        return "flat";
    }
    if (strEq(settings.preset, "exploration")) {
        return "hills";
    }
    return "hills";
}

const char* effectiveStructureProfile(const ECS::ProceduralWorldComponent& settings) {
    if (settings.structureProfile[0] != '\0') {
        return settings.structureProfile;
    }
    if (strEq(settings.preset, "backrooms")) return "rooms";
    if (strEq(settings.preset, "endless_race")) return "track";
    if (strEq(settings.preset, "exploration")) return "markers";
    return "none";
}

void applyLegacyPreset(ECS::ProceduralWorldComponent& settings, const char* legacyPreset) {
    if (!legacyPreset || !legacyPreset[0]) return;
    std::strncpy(settings.preset, legacyPreset, sizeof(settings.preset) - 1);
    settings.preset[sizeof(settings.preset) - 1] = '\0';

    if (strEq(legacyPreset, "exploration")) {
        std::strncpy(settings.terrainProfile, "hills", sizeof(settings.terrainProfile) - 1);
        std::strncpy(settings.structureProfile, "markers", sizeof(settings.structureProfile) - 1);
    } else if (strEq(legacyPreset, "backrooms")) {
        std::strncpy(settings.terrainProfile, "flat", sizeof(settings.terrainProfile) - 1);
        std::strncpy(settings.structureProfile, "rooms", sizeof(settings.structureProfile) - 1);
    } else if (strEq(legacyPreset, "endless_race")) {
        std::strncpy(settings.terrainProfile, "flat", sizeof(settings.terrainProfile) - 1);
        std::strncpy(settings.structureProfile, "track", sizeof(settings.structureProfile) - 1);
    } else if (strEq(legacyPreset, "flat")) {
        std::strncpy(settings.terrainProfile, "flat", sizeof(settings.terrainProfile) - 1);
        std::strncpy(settings.structureProfile, "none", sizeof(settings.structureProfile) - 1);
    }
}

void applyBenchmark(ECS::ProceduralWorldComponent& settings, const char* benchmarkName,
                    char* scriptPathOut, std::size_t scriptPathSize) {
    const char* name = (benchmarkName && benchmarkName[0]) ? benchmarkName : "exploration";
    applyLegacyPreset(settings, name);

    if (scriptPathOut && scriptPathSize > 0) {
        const std::string path = std::string("scripts/procedural/") + name + "_director.lua";
        std::strncpy(scriptPathOut, path.c_str(), scriptPathSize - 1);
        scriptPathOut[scriptPathSize - 1] = '\0';
    }
}

}  // namespace Caffeine::Procedural
