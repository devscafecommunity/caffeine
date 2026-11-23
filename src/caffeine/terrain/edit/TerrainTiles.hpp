#pragma once

// The tile model of terrain editing (terrain editing roadmap, phase 0).
//
// A terrain's height grid is cut into tiles of `kTileCells` cells a side. The tile is the unit everything else of the
// editor speaks in: a stroke marks the tiles it touched, only those tiles' meshes are rebuilt, undo stores the
// tiles' heights before and after, the GI is told which region changed, and the native `.terrain` container stores
// one record per tile.
//
// Real units: positions inside a terrain are metres from its corner, and the tile model also names them in **integer
// centimetres** (`vertexXCm`), the form a stable id is hashed from. Nothing here depends on how finely the heightmap is
// sampled, so resampling a terrain keeps a point's id. A kilometre-wide terrain is addressed by tile first and by a
// small offset inside the tile after, so float precision never limits it.

#include "core/Types.hpp"
#include "terrain/TerrainSplatPainter.hpp"  // TerrainVertexBounds

#include <vector>

namespace Caffeine::Terrain {

/// Cells along a side of one tile. A 257 x 257 terrain is 4 x 4 tiles; 4097 x 4097 is 64 x 64.
inline constexpr u32 kTileCells = 64;

struct TileCoord {
    i32 x = 0;
    i32 z = 0;
    friend bool operator==(const TileCoord& a, const TileCoord& b) { return a.x == b.x && a.z == b.z; }
    friend bool operator<(const TileCoord& a, const TileCoord& b) { return a.z != b.z ? a.z < b.z : a.x < b.x; }
};

class TileGrid {
public:
    TileGrid() = default;
    /// `sizeXMetres` x `sizeZMetres` is the terrain's extent; the grid has `resolutionX` x `resolutionZ` vertices.
    TileGrid(u32 resolutionX, u32 resolutionZ, f64 sizeXMetres, f64 sizeZMetres);

    bool valid() const { return m_resX >= 2 && m_resZ >= 2; }
    u32 resolutionX() const { return m_resX; }
    u32 resolutionZ() const { return m_resZ; }
    u32 tilesX() const { return m_tilesX; }
    u32 tilesZ() const { return m_tilesZ; }
    u32 tileCount() const { return m_tilesX * m_tilesZ; }
    f64 cellSizeX() const { return m_cellX; }  // metres
    f64 cellSizeZ() const { return m_cellZ; }

    u32 index(TileCoord tile) const { return static_cast<u32>(tile.z) * m_tilesX + static_cast<u32>(tile.x); }
    TileCoord coordOf(u32 index) const;
    TileCoord tileOfVertex(u32 vertexX, u32 vertexZ) const;
    /// The vertices a tile owns (inclusive). Tiles own disjoint sets: undo and the container store these.
    TerrainVertexBounds owned(TileCoord tile) const;
    /// What a change in `owned(tile)` can alter in the mesh: the owned vertices plus one on every side (a shared border
    /// vertex belongs to the next tile's triangles, and the normals read a neighbour).
    TerrainVertexBounds meshRegion(TileCoord tile) const;

    /// Integer centimetres from the terrain's corner (rounded), the coordinates a stable id is hashed from.
    i64 vertexXCm(u32 vertexX) const;
    i64 vertexZCm(u32 vertexZ) const;
    /// The tile that holds a point given in centimetres from the corner (clamped to the terrain).
    TileCoord tileAtCm(i64 xCm, i64 zCm) const;

private:
    u32 m_resX = 0, m_resZ = 0;
    u32 m_tilesX = 0, m_tilesZ = 0;
    f64 m_cellX = 0.0, m_cellZ = 0.0;
};

/// The tiles an edit has changed and the mesh, GI and collider have not seen yet.
class DirtyTiles {
public:
    DirtyTiles() = default;
    explicit DirtyTiles(const TileGrid& grid);

    /// Marks every tile whose mesh region the changed vertices touch (the rectangle is grown by one vertex).
    void mark(const TerrainVertexBounds& changed);
    bool any() const { return m_count > 0; }
    u32 count() const { return m_count; }
    bool isDirty(TileCoord tile) const;
    /// Dirty tiles, rows then columns.
    std::vector<TileCoord> tiles() const;
    /// The union of the dirty tiles' mesh regions: one rectangle for `TerrainCache::rebuildRegion`.
    TerrainVertexBounds region() const;
    /// The vertices that actually changed, as one tight rectangle (the union of what `mark` was given). The form the
    /// mesh and the GI take: they grow it themselves by the one vertex a change reaches into the neighbours.
    const TerrainVertexBounds& changed() const { return m_changed; }
    void clear();

private:
    TileGrid m_grid;
    std::vector<u8> m_dirty;
    u32 m_count = 0;
    TerrainVertexBounds m_changed;
};

/// Heights of a tile stored the way the container stores them: 16-bit offsets of 1 cm from a base, which gives 655 m
/// of range per tile. A tile that rises more than that keeps the heights as floats (`wide`), exactly.
struct QuantizedTile {
    i32 baseCm = 0;
    bool wide = false;
    std::vector<u16> offsets;      // when !wide
    std::vector<f32> wideValues;   // when wide: the normalised heights unchanged
    size_t count() const { return wide ? wideValues.size() : offsets.size(); }
};

QuantizedTile quantizeHeights(const f32* normalised, size_t count, f32 maxHeightMetres);
void dequantizeHeights(const QuantizedTile& tile, f32 maxHeightMetres, f32* out);

}  // namespace Caffeine::Terrain
