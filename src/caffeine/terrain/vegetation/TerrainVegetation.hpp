#pragma once

// A terrain's vegetation as terrain data, not entities (terrain editing roadmap, phase 3).
//
// Two **sources** fill it, and they stack:
//   * the **procedural** source: every species with a density grows by itself, deterministic for the world seed
//     (`scatter/Scatter`: a chunk is a pure function of the seed, the chunk and the rules, so any chunk can be made in any
//     order and nothing is stored);
//   * the **manual** source: instances placed by hand (painting, baking), stored one by one.
// An edit of a procedural instance (removed, scaled, turned, tinted, moved) is an **override** keyed by the instance's id,
// so it survives regenerating; an edit of a manual instance changes it in place.
//
// Ids are stable: a procedural id is a hash of the species uid and the instance's position in integer centimetres, a
// manual id of a counter and the position; resampling the heightmap changes neither. Heights are never stored: an instance
// stands on the ground as it is, so **sculpting re-projects the vegetation for free**, and an instance whose spot no longer
// satisfies its species' rules is hidden, not deleted.
//
// Nothing overlaps: instances conflict when closer than the sum of their radii; priority decides (manual wins a tie).

#include "core/Types.hpp"
#include "math/Vec3.hpp"
#include "terrain/vegetation/VegetationSpecies.hpp"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Caffeine::Terrain {

/// A flag of a manual instance.
inline constexpr u8 kVegForced = 1;  ///< painted on purpose: the species' rules do not hide it
inline constexpr u8 kVegBaked = 2;   ///< made from a procedural instance by baking (re-deriving removes it)

struct VegetationInstance {
    u64 id = 0;
    f32 x = 0.0f, z = 0.0f;  ///< metres from the terrain's centre
    u32 species = 0;         ///< the species' uid
    u8 variant = 0;
    u8 flags = 0;
    f32 scale = 1.0f;
    f32 yaw = 0.0f;          ///< radians
    u32 seed = 0;            ///< per-instance variation (wind phase)
    u8 tint[3] = {255, 255, 255};
    bool operator==(const VegetationInstance& other) const;
};

/// What an edit changed on an instance that is made by the procedural source.
struct VegetationOverride {
    bool removed = false;
    f32 scaleMul = 1.0f;
    f32 yawAdd = 0.0f;
    bool hasTint = false;
    u8 tint[3] = {255, 255, 255};
    f32 dx = 0.0f, dz = 0.0f;
    bool operator==(const VegetationOverride& other) const;
    bool isIdentity() const { return !removed && scaleMul == 1.0f && yawAdd == 0.0f && !hasTint && dx == 0.0f && dz == 0.0f; }
};

/// An instance as it stands on the ground now, after overrides.
struct VegetationResolved {
    u64 id = 0;
    u32 species = 0;  ///< index in the palette
    u8 variant = 0;
    bool manual = false;
    f32 x = 0.0f, y = 0.0f, z = 0.0f;
    f32 scale = 1.0f;
    f32 yaw = 0.0f;
    f32 up[3] = {0.0f, 1.0f, 0.0f};
    f32 tint[3] = {1.0f, 1.0f, 1.0f};
};

/// The ground a resolve reads. Callbacks take metres from the terrain's centre and must be pure.
struct VegetationField {
    std::function<f32(f32 x, f32 z)> height;    ///< metres
    std::function<Vec3(f32 x, f32 z)> normal;   ///< unit
    /// 0..1: how well the species at palette index `species` suits the point (its rules). Null: everywhere.
    std::function<f32(u32 species, f32 x, f32 z)> density;
    /// The value 0..1 of a mask (slope, height, curvature or a stored one) at a point; false when there is no such mask. Null:
    /// there are no masks, and adaptation does nothing.
    std::function<bool(const std::string& mask, f32 x, f32 z, f32& value)> mask;
    /// Half the terrain's size in metres: nothing stands beyond it. 0 means unbounded.
    f32 halfX = 0.0f, halfZ = 0.0f;
};

class TerrainVegetation {
public:
    /// Side of a chunk in metres: the unit of generation, of culling and of the GPU buffers.
    static constexpr f32 kChunkM = 32.0f;

