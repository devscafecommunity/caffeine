#pragma once

// Terrain masks (photoreal roadmap, phase 1): named 0..1 fields over a terrain that the scatter, the biome table,
// the run-time modifiers and the shaders read, so the world is placed by what the terrain *is* (steep, high, wet)
// and not only by its height.
//
// Two kinds:
//  * **Built-in** masks the engine derives from the heightfield, exactly, with nothing stored:
//      "height"    the normalised height, 0..1
//      "slope"     the angle of the surface from the horizontal, 0 (flat) .. 1 (90 degrees)
//      "curvature" the Laplacian of the surface in 1/m, 0.5 on flat ground and on a plane, towards 1 in a valley
//                  (concave), towards 0 on a ridge (convex); +-0.125 / m is the full range
//  * **Stored** masks a generator sends (erosion wear and deposit, flow, wetness, a painted mask): up to 16, 8 bits
//    each, on a grid of their own (any size, usually the heightfield's), sampled bilinearly. 8 bits is the one
//    precision: a mask is a weight, not a height.
//
// Coordinates are metres from the terrain's corner (the convention of `terrain_height`); a point outside the
// terrain reads the nearest edge.

#include "core/Types.hpp"
#include "math/Vec3.hpp"
#include "terrain/TerrainHeightmap.hpp"

#include <string>
#include <vector>

namespace Caffeine::Terrain {

/// What a mask read needs to know about the terrain it belongs to.
struct TerrainFrame {
    const TerrainHeightmap* heightmap = nullptr;
    f32 sizeX = 1.0f;
    f32 sizeZ = 1.0f;
    f32 maxHeight = 1.0f;
};

class TerrainMasks {
public:
    static constexpr u32 kMaxStoredMasks = 16;
    static constexpr u32 kMaxNameLength = 31;
    static constexpr u32 kBuiltinCount = 3;

    /// Built-in names, in the order `nameAt` lists them first.
    static const char* builtinName(u32 index);
    static bool isBuiltin(const std::string& name);
    /// Letters, digits, '_' and '-', 1 to 31 characters.
    static bool validName(const std::string& name);

    /// Stores (or replaces) a mask. False for a bad or built-in name, a grid under 2 x 2, or when 16 are stored.
    bool set(const std::string& name, u32 resolutionX, u32 resolutionZ, const u8* data);
    bool remove(const std::string& name);
    void clear();
    bool empty() const { return m_stored.empty(); }

    /// Built-ins first, then the stored ones in the order they were first set.
    u32 count() const { return kBuiltinCount + static_cast<u32>(m_stored.size()); }
    std::string nameAt(u32 index) const;
    bool has(const std::string& name) const;
    /// Grid of a stored mask (0 for a built-in or an unknown name).
    u32 resolutionX(const std::string& name) const;
    u32 resolutionZ(const std::string& name) const;

    /// The mask at (x, z) metres from the corner, 0..1. False for an unknown name.
    bool sample(const TerrainFrame& frame, const std::string& name, f32 x, f32 z, f32& out) const;
    /// Resamples a rectangle (x0, z0)-(x1, z1) in metres to a width x height byte grid, row-major, row 0 at z0
    /// (texel centres). False for an unknown name or an empty grid.
    bool rect(const TerrainFrame& frame, const std::string& name, f32 x0, f32 z0, f32 x1, f32 z1, u32 width,
              u32 height, u8* out) const;

    /// The surface normal at (x, z) metres from the corner (unit, y up).
    static Vec3 normalAt(const TerrainFrame& frame, f32 x, f32 z);

    // Persistence (the terrain file's masks section).
    struct Stored {
        std::string name;
        u32 resolutionX = 0;
        u32 resolutionZ = 0;
        std::vector<u8> data;
    };
    const std::vector<Stored>& stored() const { return m_stored; }
    /// A stored mask by name, or null (built-ins are not stored). For editing in place: painting.
    Stored* findStored(const std::string& name);
    const Stored* findStored(const std::string& name) const;

private:
    f32 sampleStored(const Stored& mask, f32 u, f32 v) const;
    std::vector<Stored> m_stored;
};

}  // namespace Caffeine::Terrain
