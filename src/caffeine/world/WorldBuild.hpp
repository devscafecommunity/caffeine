#pragma once

// Building a composition (doppio/world-composition.md, section 5): one deterministic function the editor, the
// runtime and the headless driver share. It takes the composition, the baked terrain it names (height + masks) and a
// region, and returns the instances of every class, grouped and in a canonical order. Same inputs, same output, on
// any machine and for any way of splitting the region: building two halves gives exactly the instances of the whole.
//
//   1. resolve    every ref -> recipe (exists, right kind, supported version). Any failure: build nothing, list all.
//   2. terrain    the caller bakes the terrain recipe and passes its frame and masks (the graph lives in Doppio).
//   3. water      rivers, optional bed carving, lakes and the `water_distance` mask (`world/WorldWater.hpp`).
//   4. materials  references checked and passed through as bindings.
//   5. classes    the scatter core over the terrain, every class at once: priority + exclusion radius decide
//                 conflicts, the biome terms decide density, the class id's hash is its salt.
//   6. overrides  the edit layer: remove, move (height re-conformed), pin a variant; then the ground context at
//                 each instance and the class's `adapt` mappings (size, lean, tint, wetness, snow).
//   7. output     a `WorldPart`.
//
// An instance belongs to the region where it was generated, also after an `override move`, so the edit layer keeps
// the region-split property.

#include "scatter/Scatter.hpp"
#include "terrain/TerrainMasks.hpp"
#include "world/WorldComposition.hpp"
#include "world/WorldWater.hpp"

#include <functional>
#include <string>
#include <vector>

namespace Caffeine::World {

/// What the project knows about a referenced recipe.
struct RecipeInfo {
    bool found = false;
    RecipeKind kind = RecipeKind::Terrain;
    u32 generatorVersion = 0;
    /// Hash of the recipe file's content: part of the cache key, so editing one recipe invalidates what uses it.
    u64 contentHash = 0;
};

/// Looks a reference up in the project. Must be a pure function of the reference while a build runs.
using RecipeResolver = std::function<RecipeInfo(const std::string& ref)>;

struct WorldBuildRequest {
    const WorldComposition* composition = nullptr;
    /// The `generator_version` of the `.generator.world` recipe.
    u32 generatorVersion = 0;
    /// Null skips the reference checks (tests, a composition not in a project).
    RecipeResolver resolve;
    /// The baked terrain of `composition->terrain`.
    Terrain::TerrainFrame frame;
    /// May be null (built-in masks only).
    const Terrain::TerrainMasks* masks = nullptr;
    /// The region, metres from the terrain's corner. x1 <= x0 or z1 <= z0 means the whole terrain.
    f32 x0 = 0.0f, z0 = 0.0f, x1 = 0.0f, z1 = 0.0f;
    /// Stop after this many instances in all (`truncated` says so). 0 means no limit. A safety stop, not a level of
    /// detail: like Scatter on Terrain, the kept instances are the first chunks of the region, rows then columns.
    u32 maxInstances = 200000;
};

/// What the ground is like under one instance (all 0..1), and the run-time modifiers `adapt` set.
struct InstanceContext {
    f32 slope = 0.0f;
    f32 height = 0.0f;
    /// The `wetness` mask, 0 when the terrain has none.
    f32 wetness = 0.0f;
    f32 waterDistance = 1.0f;
    /// Other generated instances within three radii, over 6 (0 when no class adapts to `neighbours`).
    f32 neighbours = 0.0f;
    f32 wetnessModifier = 0.0f;
    f32 snowModifier = 0.0f;
    /// The values of the class's bake parameters (`WorldClassInstances::params`), in that order.
    f32 params[kMaxBakeParams] = {0.0f, 0.0f, 0.0f, 0.0f};
};

struct WorldClassInstances {
    std::string id;
    /// `classSalt(id)`: the `classId` of every instance below.
    u32 salt = 0;
    std::string ref;
    /// Size of the class's variant pool.
    u32 variants = 1;
    /// Names of the nature recipe parameters the ground context drives (`adapt param.<name>`), at most 4.
    std::vector<std::string> params;
    /// Hero class: bake one model per instance (seeded by `Instance::seed`) instead of using the variant pool.
    bool unique = false;
    std::vector<Scatter::Instance> instances;
    /// One per instance, same order.
    std::vector<InstanceContext> context;
};

struct WorldPart {
    /// False when nothing was built; `errors` says why (every problem found, not only the first).
    bool ok = false;
    std::vector<std::string> errors;
    /// One entry per class, in placement order (priority descending, then salt), whatever the list order.
    std::vector<WorldClassInstances> classes;
    /// The terrain the instances stand on: the baked one, carved by rivers that ask for it.
    Terrain::TerrainHeightmap terrain;
    WaterLayer water;
    std::vector<WorldMaterial> materials;
    /// Instances in the region after the edits (exact unless `truncated`).
    u32 total = 0;
    bool truncated = false;
    /// Key of this build in a cache: seed, region, algorithm version, the composition, every recipe it used and the
    /// baked terrain (heights and masks), so a sculpted terrain never reuses a stale bake.
    u64 cacheKey = 0;
    /// The region actually built (normalised: inside the terrain) and the algorithm version.
    f32 region[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    u32 generatorVersion = 0;
};

WorldPart build(const WorldBuildRequest& request);

/// The `cacheKey` a build of `request` would have, without building: what a cache looks up first. 0 when the
/// request would not build because of a reference or a missing terrain (other problems still give a key).
u64 cacheKeyOf(const WorldBuildRequest& request);

/// Hash of the instance lists (class, cell, variant, position to the millimetre, scale, seed): the golden value tests pin.
u64 instanceHash(const WorldPart& part);

}  // namespace Caffeine::World
