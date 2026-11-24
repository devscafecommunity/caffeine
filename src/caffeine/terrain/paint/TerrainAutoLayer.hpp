#pragma once

// Auto-layering (terrain editing roadmap, phase 2): paints the layers of a terrain from rules on slope, height and masks.
//
// The rules are a `BiomeTable` (the same text as the vegetation rules: `class 2 base 0.9` and indented `slope 0 0 0.3 0.4`
// terms), where the class number is the **palette layer** index. Each texel's density for every ruled layer is evaluated;
// the layers share the texel in proportion to their density (the strongest four survive). A texel no rule claims takes the
// `fallback` layer. Locked layers keep their weight and the rules share what is left.
//
// The result is a `LayerStrokeRecord`, so one auto-layering is one undo step like a stroke.

#include "ecs/TerrainComponents.hpp"
#include "terrain/TerrainBiome.hpp"
#include "terrain/TerrainHeightmap.hpp"
#include "terrain/TerrainMasks.hpp"
#include "terrain/paint/TerrainPaintStroke.hpp"

namespace Caffeine::Terrain {

struct AutoLayerOptions {
    u32 fallbackLayer = 0;
    /// Texels to cover (the whole map when invalid).
    LayerBounds region;
};

LayerStrokeRecord autoLayer(TerrainLayerMap& map, const TerrainHeightmap& heightmap, const ECS::TerrainComponent& settings,
                            const TerrainMasks* masks, const BiomeTable& rules, const AutoLayerOptions& options = {});

}  // namespace Caffeine::Terrain
