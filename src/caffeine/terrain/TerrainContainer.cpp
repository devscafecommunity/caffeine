#include "terrain/TerrainContainer.hpp"

#include "core/io/CafTypes.hpp"
#include "core/io/CafWriter.hpp"
#include "core/io/Crc32.hpp"
#include "terrain/TerrainSerializer.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <system_error>

namespace Caffeine::Terrain {

namespace {

constexpr u32 kTileMagic = 0x454C4954;  // "TILE"
constexpr u32 kFlagSplat = 1u << 0;
constexpr u32 kMaxResolution = 65536;
constexpr u64 kMaxTexels = 1ull << 30;
constexpr u64 kRecordAlign = 16;

enum HeightForm : u8 { kHeightsQuantized = 0, kHeightsWide = 1 };
enum SplatForm : u8 { kSplatNone = 0, kSplatRgba8 = 1, kSplatRgba32f = 2 };

#pragma pack(push, 1)
struct FileMeta {
    u32 formatVersion;
    u32 resolutionX;
    u32 resolutionZ;
    u32 tileCells;
    u32 tilesX;
    u32 tilesZ;
    u32 tileCount;
    u32 flags;
    u32 splatResolutionX;
    u32 splatResolutionZ;
    u32 maskCount;
    f32 maxHeight;
    u32 directoryCrc;  // CRC32 of this struct (field zeroed), the mask directory and the tile directory
    u32 reserved;
    f64 sizeX;
    f64 sizeZ;
};
static_assert(sizeof(FileMeta) == 72, "FileMeta layout is part of the format");

struct MaskEntry {
    char name[32];
    u32 resolutionX;
    u32 resolutionZ;
};
static_assert(sizeof(MaskEntry) == 40, "MaskEntry layout is part of the format");

struct TileEntry {
    u64 offset;  // from the start of the payload
    u32 size;    // bytes of the record, padding excluded
    u32 crc;     // CRC32 of those bytes
};
static_assert(sizeof(TileEntry) == 16, "TileEntry layout is part of the format");

struct TileRecordHeader {
    u32 magic;
    u16 tileX;
    u16 tileZ;
    u8 heightForm;
    u8 splatForm;
    u16 reserved0;
    i32 baseCm;
    u32 heightCount;
    u32 splatCount;
    u32 reserved1;
    u32 reserved2;
};
static_assert(sizeof(TileRecordHeader) == 32, "TileRecordHeader layout is part of the format");
#pragma pack(pop)

static_assert(sizeof(Vec4) == 16, "the wide splat form stores Vec4 as 16 bytes");

template <typename T>
void appendRaw(std::vector<u8>& out, const T* data, size_t count) {
    const u8* bytes = reinterpret_cast<const u8*>(data);
    out.insert(out.end(), bytes, bytes + count * sizeof(T));
}

u32 bitsOf(f32 value) { return std::bit_cast<u32>(value); }

f32 decodeSplatByte(u8 value) { return static_cast<f32>(value) / 255.0f; }

/// The byte whose `decodeSplatByte` is `weight` bit for bit, if there is one.
bool exactSplatByte(f32 weight, u8& out) {
    if (!std::isfinite(weight) || weight < 0.0f || weight > 1.0f) return false;
    out = static_cast<u8>(std::lround(static_cast<f64>(weight) * 255.0));
    return bitsOf(decodeSplatByte(out)) == bitsOf(weight);
}

u8 nearestSplatByte(f32 weight) {
    if (!std::isfinite(weight)) return 0;
    return static_cast<u8>(std::lround(static_cast<f64>(std::clamp(weight, 0.0f, 1.0f)) * 255.0));
}

TerrainVertexBounds fullSpan(const TileAxis::Span& x, const TileAxis::Span& z) {
    TerrainVertexBounds b;
    if (x.size() == 0 || z.size() == 0) return b;
    b.minX = x.begin;
    b.maxX = x.end - 1;
    b.minZ = z.begin;
    b.maxZ = z.end - 1;
    b.valid = true;
    return b;
}

bool sizesAreSane(u32 resX, u32 resZ) {
    return resX >= 2 && resZ >= 2 && resX <= kMaxResolution && resZ <= kMaxResolution &&
           static_cast<u64>(resX) * resZ <= kMaxTexels;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Axis mapping
// ---------------------------------------------------------------------------------------------------------------------

std::vector<TileAxis::Span> TileAxis::spans(u32 heightResolution, u32 resolution, u32 tiles) {
    std::vector<Span> out(tiles);
    if (tiles == 0 || resolution == 0 || heightResolution < 2) return out;
    std::vector<bool> seen(tiles, false);
    const u64 denominator = resolution > 1 ? resolution - 1 : 1;
    for (u32 s = 0; s < resolution; ++s) {
        const u64 v = resolution > 1 ? static_cast<u64>(s) * (heightResolution - 1) / denominator : 0;
        const u32 t = static_cast<u32>(std::min<u64>(v / kTileCells, tiles - 1));
        if (!seen[t]) {
            seen[t] = true;
            out[t].begin = s;
        }
        out[t].end = s + 1;
    }
    return out;
}

// ---------------------------------------------------------------------------------------------------------------------
// File detection
// ---------------------------------------------------------------------------------------------------------------------

TerrainFileFormat detectTerrainFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return TerrainFileFormat::Unknown;
    u8 bytes[10] = {};
    in.read(reinterpret_cast<char*>(bytes), sizeof(bytes));
    if (in.gcount() >= 4) {
        u32 magic = 0;
        std::memcpy(&magic, bytes, sizeof(magic));
        if (magic == TerrainSerializer::kSignature) return TerrainFileFormat::Legacy;
        if (magic == CafHeader::kMagic && in.gcount() >= 10) {
            u16 type = 0;
            std::memcpy(&type, bytes + 8, sizeof(type));
            if (type == static_cast<u16>(AssetType::Terrain)) return TerrainFileFormat::Native;
        }
    }
    return TerrainFileFormat::Unknown;
}

std::filesystem::path nativeTerrainPath(const std::filesystem::path& legacyPath) {
    std::filesystem::path out = legacyPath;
    out.replace_extension(TerrainContainer::kExtension);
    return out;
}

// ---------------------------------------------------------------------------------------------------------------------
// Writer
// ---------------------------------------------------------------------------------------------------------------------

bool TerrainContainer::save(const std::filesystem::path& path, const TerrainHeightmap& heightmap,
                            const TerrainSplatmap& splatmap, bool includeSplat, const TerrainMasks* masks,
                            const TerrainDimensions& dimensions, const TerrainWriteOptions& options,
                            TerrainWriteStats* stats) {
    if (stats) *stats = {};
    if (heightmap.empty() || !sizesAreSane(heightmap.resolutionX(), heightmap.resolutionZ())) return false;

    const u32 resX = heightmap.resolutionX();
    const u32 resZ = heightmap.resolutionZ();
    const TileGrid grid(resX, resZ, dimensions.sizeX, dimensions.sizeZ);
    if (grid.tilesX() > 0xFFFF || grid.tilesZ() > 0xFFFF) return false;

    const bool hasSplat = includeSplat && !splatmap.empty() && sizesAreSane(splatmap.resolutionX(), splatmap.resolutionZ());
    const std::vector<TerrainMasks::Stored> noMasks;
    const std::vector<TerrainMasks::Stored>& storedMasks = masks ? masks->stored() : noMasks;
    for (const TerrainMasks::Stored& mask : storedMasks) {
        if (!sizesAreSane(mask.resolutionX, mask.resolutionZ) || mask.name.empty() ||
            mask.name.size() >= sizeof(MaskEntry::name) ||
            mask.data.size() != static_cast<size_t>(mask.resolutionX) * mask.resolutionZ) {
            return false;
        }
    }

    const std::vector<TileAxis::Span> heightSpansX = TileAxis::spans(resX, resX, grid.tilesX());
    const std::vector<TileAxis::Span> heightSpansZ = TileAxis::spans(resZ, resZ, grid.tilesZ());
    std::vector<TileAxis::Span> splatSpansX, splatSpansZ;
    if (hasSplat) {
        splatSpansX = TileAxis::spans(resX, splatmap.resolutionX(), grid.tilesX());
        splatSpansZ = TileAxis::spans(resZ, splatmap.resolutionZ(), grid.tilesZ());
    }
    std::vector<std::vector<TileAxis::Span>> maskSpansX, maskSpansZ;
    for (const TerrainMasks::Stored& mask : storedMasks) {
        maskSpansX.push_back(TileAxis::spans(resX, mask.resolutionX, grid.tilesX()));
        maskSpansZ.push_back(TileAxis::spans(resZ, mask.resolutionZ, grid.tilesZ()));
    }

    TerrainWriteStats local;
    std::vector<u8> payload;
    std::vector<TileEntry> directory(grid.tileCount());
    std::vector<f32> gathered;
    std::vector<f32> decoded;

    for (u32 index = 0; index < grid.tileCount(); ++index) {
        const TileCoord tile = grid.coordOf(index);
        const TerrainVertexBounds owned = grid.owned(tile);
        const u32 width = owned.maxX - owned.minX + 1;
        const u32 rows = owned.maxZ - owned.minZ + 1;

        // Heights: quantised only when decoding returns every bit.
        gathered.resize(static_cast<size_t>(width) * rows);
        for (u32 z = 0; z < rows; ++z) {
            const f32* src = heightmap.heights().data() + static_cast<size_t>(owned.minZ + z) * resX + owned.minX;
            std::copy(src, src + width, gathered.begin() + static_cast<size_t>(z) * width);
        }
        bool finite = dimensions.maxHeight > 0.0f && std::isfinite(dimensions.maxHeight);
        for (size_t i = 0; finite && i < gathered.size(); ++i) finite = std::isfinite(gathered[i]);
        QuantizedTile quantized;
        quantized.wide = true;
        if (finite) {
            quantized = quantizeHeights(gathered.data(), gathered.size(), dimensions.maxHeight);
            if (!quantized.wide) {
                decoded.resize(gathered.size());
                dequantizeHeights(quantized, dimensions.maxHeight, decoded.data());
                if (std::memcmp(decoded.data(), gathered.data(), gathered.size() * sizeof(f32)) != 0) {
                    quantized.wide = true;
                }
            }
        }
        if (quantized.wide) ++local.wideHeightTiles;

        // Splat of the texels the tile owns.
        TerrainVertexBounds splatSpan;
        std::vector<u8> splatBytes;
        u8 splatForm = kSplatNone;
        u32 splatCount = 0;
        if (hasSplat) {
            splatSpan = fullSpan(splatSpansX[static_cast<size_t>(tile.x)], splatSpansZ[static_cast<size_t>(tile.z)]);
            if (splatSpan.valid) {
                const u32 w = splatSpan.maxX - splatSpan.minX + 1;
                const u32 h = splatSpan.maxZ - splatSpan.minZ + 1;
                splatCount = w * h;
                const u32 splatResX = splatmap.resolutionX();
                std::vector<u8> bytes(static_cast<size_t>(splatCount) * 4);
                bool exact = true;
                for (u32 z = 0; z < h && exact; ++z) {
                    for (u32 x = 0; x < w; ++x) {
                        const Vec4& v = splatmap.weights()[static_cast<size_t>(splatSpan.minZ + z) * splatResX + splatSpan.minX + x];
                        const f32 comps[4] = {v.x, v.y, v.z, v.w};
                        u8* dst = bytes.data() + (static_cast<size_t>(z) * w + x) * 4;
                        for (int c = 0; c < 4; ++c) {
                            if (options.quantizeSplat) {
                                dst[c] = nearestSplatByte(comps[c]);
                            } else if (!exactSplatByte(comps[c], dst[c])) {
                                exact = false;
                                break;
                            }
                        }
                        if (!exact) break;
                    }
                }
                if (exact) {
                    splatForm = kSplatRgba8;
                    splatBytes = std::move(bytes);
                } else {
                    splatForm = kSplatRgba32f;
                    splatBytes.resize(static_cast<size_t>(splatCount) * sizeof(Vec4));
                    for (u32 z = 0; z < h; ++z) {
                        const Vec4* src = splatmap.weights().data() + static_cast<size_t>(splatSpan.minZ + z) * splatResX + splatSpan.minX;
                        std::memcpy(splatBytes.data() + static_cast<size_t>(z) * w * sizeof(Vec4), src, static_cast<size_t>(w) * sizeof(Vec4));
                    }
                    ++local.wideSplatTiles;
                }
            }
        }

        // Record: header, heights, splat, then the tile's rectangle of every mask.
        const u64 recordStart = payload.size();
        TileRecordHeader header{};
        header.magic = kTileMagic;
        header.tileX = static_cast<u16>(tile.x);
        header.tileZ = static_cast<u16>(tile.z);
        header.heightForm = quantized.wide ? kHeightsWide : kHeightsQuantized;
        header.splatForm = splatForm;
        header.baseCm = quantized.wide ? 0 : quantized.baseCm;
        header.heightCount = static_cast<u32>(gathered.size());
        header.splatCount = splatCount;
        appendRaw(payload, &header, 1);
        if (quantized.wide) {
            appendRaw(payload, gathered.data(), gathered.size());
        } else {
            appendRaw(payload, quantized.offsets.data(), quantized.offsets.size());
        }
        payload.insert(payload.end(), splatBytes.begin(), splatBytes.end());
        for (size_t m = 0; m < storedMasks.size(); ++m) {
            const TerrainMasks::Stored& mask = storedMasks[m];
            const TerrainVertexBounds span = fullSpan(maskSpansX[m][static_cast<size_t>(tile.x)], maskSpansZ[m][static_cast<size_t>(tile.z)]);
            if (!span.valid) continue;
            const u32 w = span.maxX - span.minX + 1;
            for (u32 z = span.minZ; z <= span.maxZ; ++z) {
                appendRaw(payload, mask.data.data() + static_cast<size_t>(z) * mask.resolutionX + span.minX, w);
            }
        }

        const u64 recordSize = payload.size() - recordStart;
        if (recordSize > std::numeric_limits<u32>::max()) return false;
        directory[index].offset = recordStart;
        directory[index].size = static_cast<u32>(recordSize);
        directory[index].crc = IO::crc32(payload.data() + recordStart, recordSize);
        while (payload.size() % kRecordAlign != 0) payload.push_back(0);
    }
    local.tiles = grid.tileCount();

    FileMeta meta{};
    meta.formatVersion = kFormatVersion;
    meta.resolutionX = resX;
    meta.resolutionZ = resZ;
    meta.tileCells = kTileCells;
    meta.tilesX = grid.tilesX();
    meta.tilesZ = grid.tilesZ();
    meta.tileCount = grid.tileCount();
    meta.flags = hasSplat ? kFlagSplat : 0u;
    meta.splatResolutionX = hasSplat ? splatmap.resolutionX() : 0;
    meta.splatResolutionZ = hasSplat ? splatmap.resolutionZ() : 0;
    meta.maskCount = static_cast<u32>(storedMasks.size());
    meta.maxHeight = dimensions.maxHeight;
    meta.sizeX = dimensions.sizeX;
    meta.sizeZ = dimensions.sizeZ;

    std::vector<MaskEntry> maskEntries(storedMasks.size());
    for (size_t m = 0; m < storedMasks.size(); ++m) {
        std::memset(&maskEntries[m], 0, sizeof(MaskEntry));
        std::memcpy(maskEntries[m].name, storedMasks[m].name.data(), storedMasks[m].name.size());
        maskEntries[m].resolutionX = storedMasks[m].resolutionX;
        maskEntries[m].resolutionZ = storedMasks[m].resolutionZ;
    }

    u32 crc = IO::crc32(&meta, sizeof(meta));  // directoryCrc is still zero
    if (!maskEntries.empty()) crc = IO::crc32Continue(crc, maskEntries.data(), maskEntries.size() * sizeof(MaskEntry));
    crc = IO::crc32Continue(crc, directory.data(), directory.size() * sizeof(TileEntry));
    meta.directoryCrc = crc;

    std::vector<u8> metadata;
    appendRaw(metadata, &meta, 1);
    appendRaw(metadata, maskEntries.data(), maskEntries.size());
    appendRaw(metadata, directory.data(), directory.size());

    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    const IO::CafWriter::WriteResult written =
        IO::CafWriter::write(temporary.string().c_str(), AssetType::Terrain, CAF_FLAG_NONE, metadata.data(),
                             metadata.size(), payload.data(), payload.size());
    if (!written.success) {
        std::filesystem::remove(temporary, ec);
        return false;
    }
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::filesystem::remove(temporary, ec);
        return false;
    }
    local.fileBytes = written.bytesWritten;
    if (stats) *stats = local;
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Reader
// ---------------------------------------------------------------------------------------------------------------------

bool TerrainContainer::fail(const std::string& message) {
    m_error = message;
    return false;
}

bool TerrainContainer::open(const std::filesystem::path& path) {
    m_open = false;
    m_error.clear();
    m_info = {};
    m_directory.clear();
    if (m_in.is_open()) m_in.close();
    m_in.clear();
    m_in.open(path, std::ios::binary | std::ios::ate);
    if (!m_in.is_open()) return fail("cannot open " + path.string());
    m_fileSize = static_cast<u64>(m_in.tellg());
    m_in.seekg(0, std::ios::beg);

    CafHeader header{};
    if (m_fileSize < sizeof(CafHeader) + sizeof(FileMeta) + CafHeader::kFooterSize) return fail("file too small");
    m_in.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!m_in || header.magic != CafHeader::kMagic) return fail("not a caf file");
    if (header.type != AssetType::Terrain) return fail("not a terrain asset");
    if (header.versionMajor != CafHeader::kVersionMajor) return fail("unsupported caf version");
    if (header.metadataSize < sizeof(FileMeta) || header.metadataSize > m_fileSize ||
        header.dataSize > m_fileSize || header.totalFileSize() != m_fileSize) {
        return fail("sizes in the caf header do not match the file");
    }

