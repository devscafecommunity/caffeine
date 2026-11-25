#pragma once

// Editing a terrain's vegetation (terrain editing roadmap, phase 3): a brush dragged over it, and the whole-palette
// operations (bake, re-derive, reseed, species edits), all as records that undo and redo exactly.
//
// The brush is the one that sculpts and paints: radius, falloff, stamp picture, jitter, symmetry and protection masks all
// apply, and stamps are laid by spacing in metres along the path (`placeStamp`, `shapeWeight`, `protectionWeight` of
// `TerrainStroke`). What a stamp does depends on the tool:
//   Paint    places instances of one species (rejecting spots where something of higher priority or a manual neighbour
//            is in the way, and, unless forced, where the species' rules fail)
//   Erase    removes what is under it (a manual instance is deleted, a procedural one gets an override)
//   Scale, Rotate, Tint   change what is under it
//   Replace  swaps one species for another (a procedural instance becomes a manual one of the new species)
//   Thin     removes a share of what is under it, evenly
// Every decision is a hash of the stroke's seed and the stamp, so the same path gives the same result at any sampling rate.

#include "ecs/TerrainComponents.hpp"
#include "terrain/TerrainHeightmap.hpp"
#include "terrain/TerrainMasks.hpp"
#include "terrain/edit/TerrainStroke.hpp"
#include "terrain/vegetation/TerrainVegetation.hpp"

#include <unordered_map>
#include <vector>

namespace Caffeine::Terrain {

enum class VegetationTool : u8 { Paint, Erase, Scale, Rotate, Tint, Replace, Thin };

struct VegetationBrush {
    /// Radius, strength (the share of what is under a stamp that an erase, thin or tint takes), spacing, falloff, stamp
    /// picture, jitter, symmetry, seed and protection. `mode` of this struct is not used.
    StrokeBrush shape;
    VegetationTool tool = VegetationTool::Paint;
    /// Paint, Replace: the species placed (palette index).
    u32 species = 0;
    /// Erase, Scale, Rotate, Tint, Thin, Replace: act only on this species (palette index); -1 acts on all.
    i32 onlySpecies = -1;
    /// Paint: candidate instances per 100 m2 per stamp at full weight.
    f32 per100m2 = 2.0f;
    /// Scale: the share added to the scale (0.1 makes it 10 % bigger; negative shrinks). Rotate: degrees.
    f32 amount = 0.1f;
    /// Tint: the colour moved toward, 0..1.
    f32 tint[3] = {1.0f, 1.0f, 1.0f};
    /// Paint: false paints on purpose where the species' rules say no (the instance is then immune to them).
    bool respectRules = true;
};

struct VegManualChange {
    u64 id = 0;
    bool hasBefore = false, hasAfter = false;
    VegetationInstance before, after;
};

struct VegOverrideChange {
    u64 id = 0;
    bool hasBefore = false, hasAfter = false;
    VegetationOverride before, after;
};

/// What an edit changed: enough to undo and redo it exactly.
struct VegetationRecord {
    std::vector<VegManualChange> manual;
    std::vector<VegOverrideChange> overrides;
    // Palette-level changes (bake, species edits, reseed): the state before and after.
    bool speciesChanged = false;
    std::string speciesBefore, speciesAfter;
    bool seedChanged = false;
    u64 seedBefore = 0, seedAfter = 0;
    bool proceduralChanged = false;
    bool proceduralBefore = true, proceduralAfter = true;
    u32 stamps = 0;
    /// The chunks that changed (a rectangle), for the GPU; invalid when nothing placed changed.
    bool hasChunks = false;
    i32 minChunkX = 0, minChunkZ = 0, maxChunkX = 0, maxChunkZ = 0;

    bool empty() const { return manual.empty() && overrides.empty() && !speciesChanged && !seedChanged && !proceduralChanged; }
    usize bytes() const;
    /// A change of species, seed or the procedural switch moves every chunk.
    bool wholeTerrain() const { return speciesChanged || seedChanged || proceduralChanged; }
    void applyBefore(TerrainVegetation& veg) const;
    void applyAfter(TerrainVegetation& veg) const;
};

class VegetationStroke {
public:
    /// `ground` gives the terrain's size and the heights the protection masks and the species' rules read.
    VegetationStroke(TerrainVegetation& veg, const TerrainHeightmap& ground, const ECS::TerrainComponent& settings, VegetationBrush brush,
                     const VegetationField& field);

    /// Extends the path to `point` and lays the stamps that fall on the new stretch. Returns how many.
    u32 addPoint(const StrokePoint& point);
    u32 stamps() const { return m_stamps; }
    void setMasks(const TerrainMasks* masks) { m_masks = masks; }
    VegetationRecord finish();

private:
    void stamp(f64 x, f64 z, f32 pressure);
    void applyStamp(const StampParams& params, u32 stampNumber);
    void touchManual(u64 id);
    void touchOverride(u64 id);
    VegetationOverride overrideOf(u64 id) const;

    TerrainVegetation& m_veg;
    const TerrainHeightmap& m_ground;
    f64 m_sizeX, m_sizeZ;
    f32 m_maxHeight;
    VegetationBrush m_brush;
    VegetationField m_field;
    const TerrainMasks* m_masks = nullptr;
    std::unordered_map<u64, std::pair<bool, VegetationInstance>> m_manualBefore;
    std::unordered_map<u64, std::pair<bool, VegetationOverride>> m_overrideBefore;
    bool m_hasLast = false;
    f64 m_lastX = 0.0, m_lastZ = 0.0;
    f32 m_lastPressure = 1.0f;
    f64 m_carry = 0.0;
    f32 m_dirX = 0.0f, m_dirZ = 0.0f;
    u32 m_stamps = 0, m_logical = 0;
    bool m_finished = false;
};

// --- whole-palette operations. Each returns the record to keep for undo (empty when nothing changed).

/// Replaces the species of the terrain by those of `text` (the palette as text); a species keeps its uid by name match,
/// else it is new. Manual instances of species that are gone are removed (and recorded).
VegetationRecord editSpeciesFromText(TerrainVegetation& veg, const std::string& text, std::string* error);
/// Turns a species' procedural instances into manual ones (under the same overrides) and switches its procedural source
/// off, so what is on the ground stays and can be edited one by one. `field` is the ground; the whole terrain
/// (`extentX`, `extentZ` metres) is baked.
VegetationRecord bakeProcedural(TerrainVegetation& veg, u32 speciesIndex, const VegetationField& field, f32 extentX, f32 extentZ);
/// The opposite: removes the manual instances of a species that were baked (every manual one that is not forced) and switches
/// its procedural source on again.
VegetationRecord rederiveProcedural(TerrainVegetation& veg, u32 speciesIndex);
/// A new world seed: the procedural source grows differently (overrides, keyed by id, find nothing and fall away).
VegetationRecord reseed(TerrainVegetation& veg, u64 seed);

}  // namespace Caffeine::Terrain
