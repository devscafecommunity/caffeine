#pragma once

// Scatter on a terrain (photoreal roadmap, phase 2): the deterministic scatter core fed by a terrain's own data: its
// height and normal for placement, its masks through a biome table for density. A pure function of the seed, the
// terrain, the masks, the table and the classes: the same call gives the same instances, in the same order, on any
// machine and for any chunk size; scattering two halves of a region gives exactly the instances of the whole.
//
// Positions are metres from the terrain's corner, the convention of `terrain_height` and the masks.

#include "scatter/Scatter.hpp"
#include "terrain/TerrainBiome.hpp"
#include "terrain/TerrainMasks.hpp"

#include <vector>

namespace Caffeine::Scatter {

struct TerrainScatterRequest {
    u64 seed = 0;
    Terrain::TerrainFrame frame;
    /// May be null (no stored masks; the built-ins still work through the table).
    const Terrain::TerrainMasks* masks = nullptr;
    /// May be null: every class has density 1.
    const Terrain::BiomeTable* biome = nullptr;
    std::vector<ScatterClass> classes;
    /// The region to fill, metres from the corner. x1 <= x0 or z1 <= z0 means the whole terrain.
    f32 x0 = 0.0f, z0 = 0.0f, x1 = 0.0f, z1 = 0.0f;
    /// Chunk side used while generating. The result does not depend on it.
    f32 chunkSize = 64.0f;
    /// Stop after this many instances (`truncated` says so). 0 means no limit.
    u32 maxInstances = 200000;
};

struct TerrainScatterResult {
    std::vector<Instance> instances;
    /// How many instances the region holds in all (more than `instances.size()` when truncated).
    u32 total = 0;
    bool truncated = false;
};

/// Instances inside the region and inside the terrain, ordered by chunk (rows, then columns) and, within a chunk, the
/// canonical order of `scatterChunk`.
TerrainScatterResult scatterTerrain(const TerrainScatterRequest& request);

}  // namespace Caffeine::Scatter