    FileMeta meta{};
    m_in.read(reinterpret_cast<char*>(&meta), sizeof(meta));
    if (!m_in) return fail("cannot read the terrain header");
    if (meta.formatVersion < 1 || meta.formatVersion > kFormatVersion) return fail("unsupported terrain version");
    if (meta.tileCells != kTileCells) return fail("tile size differs from this build");
    if (!sizesAreSane(meta.resolutionX, meta.resolutionZ)) return fail("bad resolution");
    if (meta.maskCount > TerrainMasks::kMaxStoredMasks) return fail("too many masks");
    if (!(meta.sizeX > 0.0) || !(meta.sizeZ > 0.0)) return fail("bad terrain size");

    const TileGrid grid(meta.resolutionX, meta.resolutionZ, meta.sizeX, meta.sizeZ);
    if (meta.tilesX != grid.tilesX() || meta.tilesZ != grid.tilesZ() || meta.tileCount != grid.tileCount()) {
        return fail("tile grid does not match the resolution");
    }
    const u64 expectedMetadata = sizeof(FileMeta) + static_cast<u64>(meta.maskCount) * sizeof(MaskEntry) +
                                 static_cast<u64>(meta.tileCount) * sizeof(TileEntry);
    if (header.metadataSize != expectedMetadata) return fail("metadata size does not match the directories");

