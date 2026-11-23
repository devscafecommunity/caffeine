#pragma once

// The stroke engine of terrain editing (terrain editing roadmap, phase 0): a brush dragged over the height grid.
//
// A stroke is a path of points; the brush is laid on it as **stamps spaced a fixed number of metres apart along the
// path**, never one per frame. The same path therefore gives the same terrain at 30 and at 240 samples a second, and a
// slow drag does not pile up more material than a fast one.
//
// Units are real: radius, spacing and positions are in metres; heights change by metres per stamp. Positions are in the
// terrain's own space, metres from its **centre** (the convention of `worldSizeX` / `worldSizeZ` and the mesh).
//
// A stroke records, tile by tile, what it changed (`StrokeRecord`): undo and redo restore exactly those tiles, however
// large the terrain is.

#include "core/Types.hpp"
#include "ecs/TerrainComponents.hpp"
#include "terrain/TerrainHeightmap.hpp"
#include "terrain/TerrainSculptor.hpp"  // TerrainBrushMode
#include "terrain/edit/TerrainTiles.hpp"

#include <unordered_map>
#include <vector>

namespace Caffeine::Terrain {

/// How the weight of a stamp falls from its centre (1) to its edge (0).
enum class FalloffCurve : u8 { Smooth, Linear, Sharp, Constant };

/// A grey-scale image that shapes a stamp (rock strata, a crater, noise...). Empty: a round brush shaped by the falloff
/// alone. The image is square, centred on the stamp and spans the brush's diameter.
struct BrushStamp {
    u32 size = 0;
    std::vector<f32> alpha;  // size * size, 0..1, row-major
    bool empty() const { return size == 0 || alpha.size() < static_cast<size_t>(size) * size; }
    /// `u`, `v` in -1..1 across the diameter; bilinear; 0 outside.
    f32 sample(f32 u, f32 v) const;
};

struct StrokeBrush {
    TerrainBrushMode mode = TerrainBrushMode::Raise;
    f32 radiusM = 6.0f;
    /// Raise, Lower, Noise: metres of height per stamp at full weight. Smooth, Flatten: the share (0..1) moved toward the
    /// average or the target per stamp.
    f32 strength = 0.25f;
    /// Metres between stamps along the path; 0 means a quarter of the radius.
    f32 spacingM = 0.0f;
    FalloffCurve falloff = FalloffCurve::Smooth;
    f32 rotationDegrees = 0.0f;
    /// Flatten: the height to flatten to, in metres; negative takes the height under the first stamp.
    f32 targetHeightM = -1.0f;
    u64 seed = 0;
    BrushStamp stamp;
    /// How much the pen's pressure scales the strength: 0 ignores pressure (mouse), 1 follows it fully. The hook a
    /// tablet will use; the stroke already carries a pressure on every point.
    f32 pressureToStrength = 0.0f;
};

struct StrokePoint {
    f64 x = 0.0;  // metres from the terrain's centre
    f64 z = 0.0;
    f32 pressure = 1.0f;
};

/// What one tile held before and after a stroke.
struct TileDelta {
    TileCoord tile;
    TerrainVertexBounds rect;  // the vertices the tile owns
    std::vector<f32> before;   // rect rows, then columns
    std::vector<f32> after;
};

/// The tiles a finished stroke changed: enough to undo and redo it exactly.
struct StrokeRecord {
    std::vector<TileDelta> tiles;
    u32 stamps = 0;
    TerrainVertexBounds region;  // the vertices that differ, one tight rectangle
    bool empty() const { return tiles.empty(); }
    size_t bytes() const;
    /// Writes the heights back; returns the mesh region to rebuild.
    TerrainVertexBounds applyBefore(TerrainHeightmap& heightmap) const;
    TerrainVertexBounds applyAfter(TerrainHeightmap& heightmap) const;
};

class TerrainStroke {
public:
    TerrainStroke(TerrainHeightmap& heightmap, const ECS::TerrainComponent& settings, StrokeBrush brush);

    /// Extends the path to `point`, laying the stamps that fall on the new stretch. The first point stamps at once.
    /// Returns how many stamps were laid.
    u32 addPoint(const StrokePoint& point);
    u32 stamps() const { return m_stamps; }
    /// The tiles changed since the last `clearDirty`: what the mesh, the GI and the collider have not seen.
    const DirtyTiles& dirty() const { return m_dirty; }
    void clearDirty() { m_dirty.clear(); }
    const TileGrid& grid() const { return m_grid; }
    /// Ends the stroke and returns its record. The stroke can not be extended after this.
    StrokeRecord finish();

private:
    void stamp(f64 x, f64 z, f32 pressure);
    void touch(TileCoord tile);

    TerrainHeightmap& m_map;
    TileGrid m_grid;
    f64 m_sizeX, m_sizeZ;
    f32 m_maxHeight;
    StrokeBrush m_brush;
    DirtyTiles m_dirty;
    std::unordered_map<u32, TileDelta> m_touched;
    bool m_hasLast = false;
    f64 m_lastX = 0.0, m_lastZ = 0.0;
    f32 m_lastPressure = 1.0f;
    f64 m_carry = 0.0;  // metres walked since the last stamp
    u32 m_stamps = 0;
    bool m_flattenSet = false;
    f32 m_flattenTarget = 0.0f;
    bool m_finished = false;
};

}  // namespace Caffeine::Terrain
