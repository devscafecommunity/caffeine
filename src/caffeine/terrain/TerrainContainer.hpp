#pragma once

// The native `.terrain` container (terrain editing roadmap, phase 0, section 3.5): a `caf` asset of type
// `AssetType::Terrain` that holds a terrain in the tiles of `terrain/edit/TerrainTiles` (64 cells a side, every vertex
// owned by exactly one tile).
//
//   [CafHeader 32 B]  magic 0xCAFECAFE, type Terrain, CRC32 of the payload
//   [metadata]        TerrainFileMeta | mask directory | tile directory (offset, size, CRC32 of every tile)
//   [payload]         one record per tile, rows then columns, each 16-byte aligned
//   [footer]          CRC32 of the whole file (CafWriter)
//
// A tile record carries, for the vertices the tile owns: the heights, the splat weights of the splat texels the tile
// owns and the same sub-rectangle of every stored mask. A reader that wants one tile reads the metadata once, seeks to
// the record, checks the record's own CRC and decodes only it.
//
// **Nothing is lost.** A height tile is stored as 16-bit centimetre offsets (`QuantizedTile`) only when decoding it
// returns every float bit for bit; otherwise the tile keeps its floats ("wide"). The splat weights are stored as 8-bit
// RGBA only when every weight is exactly `byte / 255`, otherwise as four floats; masks are 8-bit already. A legacy
// `.cterrain` therefore converts without changing a bit, and a terrain painted in 8-bit stays compact.
//
// Splat texels and mask texels sit on grids of their own. They are assigned to the tile that holds their position
// (`TileAxis`): the assignment is a partition (no overlap, no gap) and is the identity of `TileGrid::owned` when the
// grid is the height grid.

#include "core/Types.hpp"
#include "terrain/TerrainHeightmap.hpp"
#include "terrain/TerrainMasks.hpp"
#include "terrain/TerrainSplatmap.hpp"
#include "terrain/edit/TerrainTiles.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace Caffeine::Terrain {

/// What a terrain file does not store because the scene's `TerrainComponent` owns it: the extent and the height scale.
/// The legacy `.cterrain` does not carry them either; the container records them so a file stands on its own.
struct TerrainDimensions {
    f64 sizeX = 512.0;
    f64 sizeZ = 512.0;
    f32 maxHeight = 80.0f;
};

enum class TerrainFileFormat { Unknown, Legacy, Native };

/// Looks at the first bytes of a file ("CTRN" is the legacy terrain, a caf header of type Terrain the native one).
TerrainFileFormat detectTerrainFile(const std::filesystem::path& path);

/// The `.terrain` path next to a legacy file (same stem), the one an old project offers to save.
std::filesystem::path nativeTerrainPath(const std::filesystem::path& legacyPath);

/// How the vertices (or texels) of an axis of `resolution` samples map to the `tiles` tiles of a terrain whose height
/// grid has `heightResolution` vertices: texel `s` belongs to the tile holding `s * (heightRes - 1) / (resolution - 1)`
/// (integer maths). Monotonic, so a tile's texels are one contiguous range, possibly empty for a coarse grid.
struct TileAxis {
    struct Span {
        u32 begin = 0;
        u32 end = 0;  // exclusive
        u32 size() const { return end - begin; }
    };
    static std::vector<Span> spans(u32 heightResolution, u32 resolution, u32 tiles);
};

struct TerrainWriteOptions {
    /// Quantise splat weights to 8 bits even when that changes them (the editor's compact save). Default: only when
    /// exact.
    bool quantizeSplat = false;
};

struct TerrainWriteStats {
    u32 tiles = 0;
    u32 wideHeightTiles = 0;    // tiles that kept their floats
    u32 wideSplatTiles = 0;     // tiles whose splat kept its floats
    u64 fileBytes = 0;
};

struct TerrainMaskInfo {
    std::string name;
    u32 resolutionX = 0;
    u32 resolutionZ = 0;
};

struct TerrainFileInfo {
    u32 formatVersion = 0;
    u32 resolutionX = 0;
    u32 resolutionZ = 0;
    TerrainDimensions dimensions;
    u32 tilesX = 0;
    u32 tilesZ = 0;
    bool hasSplat = false;
    u32 splatResolutionX = 0;
    u32 splatResolutionZ = 0;
    std::vector<TerrainMaskInfo> masks;
    u32 tileCount() const { return tilesX * tilesZ; }
};

/// One tile, decoded. `heights` are the normalised heights of the owned vertices, `(owned.maxX - owned.minX + 1)` wide,
/// row-major; `splat` and `masks` cover the tile's span of their own grids (`splatSpanX/Z`, `maskSpan*`).
struct TerrainTileData {
    TileCoord coord;
    TerrainVertexBounds owned;
    bool heightsWide = false;
    bool splatWide = false;
    std::vector<f32> heights;
    u32 splatX0 = 0, splatZ0 = 0, splatW = 0, splatH = 0;
    std::vector<Vec4> splat;
    struct Mask {
        u32 x0 = 0, z0 = 0, w = 0, h = 0;
        std::vector<u8> data;
    };
    std::vector<Mask> masks;
};