    std::vector<MaskEntry> maskEntries(meta.maskCount);
    if (meta.maskCount > 0) {
        m_in.read(reinterpret_cast<char*>(maskEntries.data()),
                  static_cast<std::streamsize>(maskEntries.size() * sizeof(MaskEntry)));
    }
    std::vector<TileEntry> entries(meta.tileCount);
    m_in.read(reinterpret_cast<char*>(entries.data()), static_cast<std::streamsize>(entries.size() * sizeof(TileEntry)));
    if (!m_in) return fail("cannot read the directories");

    FileMeta zeroed = meta;
    zeroed.directoryCrc = 0;
    u32 crc = IO::crc32(&zeroed, sizeof(zeroed));
    if (!maskEntries.empty()) crc = IO::crc32Continue(crc, maskEntries.data(), maskEntries.size() * sizeof(MaskEntry));
    crc = IO::crc32Continue(crc, entries.data(), entries.size() * sizeof(TileEntry));
    if (crc != meta.directoryCrc) return fail("directory CRC mismatch");

    m_info.formatVersion = meta.formatVersion;
    m_info.resolutionX = meta.resolutionX;
    m_info.resolutionZ = meta.resolutionZ;
    m_info.dimensions = {meta.sizeX, meta.sizeZ, meta.maxHeight};
    m_info.tilesX = meta.tilesX;
    m_info.tilesZ = meta.tilesZ;
    m_info.hasSplat = (meta.flags & kFlagSplat) != 0;
    m_info.splatResolutionX = m_info.hasSplat ? meta.splatResolutionX : 0;
    m_info.splatResolutionZ = m_info.hasSplat ? meta.splatResolutionZ : 0;
    if (m_info.hasSplat && !sizesAreSane(meta.splatResolutionX, meta.splatResolutionZ)) return fail("bad splat resolution");
    for (const MaskEntry& entry : maskEntries) {
        const size_t length = strnlen(entry.name, sizeof(entry.name));
        if (length == 0 || length >= sizeof(entry.name) || !sizesAreSane(entry.resolutionX, entry.resolutionZ)) {
            return fail("bad mask entry");
        }
        m_info.masks.push_back({std::string(entry.name, length), entry.resolutionX, entry.resolutionZ});
    }

