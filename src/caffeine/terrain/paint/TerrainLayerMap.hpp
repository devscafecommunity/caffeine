#pragma once

// Which palette layers dress each texel of a terrain (terrain editing roadmap, phase 2).
//
// Every texel names up to **four** palette layers and how much of each (an index map and a weight map, 8 bytes a texel).
// Four is what blends in one pixel; it is also why 64 layers are affordable: a texel, and so a screen pixel, only ever
// reads four of them. The weights of a texel always add up to 255 (a texel is never left bare).
//
// Painting edits weights by shares (`blendToward`, `eraseLayer`, `replaceLayer`, `smoothTexel`), so a small share on a
// soft brush edge does not vanish in a byte: the rounding is **stochastic but deterministic** (a hash of the texel and the
// stamp), unbiased on average and identical on every run. A locked layer's weight is never changed.
//
// The file (`.terrainlayers`) is `TLMP`, a version, the size, the lock mask, the texels and a CRC-32 of the whole.

#include "core/Types.hpp"
#include "terrain/TerrainSplatmap.hpp"

#include <string>
#include <utility>
#include <vector>

namespace Caffeine::Terrain {

struct LayerTexel {
    u8 index[4] = {0, 0, 0, 0};
    u8 weight[4] = {255, 0, 0, 0};
    bool operator==(const LayerTexel& other) const;
};

class TerrainLayerMap {
public:
    static constexpr u32 kSlots = 4;
    static constexpr u32 kMaxLayers = 64;

    TerrainLayerMap() = default;
    /// A map of the given size painted wholly with layer 0.
    TerrainLayerMap(u32 resolutionX, u32 resolutionZ);

    u32 resolutionX() const { return m_resolutionX; }
    u32 resolutionZ() const { return m_resolutionZ; }
    bool empty() const { return m_texels.empty(); }
    usize bytes() const { return m_texels.size() * sizeof(LayerTexel); }

    /// Changes the size, keeping the painting (nearest texel) when `keep` is set, else repainting with layer 0.
    void resize(u32 resolutionX, u32 resolutionZ, bool keep);
    /// Paints every texel wholly with `layer`.
    void fill(u32 layer);

    const LayerTexel& at(u32 x, u32 z) const { return m_texels[static_cast<usize>(z) * m_resolutionX + x]; }
    /// The share (0..1) of `layer` at a texel.
    f32 weightOf(u32 x, u32 z, u32 layer) const;
    /// The layer with the most weight at a texel.
    u32 dominant(u32 x, u32 z) const;
    /// Writes a texel as it is (used by undo); the caller guarantees the weights add up to 255.
    void setRaw(u32 x, u32 z, const LayerTexel& texel) { m_texels[static_cast<usize>(z) * m_resolutionX + x] = texel; }

    // --- locks: a locked layer keeps its weight under every edit below
    bool locked(u32 layer) const { return layer < kMaxLayers && ((m_lockMask >> layer) & 1ull) != 0; }
    void setLocked(u32 layer, bool on);
    u64 lockMask() const { return m_lockMask; }

    // --- editing by shares. `amount` is 0..1; `dither` makes the rounding deterministic (the stamp's number).
    /// Moves `amount` of what is not yet `layer` to `layer` (1 paints it solid).
    void blendToward(u32 x, u32 z, u32 layer, f32 amount, u32 dither);
    /// Takes `amount` of `layer` away and gives it to the other layers in proportion; a texel that is all `layer` stays.
    void eraseLayer(u32 x, u32 z, u32 layer, f32 amount, u32 dither);
    /// Moves `amount` of the weight of `from` to `to`.
    void replaceLayer(u32 x, u32 z, u32 from, u32 to, f32 amount, u32 dither);
    /// Moves the texel `amount` of the way toward the average of its 3 x 3 neighbourhood.
    void smoothTexel(u32 x, u32 z, f32 amount, u32 dither);
    /// What `smoothTexel` would write, without writing it: a stamp computes every texel it covers from the ground as it
    /// was, then writes them, so the result does not depend on the order of the texels.
    LayerTexel smoothed(u32 x, u32 z, f32 amount, u32 dither) const;

    /// Sets a texel from layer weights (any scale): locked layers keep what they have and the given layers share the
    /// rest. This is what auto-layering writes. Fewer than one layer given leaves the texel as it is.
    void assign(u32 x, u32 z, const std::vector<std::pair<u32, f32>>& weights, u32 dither);

    // --- palette edits that renumber layers
    /// Layer `index` is gone: its weight goes to the other layers of each texel (to layer 0 when it stood alone), and
    /// every higher index moves down by one. Locks move with their layers.
    void removeLayer(u32 index);
    /// Layer `from` becomes position `to` (see `TerrainPalette::move`): every index is renumbered.
    void moveLayer(u32 from, u32 to);

    // --- the legacy four-layer splat map
    /// Reads a splat map: layer i of the old map is layer i here.
    static TerrainLayerMap fromSplatmap(const TerrainSplatmap& splat);
    /// The four-layer view of this map: layers 0..3 as channels, anything above folded into the nearest of those by
    /// being dropped and the rest renormalised (what the old renderer can show).
    TerrainSplatmap toSplatmap() const;
    /// How many texels use each layer (any weight above zero), for the palette's usage counts and the VRAM budget.
    std::vector<u32> usage(u32 layerCount) const;

    // --- the file
    std::vector<u8> toBytes() const;
    static bool fromBytes(const u8* data, usize size, TerrainLayerMap& out, std::string* error = nullptr);
    bool save(const std::string& path, std::string* error = nullptr) const;
    static bool load(const std::string& path, TerrainLayerMap& out, std::string* error = nullptr);

    bool operator==(const TerrainLayerMap& other) const;

private:
    struct Mix;  // the layers of one texel as floats, while it is edited
    LayerTexel pack(u32 x, u32 z, Mix& mix, u32 dither) const;
    void store(u32 x, u32 z, Mix& mix, u32 dither) { setRaw(x, z, pack(x, z, mix, dither)); }

    u32 m_resolutionX = 0;
    u32 m_resolutionZ = 0;
    u64 m_lockMask = 0;
    std::vector<LayerTexel> m_texels;
};

}  // namespace Caffeine::Terrain
