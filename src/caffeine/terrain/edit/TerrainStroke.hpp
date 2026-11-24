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
#include "terrain/TerrainBiome.hpp"
#include "terrain/TerrainHeightmap.hpp"
#include "terrain/TerrainMasks.hpp"
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

/// Copies of every stamp laid about a centre, so a mountain range can be mirrored or a crater pattern repeated.
enum class Symmetry : u8 { None, MirrorX, MirrorZ, MirrorBoth, Radial };

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

    /// The stamp is signed: 0.5 changes nothing, above raises, below lowers (a crater, a valley).
    bool signedStamp = false;

    // --- the newer modes
    /// Terrace: the height of a step, metres.
    f32 stepM = 2.0f;
    /// ErodeThermal: the steepest slope that stays, degrees; `iterations` sliding passes per stamp.
    f32 talusDegrees = 35.0f;
    u32 iterations = 4;
    /// ErodeHydraulic: droplets released per stamp.
    u32 droplets = 24;
    /// Clone: where the relief is copied from, metres from the stamp's centre.
    f32 offsetX = 0.0f;
    f32 offsetZ = 0.0f;

    // --- variation per stamp (deterministic for the seed)
    f32 positionJitter = 0.0f;         // of the radius
    f32 rotationJitterDegrees = 0.0f;  // +- this
    f32 sizeJitter = 0.0f;             // radius scaled by up to +- half of this
    f32 opacityJitter = 0.0f;          // strength reduced by up to this share

    // --- symmetry about a centre (terrain space, metres from the terrain's centre)
    Symmetry symmetry = Symmetry::None;
    u32 radialCount = 4;
    f32 symmetryCenterX = 0.0f;
    f32 symmetryCenterZ = 0.0f;

    /// Protection: the brush acts only where every term's mask is inside its trapezoid (`slope`, `height`, `curvature`,
    /// or a stored mask); the weight falls off across the trapezoid's edges. Empty: everywhere.
    std::vector<BiomeTerm> protect;
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

/// One stamp laid on the ground: where, how big, turned how, and how strong (after jitter and symmetry).
struct StampParams {
    f64 x = 0.0, z = 0.0;
    f64 radius = 1.0;
    f32 rotation = 0.0f;  // radians
    f32 flipU = 1.0f, flipV = 1.0f;
    f32 strength = 0.0f;
    f32 dirX = 0.0f, dirZ = 0.0f;  // the stroke's direction (unit), for Smudge
    f32 offsetX = 0.0f, offsetZ = 0.0f;
};

/// The stamps one logical stamp of a stroke lays: itself, varied by the brush's jitter (deterministic for the seed and
/// the stamp's `index`) and then copied by its symmetry. `dirX`, `dirZ` are the stroke's direction. Shared by the height
/// strokes and the layer painting.
std::vector<StampParams> placeStamp(const StrokeBrush& brush, u32 index, f64 x, f64 z, f32 pressure, f32 dirX, f32 dirZ);

/// The weight of a stamp at `dx`, `dz` (metres from its centre, `dist` away): the falloff times the stamp's picture.
/// Zero outside the radius.
f32 shapeWeight(const StrokeBrush& brush, const StampParams& params, f64 dx, f64 dz, f64 dist);

/// The product of a brush's protection terms at a point of the terrain (1 with none): see `StrokeBrush::protect`.
f32 protectionWeight(const StrokeBrush& brush, const TerrainMasks* masks, const TerrainFrame& frame, f32 x, f32 z);

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
    /// The stored masks a protection term may name (the built-in ones need nothing). Must outlive the stroke.
    void setMasks(const TerrainMasks* masks) { m_masks = masks; }
    /// Ends the stroke and returns its record. The stroke can not be extended after this.
    StrokeRecord finish();

private:
    void stamp(f64 x, f64 z, f32 pressure);
    void applyStamp(const StampParams& params);
    void erode(const StampParams& params, bool thermal, i64 minX, i64 maxX, i64 minZ, i64 maxZ, f64 gridX, f64 gridZ);
    f32 weightAt(const StampParams& params, f64 dx, f64 dz, f64 dist, i64 vx, i64 vz) const;
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
    const TerrainMasks* m_masks = nullptr;
    u32 m_logical = 0;           // stamps asked for (before symmetry)
    bool m_rampSet = false;      // Ramp: where the stroke began
    f64 m_rampX = 0.0, m_rampZ = 0.0;
    f32 m_rampHeight = 0.0f;
    f64 m_pointX = 0.0, m_pointZ = 0.0;  // the latest point of the path
    f32 m_dirX = 0.0f, m_dirZ = 0.0f;    // the direction of the latest stretch
    bool m_flattenSet = false;
    f32 m_flattenTarget = 0.0f;
    bool m_finished = false;
};

}  // namespace Caffeine::Terrain