    m_directory.resize(entries.size());
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].offset > header.dataSize || entries[i].size > header.dataSize - entries[i].offset ||
            entries[i].size < sizeof(TileRecordHeader)) {
            return fail("tile directory points outside the payload");
        }
        m_directory[i] = {entries[i].offset, entries[i].size, entries[i].crc};
    }

    m_grid = grid;
    m_payloadStart = sizeof(CafHeader) + header.metadataSize;
    m_heightSpansX = TileAxis::spans(meta.resolutionX, meta.resolutionX, grid.tilesX());
    m_heightSpansZ = TileAxis::spans(meta.resolutionZ, meta.resolutionZ, grid.tilesZ());
    m_splatSpansX.clear();
    m_splatSpansZ.clear();
    if (m_info.hasSplat) {
        m_splatSpansX = TileAxis::spans(meta.resolutionX, meta.splatResolutionX, grid.tilesX());
        m_splatSpansZ = TileAxis::spans(meta.resolutionZ, meta.splatResolutionZ, grid.tilesZ());
    }
    m_maskSpansX.clear();
    m_maskSpansZ.clear();
    for (const TerrainMaskInfo& mask : m_info.masks) {
        m_maskSpansX.push_back(TileAxis::spans(meta.resolutionX, mask.resolutionX, grid.tilesX()));
        m_maskSpansZ.push_back(TileAxis::spans(meta.resolutionZ, mask.resolutionZ, grid.tilesZ()));
    }
    m_open = true;
    return true;
}

