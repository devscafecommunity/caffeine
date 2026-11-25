#pragma once

// Deterministic scatter of vegetation and rocks over a terrain, one chunk at a time.
//
// A chunk is a pure function of (world seed, chunk coordinates, classes, field): no state, no
// neighbour has to be generated first, any number of threads, any load order. Placement runs on a
// global jittered grid per class (the candidate of a cell never depends on the chunk asking), a
// chunk keeps the points that fall inside it, and conflicts are decided by a **non-recursive**
// rule (a point is dropped when a point with a higher key is closer than the sum of the two
// radii, whether or not that one survives), which only needs a margin of one radius around the
// chunk. All decisions use the integer hash of `core/Hash.hpp`.

#include "core/Hash.hpp"
#include "core/Types.hpp"
#include "math/Vec3.hpp"

#include <functional>
#include <vector>

namespace Caffeine::Scatter {

/// One kind of thing to scatter (a tree species, a rock, a grass tuft).
struct ScatterClass {
    u32 id = 0;
    /// Higher priority wins a conflict (rocks, then trees, then bushes, then grass).
    i32 priority = 0;
    /// Candidate grid spacing in metres: one candidate per cell. Smaller = denser.
    f32 cellSize = 8.0f;
    /// Exclusion radius in metres (a crown or a footprint). Two points conflict when closer than
    /// the sum of their radii.
    f32 radius = 2.0f;
    /// Chance (0..1) multiplied by the field's density at the candidate.
    f32 density = 1.0f;
    f32 minScale = 0.8f;
    f32 maxScale = 1.2f;
    /// How far the up axis leans toward the terrain normal (trees ~0.15, grass 1).
    f32 tiltToNormal = 0.15f;
    /// Metres the base sinks below the terrain, per unit of scale (rocks).
    f32 sink = 0.0f;
    /// Per-channel tint jitter: the tint is 1 +- this.
    f32 tintJitter = 0.06f;
    /// Number of baked variants of the recipe to choose from (>= 1).
    u32 variants = 1;
};

/// The world a scatter reads. Callbacks must be pure functions of their arguments.
struct ScatterField {
    std::function<f32(f32 x, f32 z)> height;
    std::function<Vec3(f32 x, f32 z)> normal;
    /// 0..1 density of a class at a point (a biome or mask value). Null means 1.
    std::function<f32(u32 classId, f32 x, f32 z)> density;
};

struct Instance {
    u32 classId = 0;
    u32 variant = 0;
    Vec3 position;
    /// The up axis of the instance (terrain normal blended with world up).
    Vec3 up{0.0f, 1.0f, 0.0f};
    f32 yaw = 0.0f;
    f32 scale = 1.0f;
    Vec3 tint{1.0f, 1.0f, 1.0f};
    /// Seed for per-instance variation (wind phase, a recipe seed).
    u64 seed = 0;
    /// The grid cell of the class that produced the instance: with `classId`, its stable identity (the key of the
    /// World Composer's edit layer).
    i32 cellX = 0;
    i32 cellZ = 0;
};

/// Instances owned by the chunk `[chunkX, chunkX+1) x [chunkZ, chunkZ+1)` of side `chunkSize`,
/// sorted by (class priority descending, class id, then position) so the order is canonical.
std::vector<Instance> scatterChunk(u64 worldSeed, i32 chunkX, i32 chunkZ, f32 chunkSize,
                                   const std::vector<ScatterClass>& classes,
                                   const ScatterField& field);

}  // namespace Caffeine::Scatter
