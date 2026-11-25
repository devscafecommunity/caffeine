#pragma once

// The species palette of a terrain's vegetation (terrain editing roadmap, phase 3): what can grow on it.
//
// A species is one kind of plant or rock: a few baked **variants** (mesh files of the project; a recipe from the Nature
// Generator is baked into them by the editor), how dense it is where it grows by itself, how much room it takes, how
// big it gets, and the rules (slope, height, masks) that say where it may stand. Instances never copy a species: they
// name it by its `uid`, so editing a species reaches every instance already on the map.
//
// The `uid` is stable for the life of the terrain (it is hashed into instance ids and seeds); the *index* in the palette
// is only a position and changes when a species is removed.

#include "core/Types.hpp"
#include "terrain/TerrainBiome.hpp"

#include <string>
#include <vector>

namespace Caffeine::Terrain {

inline constexpr u32 kMaxVegetationSpecies = 256;

/// How the ground changes a plant of a species: a mask (slope, height, curvature or a stored one such as wetness) scales it
/// or tints it. The value 0..1 of the mask at the plant's spot mixes `at0` (mask 0) and `at1` (mask 1): a wet place is greener
/// and bigger, a steep one smaller, a high one stunted. The plant is read against the ground as it is, so sculpting and
/// repainting the masks re-adapts it.
struct VegetationAdapt {
    std::string mask;
    /// 0: multiplies the scale by the mix of `at0[0]` and `at1[0]`; 1: multiplies the tint's channels by the mix of the three.
    u8 target = 0;
    f32 at0[3] = {1.0f, 1.0f, 1.0f};
    f32 at1[3] = {1.0f, 1.0f, 1.0f};
    bool operator==(const VegetationAdapt& other) const;
};

struct VegetationSpecies {
    u32 uid = 0;
    std::string name = "Species";
    /// Project paths of the baked variants (one mesh each); at least one for the species to be drawn.
    std::vector<std::string> meshes;
    /// Optional `.mat` the draws use (a mesh's own materials otherwise).
    std::string material;
    /// A `.mat` per sub-mesh of the variant meshes (a tree's bark and its leaves), in sub-mesh order; a slot without one uses
    /// `material`.
    std::vector<std::string> slotMaterials;
    /// The `.generator.nature` the variants were baked from (the editor re-bakes it when it changes); informational here.
    std::string recipe;
    /// The content hash of the recipe the variants were baked from; the editor compares it to the file to know when to re-bake.
    u64 recipeHash = 0;
    /// Where it grows by itself: instances per hectare (10 000 m2) at full suitability; 0 turns the procedural source off.
    f32 densityPerHa = 200.0f;
    /// Metres of room an instance takes (a crown, a footprint): two instances conflict when closer than the sum of their radii.
    f32 radius = 1.0f;
    /// Higher priority wins a conflict (rocks, then trees, then bushes, then grass).
    i32 priority = 0;
    f32 minScale = 0.8f;
    f32 maxScale = 1.2f;
    /// How far the up axis leans toward the terrain normal (trees about 0.15, grass 1).
    f32 tilt = 0.15f;
    /// Metres the base sinks below the ground, per unit of scale.
    f32 sinkM = 0.0f;
    /// Per-channel tint jitter: the tint is 1 +- this.
    f32 tintJitter = 0.06f;
    f32 tint[3] = {1.0f, 1.0f, 1.0f};
    /// Metres beyond which instances are not drawn.
    f32 maxDistance = 250.0f;
    /// The procedural source: on or off without losing anything else.
    bool procedural = true;
    bool visible = true;
    /// Where it may stand: every term must hold (slope, height, curvature or a stored mask, each 0..1).
    std::vector<BiomeTerm> rules;
    /// How the ground changes a plant's size and colour (see `VegetationAdapt`).
    std::vector<VegetationAdapt> adapt;

    u32 variantCount() const { return meshes.empty() ? 1u : static_cast<u32>(meshes.size()); }
    bool operator==(const VegetationSpecies& other) const;
};

/// The text of a species list: `species Name` starts a block; `uid`, `mesh` (repeatable), `material`, `slot_material`
/// (repeatable), `recipe`, `recipe_hash`, `adapt` (repeatable: `scale <mask> <at 0> <at 1>` or `tint <mask> r g b r g b`),
/// `density`, `radius`, `priority`, `scale`, `tilt`, `sink`, `tint_jitter`, `tint`, `distance`, `procedural`, `visible`
/// and `rule` (repeatable: mask and four numbers) follow. `#` starts a comment; unknown keys are skipped.
std::string vegetationSpeciesToText(const std::vector<VegetationSpecies>& species);
/// Reads that text. On failure `error` names the line; `out` is left as it was.
bool vegetationSpeciesFromText(const std::string& text, std::vector<VegetationSpecies>& out, std::string* error = nullptr);

}  // namespace Caffeine::Terrain