bool TerrainContainer::loadTile(TileCoord tile, TerrainTileData& out) {
    if (!m_open) return fail("no file open");
    if (tile.x < 0 || tile.z < 0 || static_cast<u32>(tile.x) >= m_info.tilesX ||
        static_cast<u32>(tile.z) >= m_info.tilesZ) {
        return fail("tile outside the grid");
    }
    const u32 index = m_grid.index(tile);
    const DirectoryEntry& entry = m_directory[index];

    std::vector<u8> record(entry.size);
    m_in.clear();
    m_in.seekg(static_cast<std::streamoff>(m_payloadStart + entry.offset), std::ios::beg);
    m_in.read(reinterpret_cast<char*>(record.data()), static_cast<std::streamsize>(record.size()));
    if (!m_in) return fail("cannot read tile record");
    if (IO::crc32(record.data(), record.size()) != entry.crc) return fail("tile CRC mismatch");

    TileRecordHeader header{};
    std::memcpy(&header, record.data(), sizeof(header));
    if (header.magic != kTileMagic || header.tileX != tile.x || header.tileZ != tile.z) {
        return fail("tile record does not match its directory entry");
    }

    out = TerrainTileData{};
    out.coord = tile;
    out.owned = m_grid.owned(tile);
    const u32 width = out.owned.maxX - out.owned.minX + 1;
    const u32 rows = out.owned.maxZ - out.owned.minZ + 1;
    const size_t heightCount = static_cast<size_t>(width) * rows;
    if (header.heightCount != heightCount) return fail("tile height count mismatch");

    size_t cursor = sizeof(TileRecordHeader);
    const auto take = [&](size_t bytes) -> const u8* {
        if (bytes > record.size() - cursor) return nullptr;
        const u8* at = record.data() + cursor;
        cursor += bytes;
        return at;
    };

    out.heights.resize(heightCount);
    if (header.heightForm == kHeightsWide) {
        const u8* src = take(heightCount * sizeof(f32));
        if (!src) return fail("tile record truncated");
        std::memcpy(out.heights.data(), src, heightCount * sizeof(f32));
        out.heightsWide = true;
    } else if (header.heightForm == kHeightsQuantized) {
        const u8* src = take(heightCount * sizeof(u16));
        if (!src) return fail("tile record truncated");
        QuantizedTile quantized;
        quantized.baseCm = header.baseCm;
        quantized.offsets.resize(heightCount);
        std::memcpy(quantized.offsets.data(), src, heightCount * sizeof(u16));
        dequantizeHeights(quantized, m_info.dimensions.maxHeight, out.heights.data());
    } else {
        return fail("unknown height form");
    }

    if (m_info.hasSplat) {
        const TerrainVertexBounds span = fullSpan(m_splatSpansX[static_cast<size_t>(tile.x)], m_splatSpansZ[static_cast<size_t>(tile.z)]);
        const u32 w = span.valid ? span.maxX - span.minX + 1 : 0;
        const u32 h = span.valid ? span.maxZ - span.minZ + 1 : 0;
        if (header.splatCount != static_cast<u64>(w) * h) return fail("tile splat count mismatch");
        out.splatX0 = span.minX;
        out.splatZ0 = span.minZ;
        out.splatW = w;
        out.splatH = h;
        if (header.splatCount > 0) {
            out.splat.resize(header.splatCount);
            if (header.splatForm == kSplatRgba8) {
                const u8* src = take(static_cast<size_t>(header.splatCount) * 4);
                if (!src) return fail("tile record truncated");
                for (size_t i = 0; i < out.splat.size(); ++i) {
                    out.splat[i] = Vec4(decodeSplatByte(src[i * 4 + 0]), decodeSplatByte(src[i * 4 + 1]),
                                        decodeSplatByte(src[i * 4 + 2]), decodeSplatByte(src[i * 4 + 3]));
                }
            } else if (header.splatForm == kSplatRgba32f) {
                const u8* src = take(static_cast<size_t>(header.splatCount) * sizeof(Vec4));
                if (!src) return fail("tile record truncated");
                std::memcpy(out.splat.data(), src, out.splat.size() * sizeof(Vec4));
                out.splatWide = true;
            } else {
                return fail("unknown splat form");
            }
        }
    } else if (header.splatCount != 0 || header.splatForm != kSplatNone) {
        return fail("tile has splat data but the file has none");
    }

    out.masks.resize(m_info.masks.size());
    for (size_t m = 0; m < m_info.masks.size(); ++m) {
        const TerrainVertexBounds span = fullSpan(m_maskSpansX[m][static_cast<size_t>(tile.x)], m_maskSpansZ[m][static_cast<size_t>(tile.z)]);
        if (!span.valid) continue;
        const u32 w = span.maxX - span.minX + 1;
        const u32 h = span.maxZ - span.minZ + 1;
        const u8* src = take(static_cast<size_t>(w) * h);
        if (!src) return fail("tile record truncated");
        out.masks[m].x0 = span.minX;
        out.masks[m].z0 = span.minZ;
        out.masks[m].w = w;
        out.masks[m].h = h;
        out.masks[m].data.assign(src, src + static_cast<size_t>(w) * h);
    }
    if (cursor != record.size()) return fail("tile record has trailing bytes");
    return true;
}

