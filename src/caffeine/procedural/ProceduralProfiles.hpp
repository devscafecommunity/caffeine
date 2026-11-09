#pragma once

#include "ecs/ProceduralComponents.hpp"

namespace Caffeine::Procedural {

/// Resolves terrain/structure profiles, including legacy `preset` field fallback.
const char* effectiveTerrainProfile(const ECS::ProceduralWorldComponent& settings);
const char* effectiveStructureProfile(const ECS::ProceduralWorldComponent& settings);

void applyLegacyPreset(ECS::ProceduralWorldComponent& settings, const char* legacyPreset);

void applyBenchmark(ECS::ProceduralWorldComponent& settings, const char* benchmarkName,
                    char* scriptPathOut, std::size_t scriptPathSize);

}  // namespace Caffeine::Procedural
