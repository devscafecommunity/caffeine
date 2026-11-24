#pragma once

// Painting layers on a terrain (terrain editing roadmap, phase 2): a brush dragged over the layer map.
//
// The brush is the same one that sculpts: its radius, falloff, stamp picture, rotation, jitter, symmetry and protection
// masks all apply, and stamps are laid by spacing in metres along the path, never by frame (`placeStamp`, `shapeWeight`
// and `protectionWeight` of `TerrainStroke` are the shared parts). What differs is what a stamp does: it moves weight
// between palette layers (Paint, Erase, Replace, Smooth). A locked layer is never touched.
//
// A stroke records, block by block (64 x 64 texels), what it changed, so undo and redo are exact and small however large
// the terrain is.

#include "ecs/TerrainComponents.hpp"
#include "terrain/TerrainHeightmap.hpp"
#include "terrain/TerrainMasks.hpp"
#include "terrain/edit/TerrainStroke.hpp"
#include "terrain/paint/TerrainLayerMap.hpp"

#include <unordered_map>
#include <vector>

namespace Caffeine::Terrain {

enum class PaintMode : u8 {
    Paint,    ///< moves weight toward `layer`
    Erase,    ///< takes `layer` away, the others fill in
    Replace,  ///< moves the weight of `fromLayer` to `layer`
    Smooth,   ///< blends each texel toward its neighbours
};

struct PaintBrush {
    /// The shape of a stamp: radius, strength (the share moved per stamp at full weight), spacing, falloff, rotation,
    /// stamp picture, jitter, symmetry, seed and protection. `mode` of this struct is not used.
    StrokeBrush shape;
    PaintMode mode = PaintMode::Paint;
    u32 layer = 0;
    u32 fromLayer = 0;
};

struct LayerBounds {
    u32 minX = 0, minZ = 0, maxX = 0, maxZ = 0;
    bool valid = false;
};

/// What one block of texels held before and after a stroke.
struct LayerBlock {
    u32 blockX = 0, blockZ = 0;  // in blocks of `TerrainPaintStroke::kBlock` texels
    u32 minX = 0, minZ = 0, maxX = 0, maxZ = 0;  // the texels the block covers
    std::vector<LayerTexel> before;
    std::vector<LayerTexel> after;
};

/// The blocks a finished stroke changed: enough to undo and redo it exactly.
struct LayerStrokeRecord {
    std::vector<LayerBlock> blocks;
    u32 stamps = 0;
    LayerBounds region;  // the texels that differ, one tight rectangle
    bool empty() const { return blocks.empty(); }
    usize bytes() const;
    LayerBounds applyBefore(TerrainLayerMap& map) const;
    LayerBounds applyAfter(TerrainLayerMap& map) const;
};

/// The blocks that differ between two maps of the same size (what a palette edit that renumbers layers undoes with).
LayerStrokeRecord diffLayerMaps(const TerrainLayerMap& before, const TerrainLayerMap& after);

class TerrainPaintStroke {
public:
    static constexpr u32 kBlock = 64;

    /// `heightmap` and `settings` give the terrain's size and the ground the protection masks read.
    TerrainPaintStroke(TerrainLayerMap& map, const TerrainHeightmap& heightmap, const ECS::TerrainComponent& settings, PaintBrush brush);

    /// Extends the path to `point` and lays the stamps that fall on the new stretch. Returns how many.
    u32 addPoint(const StrokePoint& point);
    u32 stamps() const { return m_stamps; }
    /// The texels changed since the last `clearDirty`: what the GPU has not seen.
    const LayerBounds& dirty() const { return m_dirty; }
    void clearDirty() { m_dirty = {}; }
    /// The stored masks a protection term may name. Must outlive the stroke.
    void setMasks(const TerrainMasks* masks) { m_masks = masks; }
    StrokeBrush& shape() { return m_brush.shape; }
    LayerStrokeRecord finish();

private:
    void stamp(f64 x, f64 z, f32 pressure);
    void applyStamp(const StampParams& params);
    void touch(u32 blockX, u32 blockZ);

    TerrainLayerMap& m_map;
    const TerrainHeightmap& m_height;
    f64 m_sizeX, m_sizeZ, m_stepX, m_stepZ;
    f32 m_maxHeight;
    PaintBrush m_brush;
    const TerrainMasks* m_masks = nullptr;
    std::unordered_map<u64, LayerBlock> m_touched;
    LayerBounds m_dirty;
    bool m_hasLast = false;
    f64 m_lastX = 0.0, m_lastZ = 0.0;
    f32 m_lastPressure = 1.0f;
    f64 m_carry = 0.0;
    f32 m_dirX = 0.0f, m_dirZ = 0.0f;
    u32 m_stamps = 0, m_logical = 0;
    bool m_finished = false;
};

}  // namespace Caffeine::Terrain