bool TerrainContainer::sizeDestinations(TerrainHeightmap& heightmap, TerrainSplatmap& splatmap) const {
    if (heightmap.resolutionX() != m_info.resolutionX || heightmap.resolutionZ() != m_info.resolutionZ) {
        heightmap.resize(m_info.resolutionX, m_info.resolutionZ);
    }
    const u32 splatX = m_info.hasSplat ? m_info.splatResolutionX : m_info.resolutionX;
    const u32 splatZ = m_info.hasSplat ? m_info.splatResolutionZ : m_info.resolutionZ;
    if (splatmap.resolutionX() != splatX || splatmap.resolutionZ() != splatZ) {
        splatmap.resize(splatX, splatZ);
        if (!m_info.hasSplat) splatmap.fillLayer(0, 1.0f);
    }
    return true;
}

void TerrainContainer::writeTile(const TerrainTileData& tile, const TerrainFileInfo& info, TerrainHeightmap& heightmap,
                                 TerrainSplatmap& splatmap) {
    const u32 width = tile.owned.maxX - tile.owned.minX + 1;
    const u32 rows = tile.owned.maxZ - tile.owned.minZ + 1;
    for (u32 z = 0; z < rows; ++z) {
        f32* dst = heightmap.heights().data() + static_cast<size_t>(tile.owned.minZ + z) * info.resolutionX + tile.owned.minX;
        std::copy(tile.heights.begin() + static_cast<size_t>(z) * width,
                  tile.heights.begin() + static_cast<size_t>(z + 1) * width, dst);
    }
    if (info.hasSplat && tile.splatW > 0) {
        for (u32 z = 0; z < tile.splatH; ++z) {
            Vec4* dst = splatmap.weights().data() + static_cast<size_t>(tile.splatZ0 + z) * info.splatResolutionX + tile.splatX0;
            std::copy(tile.splat.begin() + static_cast<size_t>(z) * tile.splatW,
                      tile.splat.begin() + static_cast<size_t>(z + 1) * tile.splatW, dst);
        }
    }
}