    // --- species
    const std::vector<VegetationSpecies>& species() const { return m_species; }
    /// Appends a species (its uid is assigned when it is 0); returns its index, or -1 when the palette is full.
    i32 addSpecies(VegetationSpecies species);
    /// Replaces a species' settings in place (the uid stays): every instance of it follows.
    bool setSpecies(u32 index, const VegetationSpecies& species);
    /// Removes a species and its manual instances; the species after it move down by one.
    bool removeSpecies(u32 index);
    i32 indexOfUid(u32 uid) const;
    const VegetationSpecies* speciesByUid(u32 uid) const;
    /// Replaces the whole palette (an undo, or an edit by text): manual instances of species that are gone are dropped.
    void setAllSpecies(std::vector<VegetationSpecies> species);

    // --- the world seed of the procedural source
    u64 seed() const { return m_seed; }
    void setSeed(u64 seed);
    bool proceduralEnabled() const { return m_procedural; }
    void setProceduralEnabled(bool on);

    // --- manual instances
    usize manualCount() const { return m_manualCount; }
    const VegetationInstance* findManual(u64 id) const;
    /// Adds an instance (its id must be unused); returns false if it is.
    bool addManual(const VegetationInstance& instance);
    bool removeManual(u64 id, VegetationInstance* removed = nullptr);
    /// Replaces the instance with this id (it may move to another chunk).
    bool replaceManual(const VegetationInstance& instance);
    /// A fresh id for a manual instance at a point: unused, a hash of the position in centimetres and a counter.
    u64 newManualId(f32 x, f32 z);
    /// All the manual instances (unspecified order): for baking, saving and tests.
    std::vector<VegetationInstance> allManual() const;

    // --- overrides of procedural instances
    const VegetationOverride* findOverride(u64 id) const;
    /// Sets an override; an identity override removes it.
    void setOverride(u64 id, const VegetationOverride& override);
    usize overrideCount() const { return m_overrides.size(); }
    std::vector<std::pair<u64, VegetationOverride>> allOverrides() const;

    /// Moves when anything that changes what is drawn changes (species, seed, instances, overrides).
    u32 revision() const { return m_revision; }
    void touch() { ++m_revision; }

    /// The id of a procedural instance of a species at a point (metres): a hash, stable across resampling.
    static u64 proceduralId(u32 uid, f32 x, f32 z);
    static u64 chunkKey(i32 cx, i32 cz) { return (static_cast<u64>(static_cast<u32>(cz)) << 32) | static_cast<u32>(cx); }
    static i32 chunkOf(f32 metres);

    /// Everything standing in chunk (`cx`, `cz`): procedural and manual, overrides applied, rules checked, conflicts
    /// resolved. Sorted by species, variant and id.
    void resolveChunk(i32 cx, i32 cz, const VegetationField& field, std::vector<VegetationResolved>& out) const;
    /// Everything within `radius` of a point (the chunks it touches, then filtered).
    void resolveArea(f32 x, f32 z, f32 radius, const VegetationField& field, std::vector<VegetationResolved>& out) const;

    // --- the file (`.terrainveg`): `TVEG`, a version, the seed, the species text, the manual instances, the overrides, a CRC-32
    std::vector<u8> toBytes() const;
    static bool fromBytes(const u8* data, usize size, TerrainVegetation& out, std::string* error = nullptr);
    bool save(const std::string& path, std::string* error = nullptr) const;
    static bool load(const std::string& path, TerrainVegetation& out, std::string* error = nullptr);

private:
    VegetationResolved place(const VegetationSpecies& species, u32 index, u64 id, u8 variant, bool manual, f32 x, f32 z, f32 scale, f32 yaw,
                             const f32* tint, const VegetationField& field) const;

    std::vector<VegetationSpecies> m_species;
    u64 m_seed = 1;
    bool m_procedural = true;
    std::unordered_map<u64, std::vector<VegetationInstance>> m_chunks;  ///< manual instances by chunk
    std::unordered_map<u64, u64> m_where;                               ///< manual id -> chunk key
    usize m_manualCount = 0;
    std::unordered_map<u64, VegetationOverride> m_overrides;
    u32 m_nextUid = 1;
    u64 m_idCounter = 0;
    u32 m_revision = 1;
};

}  // namespace Caffeine::Terrain
