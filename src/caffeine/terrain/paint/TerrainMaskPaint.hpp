#pragma once

// Painted masks (terrain editing roadmap, phase 5): a stored terrain mask (8 bits, 0..1) edited with the same brush
// that sculpts and paints layers. A painted mask is an ordinary stored mask, so everything that reads masks reads it:
// biome rules, auto-layering, protection terms, vegetation rules and the size and colour adaptations of species.
//
// The brush is the shared one (radius, falloff, stamp picture, rotation, jitter, symmetry, protection; stamps laid by
// spacing in metres, never by frame). What a stamp does: Paint moves the mask toward `value`, Erase toward 0 and
// Smooth toward the average of the neighbours. A stroke computes in floats and writes the rounded byte, so a soft
// brush of low strength keeps its falloff instead of stepping whole levels.
//
// A mask has a grid of its own, so one is made at the resolution it needs. Every edit records what it changed (blocks
// of 64 x 64 texels for a stroke, the whole mask for creating, deleting, filling and inverting), so undo and redo are
// exact.

#include "ecs/TerrainComponents.hpp"
#include "terrain/TerrainHeightmap.hpp"
#include "terrain/TerrainMasks.hpp"
#include "terrain/edit/TerrainStroke.hpp"
#include "terrain/paint/TerrainPaintStroke.hpp"  // LayerBounds

#include <string>
#include <unordered_map>
#include <vector>

namespace Caffeine::Terrain {

enum class MaskPaintMode : u8 {
    Paint,   ///< toward `value`
    Erase,   ///< toward 0
    Smooth,  ///< toward the average of the neighbours
};

struct MaskPaintBrush {
    /// The shape of a stamp; `mode` of this struct is not used.
    StrokeBrush shape;
    MaskPaintMode mode = MaskPaintMode::Paint;
    /// Paint: the level the mask moves toward, 0..1.
    f32 value = 1.0f;
};

/// What one block of texels held before and after a stroke.
struct MaskBlock {
    u32 minX = 0, minZ = 0, maxX = 0, maxZ = 0;  // the texels the block covers
    std::vector<u8> before;
    std::vector<u8> after;
};

/// A whole mask (or its absence): what creating, deleting, filling and inverting undo with.
struct MaskSnapshot {
    bool exists = false;
    u32 resolutionX = 0, resolutionZ = 0;
    std::vector<u8> data;
};

/// An edit of one stored mask, enough to undo and redo it exactly.
struct MaskRecord {
    std::string mask;
    std::vector<MaskBlock> blocks;  // a stroke
    bool whole = false;             // a structural edit: `wholeBefore` and `wholeAfter` hold the mask
    MaskSnapshot wholeBefore, wholeAfter;
    u32 stamps = 0;
    LayerBounds region;  // the texels that differ (a stroke)
    bool empty() const { return !whole && blocks.empty(); }
    usize bytes() const;
    /// Writes the mask back. Returns false when the mask this record belongs to cannot take it (a grid of another size).
    bool applyBefore(TerrainMasks& masks) const;
    bool applyAfter(TerrainMasks& masks) const;
};

/// What a structural edit does to a stored mask.
enum class MaskOp : u8 {
    Fill,    ///< every texel to `value`
    Invert,  ///< 1 - mask
    Delete,  ///< removes the mask
};

/// Makes a stored mask `name` of `resolutionX` x `resolutionZ` texels, all `value` (0..1) or, when `from` names a
/// mask (a built-in one such as "slope" or a stored one), sampled from it. False with the reason in `error` for a bad or
/// taken name, a bad grid, the 17th mask or an unknown `from`.
bool createMask(TerrainMasks& masks, const TerrainFrame& frame, const std::string& name, u32 resolutionX, u32 resolutionZ,
                f32 value, const std::string& from, MaskRecord& record, std::string* error);
/// Fills, inverts or deletes a stored mask. False when it is not a stored mask.
bool editMask(TerrainMasks& masks, const std::string& name, MaskOp op, f32 value, MaskRecord& record, std::string* error);

class TerrainMaskStroke {
public:
    static constexpr u32 kBlock = 64;

    /// `masks` must hold a stored mask `name`; `heightmap` and `settings` give the terrain's size and the ground the
    /// protection masks read.
    TerrainMaskStroke(TerrainMasks& masks, const std::string& name, const TerrainHeightmap& heightmap,
                      const ECS::TerrainComponent& settings, MaskPaintBrush brush);

    /// Extends the path to `point` (metres from the terrain's centre) and lays the stamps on the new stretch.
    u32 addPoint(const StrokePoint& point);
    u32 stamps() const { return m_stamps; }
    /// The texels changed since the last `clearDirty`.
    const LayerBounds& dirty() const { return m_dirty; }
    void clearDirty() { m_dirty = {}; }
    const std::string& mask() const { return m_name; }
    bool valid() const { return m_data != nullptr; }
    MaskRecord finish();

private:
    struct Work {
        MaskBlock block;
        std::vector<f32> value;
    };
    void stamp(f64 x, f64 z, f32 pressure);
    void applyStamp(const StampParams& params);
    Work& touch(u32 blockX, u32 blockZ);
    f32 read(u32 x, u32 z) const;

    TerrainMasks& m_masks;
    std::string m_name;
    TerrainMasks::Stored* m_data = nullptr;
    const TerrainHeightmap& m_height;
    f64 m_sizeX, m_sizeZ, m_stepX, m_stepZ;
    f32 m_maxHeight;
    MaskPaintBrush m_brush;
    std::unordered_map<u64, Work> m_touched;
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
