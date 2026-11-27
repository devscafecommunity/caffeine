#pragma once

// Roads and rivers by spline (terrain editing roadmap, phase 5): a path drawn over the terrain that shapes the ground
// along it, paints a palette layer on it and writes a mask of where it runs.
//
//  * Road: the ground is levelled to a profile taken from the terrain along the path, smoothed over `smoothM` metres
//    and limited to `maxGrade` (rise over run), so a road cuts and fills hills instead of climbing every bump. The
//    corridor is `widthM` wide and blends into the ground over `shoulderM` on each side.
//  * River: a channel of `depthM` with a rounded bed. The bed never rises going downstream (the first point is the
//    source), so water can run along it; the banks blend into the ground over `shoulderM`.
//  * The layer (optional) is painted on the corridor and the mask (optional, a stored mask made when missing) is 1 on
//    the corridor and falls to 0 over the shoulder. A species rule on that mask (a trapezoid that is 1 only where the
//    mask is 0) keeps plants off the road; a rule on a river's mask can put reeds on its banks.
//
// Positions are metres from the terrain's corner (the convention of masks and `terrain_height`). The path is a
// Catmull-Rom curve through the points; the result is a pure function of the terrain and the description, and one
// undoable edit (`SplineRecord`).

#include "ecs/TerrainComponents.hpp"
#include "terrain/TerrainHeightmap.hpp"
#include "terrain/TerrainMasks.hpp"
#include "terrain/edit/TerrainStroke.hpp"
#include "terrain/paint/TerrainLayerMap.hpp"
#include "terrain/paint/TerrainMaskPaint.hpp"
#include "terrain/paint/TerrainPaintStroke.hpp"

#include <string>
#include <vector>

namespace Caffeine::Terrain {

enum class SplineKind : u8 { Road, River };

struct SplinePoint {
    f32 x = 0.0f, z = 0.0f;  // metres from the terrain's corner
    f32 widthM = 0.0f;       // full width here; 0 or less takes the description's
};

struct SplineDesc {
    SplineKind kind = SplineKind::Road;
    std::vector<SplinePoint> points;  // 2 to kMaxPoints
    f32 widthM = 6.0f;
    f32 shoulderM = 4.0f;
    f32 depthM = 1.5f;     // River
    f32 smoothM = 30.0f;   // Road: the window the profile is averaged over
    f32 maxGrade = 0.12f;  // Road: the steepest rise over run of the profile
    i32 layer = -1;        // palette layer painted on the corridor; negative: none
    f32 layerStrength = 1.0f;
    std::string mask;      // stored mask written along it; empty: none
};

inline constexpr u32 kMaxSplinePoints = 256;

/// What a spline changed: enough to undo and redo it exactly.
struct SplineRecord {
    StrokeRecord height;
    LayerStrokeRecord layers;
    MaskRecord mask;
    bool madeMask = false;  // the mask did not exist: `mask` holds the whole mask (a creation)
    f32 lengthM = 0.0f;
    bool empty() const { return height.empty() && layers.empty() && mask.empty(); }
    usize bytes() const;
};

/// The path as a polyline: points every `stepM` metres with the width and the profile height at each.
struct SplineSample {
    f32 x, z, width, height;  // height in metres
    f32 along;                // metres from the start
};

/// Checks a description: false with the reason in `error`.
bool validateSpline(const SplineDesc& desc, const TerrainFrame& frame, std::string* error);

/// Samples the Catmull-Rom path through the points, `stepM` apart (at least 2 samples), with the profile the kind asks
/// for (see the header). `frame` gives the ground the profile is taken from.
std::vector<SplineSample> sampleSpline(const SplineDesc& desc, const TerrainFrame& frame, f32 stepM);

/// Applies the spline to the terrain. `layers` and `masks` may be null when the description asks for none. On success
/// `record` holds what changed (empty when nothing did); on failure nothing was changed.
bool applySpline(TerrainHeightmap& heightmap, TerrainLayerMap* layers, TerrainMasks* masks, const ECS::TerrainComponent& settings,
                 const SplineDesc& desc, SplineRecord& record, std::string* error);

/// Undo and redo. Each returns the height region to rebuild (invalid when the heights did not change) and the layer
/// and mask regions through the out parameters.
struct SplineRegions {
    TerrainVertexBounds height;
    LayerBounds layers;
    LayerBounds mask;
    bool maskWhole = false;
};
SplineRegions revertSpline(const SplineRecord& record, TerrainHeightmap& heightmap, TerrainLayerMap* layers, TerrainMasks* masks);
SplineRegions reapplySpline(const SplineRecord& record, TerrainHeightmap& heightmap, TerrainLayerMap* layers, TerrainMasks* masks);

}  // namespace Caffeine::Terrain