bool TerrainContainer::loadTilesInto(const std::vector<TileCoord>& tiles, TerrainHeightmap& heightmap,
                                     TerrainSplatmap& splatmap) {
    if (!m_open) return fail("no file open");
    sizeDestinations(heightmap, splatmap);
    TerrainTileData data;
    for (const TileCoord& tile : tiles) {
        if (!loadTile(tile, data)) return false;
        writeTile(data, m_info, heightmap, splatmap);
    }
    return true;
}

bool TerrainContainer::load(const std::filesystem::path& path, TerrainHeightmap& heightmap, TerrainSplatmap& splatmap,
                            bool* hadSplat, TerrainMasks* masks, TerrainDimensions* dimensions) {
    if (hadSplat) *hadSplat = false;
    if (masks) masks->clear();

    TerrainContainer file;
    if (!file.open(path)) return false;
    const TerrainFileInfo& info = file.info();

    // Decode into locals so a corrupted tile leaves the caller's terrain untouched.
    TerrainHeightmap newHeights;
    TerrainSplatmap newSplat;
    file.sizeDestinations(newHeights, newSplat);
    std::vector<std::vector<u8>> maskData(info.masks.size());
    if (masks) {
        for (size_t m = 0; m < info.masks.size(); ++m) {
            maskData[m].resize(static_cast<size_t>(info.masks[m].resolutionX) * info.masks[m].resolutionZ);
        }
    }

    TerrainTileData tile;
    for (u32 index = 0; index < info.tileCount(); ++index) {
        if (!file.loadTile(file.grid().coordOf(index), tile)) return false;
        writeTile(tile, info, newHeights, newSplat);
        if (!masks) continue;
        for (size_t m = 0; m < tile.masks.size(); ++m) {
            const TerrainTileData::Mask& part = tile.masks[m];
            for (u32 z = 0; z < part.h; ++z) {
                std::copy(part.data.begin() + static_cast<size_t>(z) * part.w,
                          part.data.begin() + static_cast<size_t>(z + 1) * part.w,
                          maskData[m].begin() + static_cast<size_t>(part.z0 + z) * info.masks[m].resolutionX + part.x0);
            }
        }
    }

    if (masks) {
        for (size_t m = 0; m < info.masks.size(); ++m) {
            if (!masks->set(info.masks[m].name, info.masks[m].resolutionX, info.masks[m].resolutionZ, maskData[m].data())) {
                masks->clear();
                return false;
            }
        }
    }
    heightmap = std::move(newHeights);
    splatmap = std::move(newSplat);
    if (hadSplat) *hadSplat = info.hasSplat;
    if (dimensions) *dimensions = info.dimensions;
    return true;
}

