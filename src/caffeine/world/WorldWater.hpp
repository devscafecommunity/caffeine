#pragma once

// Water as part of a composition (doppio/world-composition.md, phase C2). Step 3 of the build, before any class
// reads a mask:
//
//   1. rivers   every heightfield vertex where the `follow` mask (usually `flow`) is at least `min`;
//   2. carving  a river with `carve <metres>` lowers its bed by that depth (the built-in masks then see the new
//               ground: slope and curvature change along the banks); the surface stays at 70% of the depth;
//   3. lakes    a priority-flood over the (carved) heightfield finds the basins; a vertex is lake when it lies under
//               both its spill height and the lake's normalised `level`; the surface is the lower of the two;
//   4. distance `water_distance`, a stored mask: 0 on water, rising linearly to 1 at `water_distance` metres
//               (64 by default), from an integer chamfer transform so every machine gets the same bytes.
//
// It all runs on the terrain's own grid, on the CPU, over the whole terrain whatever region is built (so regions
// agree): the documented limit is a bounded heightfield (129 to 513 vertices a side today).

#include "core/Types.hpp"
#include "terrain/TerrainHeightmap.hpp"
#include "terrain/TerrainMasks.hpp"
#include "world/WorldComposition.hpp"

#include <string>
#include <vector>

namespace Caffeine::World {

struct WaterBody {
    std::string ref;
    bool lake = false;
    /// 255 where the body covers a heightfield vertex, row-major (z rows), the heightfield's resolution.
    std::vector<u8> coverage;
    /// The water surface in metres at each covered vertex (0 elsewhere).
    std::vector<f32> surface;
    u32 cells = 0;
};

struct WaterLayer {
    u32 resolutionX = 0;
    u32 resolutionZ = 0;
    std::vector<WaterBody> bodies;
    /// The `water_distance` mask, 0..255, the heightfield's resolution.
    std::vector<u8> distance;
    /// True when a river carved the heightfield.
    bool carved = false;
};

/// Builds the water of `composition` over `heights` (normalised, metres = value * frame.maxHeight), carving it in
/// place when a river asks to. `masks` supplies the `follow` masks; `frame` must describe `heights`. Errors (an
/// unknown `follow` mask) are appended to `errors`.
WaterLayer buildWater(const WorldComposition& composition, Terrain::TerrainHeightmap& heights,
                      const Terrain::TerrainFrame& frame, const Terrain::TerrainMasks& masks,
                      std::vector<std::string>& errors);

}  // namespace Caffeine::World
