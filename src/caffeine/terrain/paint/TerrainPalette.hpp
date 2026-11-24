#pragma once

// The material palette of a terrain (terrain editing roadmap, phase 2): up to 64 layers a terrain can be painted with.
//
// A layer names the pictures that dress the ground (albedo, optionally a normal map), how big one repeat is in metres and
// how sharp its texture must be. Painting refers to a layer by its **index** in the palette, so the order matters and
// removing a layer renumbers the ones after it (the layer map is edited with it: see `TerrainLayerMap::removeLayer`).
//
// A palette is a document: the `.terrainpalette` text file of the project (one block per layer). The terrain's component
// points at it; a terrain without one paints with the four layers of the older splat map.

#include "core/Types.hpp"
#include "ecs/TerrainComponents.hpp"

#include <string>
#include <vector>

namespace Caffeine::Terrain {

inline constexpr u32 kMaxPaletteLayers = 64;
/// The sharpness classes of a layer's picture, pixels a side. The GPU array holds the largest class in use.
inline constexpr u32 kMinLayerResolution = 256;
inline constexpr u32 kMaxLayerResolution = 4096;

struct PaletteLayer {
    std::string name = "Layer";
    /// Project paths (resolved by the texture cache); empty draws a flat colour from `tint`.
    std::string albedo;
    std::string normal;
    /// Metres one repeat of the picture covers.
    f32 tileSizeM = 8.0f;
    /// Turns the picture on the ground, degrees (breaks a visible grid between neighbouring layers).
    f32 rotationDegrees = 0.0f;
    f32 tint[3] = {1.0f, 1.0f, 1.0f};
    /// Pixels a side in the GPU array: a power of two between 256 and 4096.
    u32 resolution = 1024;
    /// Painting never changes the weight of a locked layer.
    bool locked = false;

    bool operator==(const PaletteLayer& other) const;
};

class TerrainPalette {
public:
    const std::vector<PaletteLayer>& layers() const { return m_layers; }
    usize size() const { return m_layers.size(); }
    bool empty() const { return m_layers.empty(); }
    const PaletteLayer* find(u32 index) const { return index < m_layers.size() ? &m_layers[index] : nullptr; }
    PaletteLayer* find(u32 index) { return index < m_layers.size() ? &m_layers[index] : nullptr; }
    /// The index of the layer called `name` (case-insensitive), or -1.
    i32 indexOf(const std::string& name) const;

    /// Appends a layer; returns its index, or -1 when the palette is full (64).
    i32 add(const PaletteLayer& layer);
    /// Removes a layer. The layers after it move down by one.
    bool remove(u32 index);
    /// Moves a layer to a new position (the others shift). Returns false when an index is out of range.
    bool move(u32 from, u32 to);

    /// The sharpness class a resolution is rounded to: the nearest power of two in 256..4096.
    static u32 resolutionClass(u32 resolution);

    /// The text of a `.terrainpalette`: `layer Name` starts a block; `albedo`, `normal`, `tile`, `rotation`, `tint`,
    /// `resolution` and `locked` follow. `#` starts a comment; unknown keys are skipped.
    std::string toText() const;
    /// Reads that text. On failure `error` says why and names the line; the palette is left as it was.
    static bool fromText(const std::string& text, TerrainPalette& out, std::string* error = nullptr);

    /// The four layers of an older terrain (its `splatLayerPaths`) as a palette: index i is the old layer i.
    static TerrainPalette fromLegacyLayers(const ECS::TerrainComponent& terrain);

    bool operator==(const TerrainPalette& other) const { return m_layers == other.m_layers; }

private:
    std::vector<PaletteLayer> m_layers;
};

}  // namespace Caffeine::Terrain