class TerrainContainer {
public:
    static constexpr u32 kFormatVersion = 1;
    static constexpr const char* kExtension = ".terrain";

    /// Writes `heightmap` (and splat, masks) as a `.terrain`, through a temporary file renamed on success.
    static bool save(const std::filesystem::path& path, const TerrainHeightmap& heightmap,
                     const TerrainSplatmap& splatmap, bool includeSplat, const TerrainMasks* masks,
                     const TerrainDimensions& dimensions, const TerrainWriteOptions& options = {},
                     TerrainWriteStats* stats = nullptr);

    /// Reads a whole `.terrain` into maps; `hadSplat` mirrors `TerrainSerializer::load` (false: the splat is filled
    /// with layer 0, as for a legacy file without one). Every tile's CRC is checked.
    static bool load(const std::filesystem::path& path, TerrainHeightmap& heightmap, TerrainSplatmap& splatmap,
                     bool* hadSplat = nullptr, TerrainMasks* masks = nullptr, TerrainDimensions* dimensions = nullptr);

    /// Opens a file for tile-by-tile reading (header, directories, metadata CRC). The stream stays open.
    bool open(const std::filesystem::path& path);
    bool isOpen() const { return m_open; }
    const TerrainFileInfo& info() const { return m_info; }
    const TileGrid& grid() const { return m_grid; }

    /// Reads and decodes one tile (its record's CRC must match). False for a tile outside the grid or a corrupted one.
    bool loadTile(TileCoord tile, TerrainTileData& out);
    /// Reads `tiles` only and writes them into `heightmap` / `splatmap`, which are sized to the file's grids when
    /// they are not already (other tiles stay as they are).
    bool loadTilesInto(const std::vector<TileCoord>& tiles, TerrainHeightmap& heightmap, TerrainSplatmap& splatmap);
    /// Checks the whole-file CRC32 of the caf footer (reads the entire file).
    bool verifyWholeFile();

    const std::string& error() const { return m_error; }

private:
    bool fail(const std::string& message);
    bool sizeDestinations(TerrainHeightmap& heightmap, TerrainSplatmap& splatmap) const;
    static void writeTile(const TerrainTileData& tile, const TerrainFileInfo& info, TerrainHeightmap& heightmap,
                          TerrainSplatmap& splatmap);

    struct DirectoryEntry {
        u64 offset = 0;
        u32 size = 0;
        u32 crc = 0;
    };

    std::ifstream m_in;
    bool m_open = false;
    std::string m_error;
    TerrainFileInfo m_info;
    TileGrid m_grid;
    u64 m_payloadStart = 0;
    u64 m_fileSize = 0;
    std::vector<DirectoryEntry> m_directory;
    std::vector<TileAxis::Span> m_heightSpansX, m_heightSpansZ;
    std::vector<TileAxis::Span> m_splatSpansX, m_splatSpansZ;
    std::vector<std::vector<TileAxis::Span>> m_maskSpansX, m_maskSpansZ;
};

/// True when two terrains are the same bit for bit: heights, splat weights, every stored mask (names, grids, bytes).
bool terrainsIdentical(const TerrainHeightmap& a, const TerrainHeightmap& b, const TerrainSplatmap& splatA,
                       const TerrainSplatmap& splatB, const TerrainMasks& masksA, const TerrainMasks& masksB);

struct TerrainConvertReport {
    TerrainWriteStats stats;
    bool hadSplat = false;
    u32 maskCount = 0;
    bool verified = false;
    std::string error;
};

/// Converts a legacy `.cterrain` into a `.terrain`. The legacy reader is only read from. With `verify` the written
/// file is read back and compared bit for bit before the call reports success (the file is removed otherwise).
bool convertLegacyTerrain(const std::filesystem::path& legacyPath, const std::filesystem::path& nativePath,
                          const TerrainDimensions& dimensions, TerrainConvertReport* report = nullptr,
                          bool verify = true);

/// What the engine calls to open a terrain file of either format. A legacy file is read in memory as it is (so an old
/// project opens at once; `fromLegacy` tells the caller it may offer `nativeTerrainPath(path)` to save the new format).
bool loadTerrainAny(const std::filesystem::path& path, TerrainHeightmap& heightmap, TerrainSplatmap& splatmap,
                    bool* hadSplat = nullptr, TerrainMasks* masks = nullptr, bool* fromLegacy = nullptr);

}  // namespace Caffeine::Terrain
