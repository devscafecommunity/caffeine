#pragma once

// The World Composer's composition (doppio/world-composition.md): what a world is made of. A terrain recipe, the
// water bodies cut into it, the material bindings and, in priority order, the classes of things that grow on it,
// each a nature recipe with the rule of where it grows. Plus a sparse edit layer (`override`) applied after
// generation, so hand edits never break determinism.
//
// It is the `graph` text of a `<Name>.generator.world` recipe: every generator owns the text form of its graph, and
// this one shares the grammar of the `.scatter` plan and the biome table (`terrain/TerrainBiome.hpp`), so a class's
// `where` terms are biome terms. `#` starts a comment; indented lines belong to the `class` above them:
//
//     seed 1234
//     terrain Terrain/Alpine.generator.terrain size 1024 1024
//     water_distance 64
//     water Water/River.generator.water follow flow min 0.8 carve 1.5
//     water Water/Lake.generator.water fill basins level 0.18
//     material terrain.layer0 Materials/Moss.generator.material
//     class boulder ref Nature/MossRock.generator.nature variants 6 priority 30 cell 16 radius 2.2 density 0.9 scale 0.7 1.6 tilt 0.8 sink 0.3
//       slope 0.30 0.45 1.00 1.00
//     class oak ref Nature/OakTree.generator.nature variants 8 priority 20 cell 9 radius 3 density 0.8 scale 0.8 1.4 tilt 0.1 tint 0.1
//       slope   0.00 0.00 0.15 0.35
//       wetness 0.20 0.40 0.80 1.00
//       adapt size wetness
//       adapt lean slope 0.3
//       adapt wetness water_distance
//     override remove oak 12 -4
//     override move oak 3 7 120.5 88.25
//     override pin boulder 5 5 2
//
// Class properties: `ref` (required), `variants`, `priority`, `cell`, `radius`, `density`, `scale <min> <max>`,
// `tilt`, `sink`, `tint` (the jitter), `base` (the rule's base density), `unique on|off`. Indented lines are a biome
// term `<mask> <rise0> <rise1> <fall1> <fall0>` or `adapt <size|lean|tint|wetness|snow> <mask|neighbours> [amount]`.
//
// A class is known by its `id` (a word), never by its place in the list: its placement salt is a hash of the id
// (`classSalt`), so reordering classes or adding one does not move any other instance. References are project paths
// to recipes, never to baked files.

#include "core/Types.hpp"
#include "terrain/TerrainBiome.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace Caffeine::World {

/// The recipe kinds a composition references (the last segment of `X.generator.<kind>`).
enum class RecipeKind : u8 { Terrain, Nature, Water, Material, Texture };

const char* recipeKindName(RecipeKind kind);

struct WorldTerrain {
    std::string ref;
    /// Size of the terrain in metres; 0 means "whatever the baked terrain is".
    f32 sizeX = 0.0f;
    f32 sizeZ = 0.0f;
};

struct WorldWater {
    std::string ref;
    /// A river: the mask it follows (usually `flow`) and the value above which water runs.
    std::string follow;
    f32 min = 0.0f;
    /// A lake: fill the basins under a normalised level.
    bool fillBasins = false;
    f32 level = 0.0f;
    /// A river: metres its bed is lowered before the classes read the masks (0 = no carving).
    f32 carve = 0.0f;
};

struct WorldMaterial {
    /// Where the material goes (`terrain.layer0`, `rock.moss`, ...).
    std::string slot;
    std::string ref;
};

/// Drives an instance parameter from the ground context at the instance (`v`, 0..1):
///   size     scale *= 1 + amount * (2v - 1)            (default amount 0.25)
///   lean     tilt toward the normal += amount * v      (0.5)
///   tint     tint *= 1 - amount * v                    (0.2)
///   wetness  the run-time wetness modifier = amount * v (1)
///   snow     the run-time snow modifier = amount * v    (1)
/// The source is any terrain mask (`water_distance` included) or `neighbours`: how crowded the instance is, the
/// count of other generated instances within three of its radii, over 6. Neighbours are counted before the edit
/// layer, so moving or removing an instance never changes another one.
struct WorldAdapt {
    std::string target;
    std::string mask;
    /// Negative means the target's default.
    f32 amount = -1.0f;

    f32 effectiveAmount() const;
};

/// `adapt` targets, in the order the build applies them.
bool isAdaptTarget(std::string_view target);

struct WorldClass {
    /// Letters, digits, '_' and '-'; unique in the composition.
    std::string id;
    std::string ref;
    u32 variants = 1;
    i32 priority = 0;
    f32 cell = 8.0f;
    f32 radius = 2.0f;
    f32 density = 1.0f;
    f32 minScale = 0.8f;
    f32 maxScale = 1.2f;
    f32 tilt = 0.15f;
    f32 sink = 0.0f;
    f32 tint = 0.06f;
    /// Base density of the class's rule (multiplies its terms).
    f32 base = 1.0f;
    /// Hero object: baked per position instead of picked from the variant pool.
    bool unique = false;
    std::vector<Terrain::BiomeTerm> where;
    std::vector<WorldAdapt> adapt;
};

enum class OverrideOp : u8 { Remove, Move, Pin };

/// One hand edit, keyed by the instance's identity (class id, cell).
struct WorldOverride {
    OverrideOp op = OverrideOp::Remove;
    std::string classId;
    i32 cellX = 0;
    i32 cellZ = 0;
    /// `move`: the new position, metres from the terrain's corner (the height follows the terrain).
    f32 x = 0.0f;
    f32 z = 0.0f;
    /// `pin`: the variant to keep.
    u32 variant = 0;
};

/// The placement salt of a class: FNV-1a over the id's bytes, SplitMix64-mixed and folded to 32 bits. The Rust twin
/// is `recipe_ref::content_hash` folded the same way; the golden values in tests/test_world.cpp pin both.
u32 classSalt(std::string_view id);

struct WorldComposition {
    u64 seed = 0;
    WorldTerrain terrain;
    /// Metres at which the `water_distance` mask reaches 1.
    f32 waterDistance = 64.0f;
    std::vector<WorldWater> water;
    std::vector<WorldMaterial> materials;
    std::vector<WorldClass> classes;
    std::vector<WorldOverride> overrides;

    const WorldClass* findClass(std::string_view id) const;

    /// Problems that make the composition unbuildable, independent of the project (no terrain, duplicate or
    /// colliding class ids, a scale range that decreases, an override naming an unknown class or a variant out of
    /// the pool, ...). Empty when it is valid.
    std::vector<std::string> validate() const;

    /// Canonical text: `fromText(toText())` gives the same composition.
    std::string toText() const;
    /// Parses the text form; on a syntax error returns false and says which line in `error`. It does not validate.
    static bool fromText(const std::string& text, WorldComposition& out, std::string* error = nullptr);
};

}  // namespace Caffeine::World