bool TerrainContainer::verifyWholeFile() {
    if (!m_open) return fail("no file open");
    m_in.clear();
    m_in.seekg(0, std::ios::beg);

    CafHeader header{};
    m_in.read(reinterpret_cast<char*>(&header), sizeof(header));
    const u64 body = m_fileSize - CafHeader::kFooterSize;
    const u64 payloadBegin = m_payloadStart;
    const u64 payloadEnd = m_payloadStart + header.dataSize;

    u32 whole = IO::crc32(&header, sizeof(header));
    u32 payloadCrc = IO::crc32(nullptr, 0);
    std::vector<u8> buffer(1u << 20);
    u64 position = sizeof(header);
    while (position < body) {
        const u64 want = std::min<u64>(buffer.size(), body - position);
        m_in.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(want));
        if (static_cast<u64>(m_in.gcount()) != want) return fail("file shorter than its header says");
        whole = IO::crc32Continue(whole, buffer.data(), want);
        const u64 from = std::max(position, payloadBegin);
        const u64 to = std::min(position + want, payloadEnd);
        if (from < to) payloadCrc = IO::crc32Continue(payloadCrc, buffer.data() + (from - position), to - from);
        position += want;
    }
    u32 footer = 0;
    m_in.read(reinterpret_cast<char*>(&footer), sizeof(footer));
    if (!m_in) return fail("cannot read the footer");
    if (footer != whole) return fail("whole-file CRC mismatch");
    if (payloadCrc != header.crc32) return fail("payload CRC mismatch");
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Comparison, conversion and the engine's entry point
// ---------------------------------------------------------------------------------------------------------------------

bool terrainsIdentical(const TerrainHeightmap& a, const TerrainHeightmap& b, const TerrainSplatmap& splatA,
                       const TerrainSplatmap& splatB, const TerrainMasks& masksA, const TerrainMasks& masksB) {
    if (a.resolutionX() != b.resolutionX() || a.resolutionZ() != b.resolutionZ() ||
        a.heights().size() != b.heights().size() ||
        std::memcmp(a.heights().data(), b.heights().data(), a.heights().size() * sizeof(f32)) != 0) {
        return false;
    }
    if (splatA.resolutionX() != splatB.resolutionX() || splatA.resolutionZ() != splatB.resolutionZ() ||
        splatA.weights().size() != splatB.weights().size() ||
        std::memcmp(splatA.weights().data(), splatB.weights().data(), splatA.weights().size() * sizeof(Vec4)) != 0) {
        return false;
    }
    const auto& ma = masksA.stored();
    const auto& mb = masksB.stored();
    if (ma.size() != mb.size()) return false;
    for (size_t i = 0; i < ma.size(); ++i) {
        if (ma[i].name != mb[i].name || ma[i].resolutionX != mb[i].resolutionX ||
            ma[i].resolutionZ != mb[i].resolutionZ || ma[i].data != mb[i].data) {
            return false;
        }
    }
    return true;
}

bool convertLegacyTerrain(const std::filesystem::path& legacyPath, const std::filesystem::path& nativePath,
                          const TerrainDimensions& dimensions, TerrainConvertReport* report, bool verify) {
    TerrainConvertReport local;
    const auto finish = [&](bool ok, const std::string& error) {
        local.error = error;
        if (report) *report = local;
        return ok;
    };

    if (detectTerrainFile(legacyPath) != TerrainFileFormat::Legacy) return finish(false, "not a legacy .cterrain file");

    TerrainHeightmap heights;
    TerrainSplatmap splat;
    TerrainMasks masks;
    bool hadSplat = false;
    if (!TerrainSerializer::load(legacyPath, heights, splat, &hadSplat, &masks)) {
        return finish(false, "cannot read the legacy terrain");
    }
    local.hadSplat = hadSplat;
    local.maskCount = static_cast<u32>(masks.stored().size());

    if (!TerrainContainer::save(nativePath, heights, splat, hadSplat, &masks, dimensions, {}, &local.stats)) {
        return finish(false, "cannot write " + nativePath.string());
    }

    if (verify) {
        TerrainHeightmap backHeights;
        TerrainSplatmap backSplat;
        TerrainMasks backMasks;
        bool backHadSplat = false;
        const bool same = TerrainContainer::load(nativePath, backHeights, backSplat, &backHadSplat, &backMasks) &&
                          backHadSplat == hadSplat &&
                          terrainsIdentical(heights, backHeights, splat, backSplat, masks, backMasks);
        if (!same) {
            std::error_code ec;
            std::filesystem::remove(nativePath, ec);
            return finish(false, "round-trip check failed: the converted file differs from the legacy one");
        }
        local.verified = true;
    }
    return finish(true, {});
}

bool loadTerrainAny(const std::filesystem::path& path, TerrainHeightmap& heightmap, TerrainSplatmap& splatmap,
                    bool* hadSplat, TerrainMasks* masks, bool* fromLegacy) {
    if (fromLegacy) *fromLegacy = false;
    switch (detectTerrainFile(path)) {
        case TerrainFileFormat::Native:
            return TerrainContainer::load(path, heightmap, splatmap, hadSplat, masks);
        case TerrainFileFormat::Legacy:
            if (!TerrainSerializer::load(path, heightmap, splatmap, hadSplat, masks)) return false;
            if (fromLegacy) *fromLegacy = true;
            return true;
        case TerrainFileFormat::Unknown:
            break;
    }
    return false;
}

}  // namespace Caffeine::Terrain
