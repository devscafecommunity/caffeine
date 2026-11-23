#include "catch.hpp"
#include "core/io/CafTypes.hpp"
#include "ecs/TerrainComponents.hpp"
#include "ecs/World.hpp"
#include "terrain/TerrainCache.hpp"
#include "terrain/TerrainContainer.hpp"
#include "terrain/TerrainSerializer.hpp"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>
#include <set>

using namespace Caffeine;
using namespace Caffeine::Terrain;

namespace {

struct TempDir {
    std::filesystem::path path;
    explicit TempDir(const std::string& name) {
        path = std::filesystem::temp_directory_path() / ("caffeine_terrain_container_" + name);
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

constexpr f32 kMaxHeight = 80.0f;

/// A height an exact number of centimetres: what a 16-bit tile reproduces bit for bit.
f32 exactCm(i64 centimetres, f32 maxHeight = kMaxHeight) {
    const f64 scale = 1.0 / (100.0 * static_cast<f64>(maxHeight));
    return static_cast<f32>(static_cast<f64>(centimetres) * scale);
}

/// Heights where the tiles of `wideTiles` hold arbitrary floats and every other tile is centimetre-exact.
TerrainHeightmap makeHeights(u32 res, const std::set<std::pair<i32, i32>>& wideTiles, u32 seed) {
    TerrainHeightmap map(res, res);
    const TileGrid grid(res, res, 512.0, 512.0);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<f32> any(0.0f, 1.0f);
    for (u32 z = 0; z < res; ++z) {
        for (u32 x = 0; x < res; ++x) {
            const TileCoord tile = grid.tileOfVertex(x, z);
            f32 value;
            if (wideTiles.count({tile.x, tile.z})) {
                value = any(rng);
            } else {
                value = exactCm(static_cast<i64>((x * 7u + z * 13u + static_cast<u32>(tile.x) * 101u) % 6000u));
            }
            map.heights()[static_cast<size_t>(z) * res + x] = value;
        }
    }
    return map;
}

TerrainSplatmap makeSplat(u32 res, bool exactBytes, u32 seed) {
    TerrainSplatmap splat(res, res);
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> byte(0, 255);
    std::uniform_real_distribution<f32> any(0.0f, 1.0f);
    for (auto& w : splat.weights()) {
        if (exactBytes) {
            w = Vec4(static_cast<f32>(byte(rng)) / 255.0f, static_cast<f32>(byte(rng)) / 255.0f,
                     static_cast<f32>(byte(rng)) / 255.0f, static_cast<f32>(byte(rng)) / 255.0f);
        } else {
            w = Vec4(any(rng), any(rng), any(rng), any(rng));
        }
    }
    return splat;
}

std::vector<u8> makeMaskBytes(u32 resX, u32 resZ, u32 seed) {
    std::vector<u8> data(static_cast<size_t>(resX) * resZ);
    std::mt19937 rng(seed);
    for (u8& v : data) v = static_cast<u8>(rng() & 0xFF);
    return data;
}

bool sameBits(const std::vector<f32>& a, const std::vector<f32>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(f32)) == 0;
}

bool sameBits(const std::vector<Vec4>& a, const std::vector<Vec4>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(Vec4)) == 0;
}

bool sameMasks(const TerrainMasks& a, const TerrainMasks& b) {
    if (a.stored().size() != b.stored().size()) return false;
    for (size_t i = 0; i < a.stored().size(); ++i) {
        const auto& x = a.stored()[i];
        const auto& y = b.stored()[i];
        if (x.name != y.name || x.resolutionX != y.resolutionX || x.resolutionZ != y.resolutionZ || x.data != y.data) {
            return false;
        }
    }
    return true;
}

TerrainDimensions dims() { return TerrainDimensions{512.0, 512.0, kMaxHeight}; }

}  // namespace

TEST_CASE("Terrain container - splat and mask texels are partitioned between the tiles", "[terrain][container]") {
    for (const u32 heightRes : {65u, 257u, 300u, 1025u}) {
        const TileGrid grid(heightRes, heightRes, 512.0, 512.0);
        for (const u32 res : {2u, 3u, 33u, heightRes, heightRes * 2 - 1, 700u}) {
            const auto spans = TileAxis::spans(heightRes, res, grid.tilesX());
            u32 covered = 0;
            for (const auto& span : spans) {
                CHECK(span.begin <= span.end);
                if (span.size() > 0) {
                    CHECK(span.begin == covered);  // contiguous, no gap, no overlap
                    covered = span.end;
                }
            }
            CHECK(covered == res);
        }
        // On the height grid itself the assignment is the tile's owned vertices.
        const auto own = TileAxis::spans(heightRes, heightRes, grid.tilesX());
        for (u32 t = 0; t < grid.tilesX(); ++t) {
            const TerrainVertexBounds b = grid.owned(TileCoord{static_cast<i32>(t), 0});
            CHECK(own[t].begin == b.minX);
            CHECK(own[t].end == b.maxX + 1);
        }
    }
}

TEST_CASE("Terrain container - a legacy terrain converts bit for bit", "[terrain][container]") {
    TempDir dir("legacy");
    const u32 res = 257;
    TerrainHeightmap heights = makeHeights(res, {{1, 1}, {3, 0}}, 11);
    // Values the 16-bit form can never reproduce: every tile holds arbitrary floats in a few vertices.
    heights.heights()[5] = 0.123456789f;
    heights.heights()[res * 200 + 17] = 0.987654321f;
    TerrainSplatmap splat = makeSplat(res * 2 - 1, false, 5);  // splat grid of its own, floats that are not bytes
    TerrainMasks masks;
    const auto maskA = makeMaskBytes(res, res, 1);
    const auto maskB = makeMaskBytes(129, 97, 2);  // a grid that is not the heights'
    REQUIRE(masks.set("wear", res, res, maskA.data()));
    REQUIRE(masks.set("moisture", 129, 97, maskB.data()));

    const auto legacy = dir.path / "world.cterrain";
    REQUIRE(TerrainSerializer::save(legacy, heights, splat, true, &masks));

    TerrainConvertReport report;
    const auto native = nativeTerrainPath(legacy);
    REQUIRE(native.extension() == ".terrain");
    REQUIRE(convertLegacyTerrain(legacy, native, dims(), &report));
    CHECK(report.verified);
    CHECK(report.hadSplat);
    CHECK(report.maskCount == 2);
    CHECK(report.stats.tiles == 16);
    CHECK(report.stats.wideSplatTiles > 0);  // float weights cannot become bytes without a change
    CHECK(detectTerrainFile(legacy) == TerrainFileFormat::Legacy);
    CHECK(detectTerrainFile(native) == TerrainFileFormat::Native);

    TerrainHeightmap backHeights;
    TerrainSplatmap backSplat;
    TerrainMasks backMasks;
    bool hadSplat = false;
    TerrainDimensions backDims;
    REQUIRE(TerrainContainer::load(native, backHeights, backSplat, &hadSplat, &backMasks, &backDims));
    CHECK(hadSplat);
    CHECK(backDims.sizeX == 512.0);
    CHECK(backDims.maxHeight == kMaxHeight);
    CHECK(backHeights.resolutionX() == res);
    CHECK(sameBits(backHeights.heights(), heights.heights()));
    CHECK(backSplat.resolutionX() == splat.resolutionX());
    CHECK(sameBits(backSplat.weights(), splat.weights()));
    CHECK(sameMasks(backMasks, masks));

    // The engine's entry point reads either format to the same maps.
    for (const auto& file : {legacy, native}) {
        TerrainHeightmap h;
        TerrainSplatmap s;
        TerrainMasks m;
        bool fromLegacy = false;
        REQUIRE(loadTerrainAny(file, h, s, &hadSplat, &m, &fromLegacy));
        CHECK(fromLegacy == (file == legacy));
        CHECK(terrainsIdentical(h, heights, s, splat, m, masks));
    }

    TerrainContainer reader;
    REQUIRE(reader.open(native));
    CHECK(reader.verifyWholeFile());
}

TEST_CASE("Terrain container - a legacy terrain without splat or masks keeps meaning", "[terrain][container]") {
    TempDir dir("nosplat");
    const u32 res = 129;
    TerrainHeightmap heights = makeHeights(res, {}, 3);
    TerrainSplatmap none;
    const auto legacy = dir.path / "plain.cterrain";
    REQUIRE(TerrainSerializer::save(legacy, heights, none, false));

    TerrainConvertReport report;
    const auto native = dir.path / "plain.terrain";
    REQUIRE(convertLegacyTerrain(legacy, native, dims(), &report));
    CHECK_FALSE(report.hadSplat);
    CHECK(report.stats.wideHeightTiles == 0);  // centimetre-exact heights stay 16-bit

    TerrainHeightmap h;
    TerrainSplatmap s;
    bool hadSplat = true;
    REQUIRE(TerrainContainer::load(native, h, s, &hadSplat, nullptr));
    CHECK_FALSE(hadSplat);
    CHECK(sameBits(h.heights(), heights.heights()));
    CHECK(s.resolutionX() == res);
    CHECK(s.sample(10, 10).x == 1.0f);  // like the legacy reader: layer 0
}

TEST_CASE("Terrain container - only the asked tiles are loaded", "[terrain][container]") {
    TempDir dir("partial");
    const u32 res = 257;
    const TerrainHeightmap heights = makeHeights(res, {{2, 2}}, 21);
    const TerrainSplatmap splat = makeSplat(res, true, 22);
    TerrainMasks masks;
    const auto maskData = makeMaskBytes(res, res, 23);
    REQUIRE(masks.set("m", res, res, maskData.data()));
    const auto path = dir.path / "t.terrain";
    TerrainWriteStats stats;
    REQUIRE(TerrainContainer::save(path, heights, splat, true, &masks, dims(), {}, &stats));
    CHECK(stats.wideHeightTiles == 1);
    CHECK(stats.wideSplatTiles == 0);  // weights that are exact bytes are stored as bytes

    TerrainContainer file;
    REQUIRE(file.open(path));
    CHECK(file.info().tilesX == 4);
    CHECK(file.info().tilesZ == 4);

    // One tile, decoded: equal to the same vertices of the source, with the right storage form.
    for (const TileCoord tile : {TileCoord{0, 0}, TileCoord{2, 2}, TileCoord{3, 3}}) {
        TerrainTileData data;
        REQUIRE(file.loadTile(tile, data));
        CHECK(data.heightsWide == (tile.x == 2 && tile.z == 2));
        CHECK_FALSE(data.splatWide);
        const u32 width = data.owned.maxX - data.owned.minX + 1;
        for (u32 z = data.owned.minZ; z <= data.owned.maxZ; ++z) {
            for (u32 x = data.owned.minX; x <= data.owned.maxX; ++x) {
                const f32 got = data.heights[static_cast<size_t>(z - data.owned.minZ) * width + (x - data.owned.minX)];
                const f32 want = heights.heights()[static_cast<size_t>(z) * res + x];
                REQUIRE(std::memcmp(&got, &want, sizeof(f32)) == 0);
            }
        }
        REQUIRE(data.masks.size() == 1);
        CHECK(data.masks[0].w == width);
        CHECK(data.splatW == width);
    }
    TerrainTileData outside;
    CHECK_FALSE(file.loadTile(TileCoord{4, 0}, outside));
    CHECK_FALSE(file.loadTile(TileCoord{-1, 0}, outside));

    // Loading two tiles into empty maps touches those tiles' vertices and nothing else.
    TerrainHeightmap partial;
    TerrainSplatmap partialSplat;
    REQUIRE(file.loadTilesInto({TileCoord{1, 0}, TileCoord{2, 2}}, partial, partialSplat));
    const TileGrid grid(res, res, 512.0, 512.0);
    u32 matched = 0;
    for (u32 z = 0; z < res; ++z) {
        for (u32 x = 0; x < res; ++x) {
            const TileCoord t = grid.tileOfVertex(x, z);
            const size_t i = static_cast<size_t>(z) * res + x;
            if ((t.x == 1 && t.z == 0) || (t.x == 2 && t.z == 2)) {
                REQUIRE(std::memcmp(&partial.heights()[i], &heights.heights()[i], sizeof(f32)) == 0);
                REQUIRE(std::memcmp(&partialSplat.weights()[i], &splat.weights()[i], sizeof(Vec4)) == 0);
                ++matched;
            } else {
                REQUIRE(partial.heights()[i] == 0.0f);
            }
        }
    }
    CHECK(matched == 2u * 64u * 64u);  // tiles (1, 0) and (2, 2) own 64 x 64 vertices each
}

TEST_CASE("Terrain container - a corrupted tile is detected by its CRC", "[terrain][container]") {
    TempDir dir("crc");
    const u32 res = 129;
    const TerrainHeightmap heights = makeHeights(res, {}, 31);
    const TerrainSplatmap splat = makeSplat(res, true, 32);
    const auto path = dir.path / "t.terrain";
    REQUIRE(TerrainContainer::save(path, heights, splat, true, nullptr, dims()));

    {
        TerrainContainer good;
        REQUIRE(good.open(path));
        CHECK(good.verifyWholeFile());
    }

    CafHeader header{};
    {
        std::ifstream in(path, std::ios::binary);
        in.read(reinterpret_cast<char*>(&header), sizeof(header));
    }
    const std::streamoff payloadStart = static_cast<std::streamoff>(sizeof(CafHeader) + header.metadataSize);

    const auto flip = [&](const std::filesystem::path& file, std::streamoff at) {
        std::fstream io(file, std::ios::binary | std::ios::in | std::ios::out);
        char byte = 0;
        io.seekg(at);
        io.read(&byte, 1);
        byte = static_cast<char>(byte ^ 0x40);
        io.seekp(at);
        io.write(&byte, 1);
    };

    SECTION("a flipped byte inside the first tile") {
        flip(path, payloadStart + 40);  // inside the heights of tile (0, 0)
        TerrainContainer file;
        REQUIRE(file.open(path));  // the directory is intact
        TerrainTileData data;
        CHECK_FALSE(file.loadTile(TileCoord{0, 0}, data));
        CHECK(file.error().find("CRC") != std::string::npos);
        CHECK(file.loadTile(TileCoord{1, 1}, data));  // the other tiles still read
        CHECK_FALSE(file.verifyWholeFile());

        TerrainHeightmap h(4, 4);
        h.fill(0.25f);
        TerrainSplatmap s;
        CHECK_FALSE(TerrainContainer::load(path, h, s));
        CHECK(h.resolutionX() == 4);  // the caller's terrain is untouched by a failed load
        CHECK(h.heights()[0] == 0.25f);
    }
    SECTION("a flipped byte in the last tile") {
        const std::streamoff last = static_cast<std::streamoff>(std::filesystem::file_size(path)) - 4 - 32;
        flip(path, last);
        TerrainContainer file;
        REQUIRE(file.open(path));
        TerrainTileData data;
        CHECK_FALSE(file.loadTile(TileCoord{1, 1}, data));
        CHECK(file.loadTile(TileCoord{0, 0}, data));
    }
    SECTION("a flipped byte in the directory") {
        flip(path, static_cast<std::streamoff>(sizeof(CafHeader) + header.metadataSize) - 20);
        TerrainContainer file;
        CHECK_FALSE(file.open(path));
        CHECK(file.error().find("directory") != std::string::npos);
    }
    SECTION("a file that is not a terrain") {
        const auto other = dir.path / "other.terrain";
        std::ofstream(other, std::ios::binary) << "this is not a terrain container at all, not even close";
        TerrainContainer file;
        CHECK_FALSE(file.open(other));
        CHECK(detectTerrainFile(other) == TerrainFileFormat::Unknown);
    }
    SECTION("a truncated file") {
        std::filesystem::resize_file(path, std::filesystem::file_size(path) - 100);
        TerrainContainer file;
        CHECK_FALSE(file.open(path));
    }
}

TEST_CASE("Terrain container - a tile that needs the wide form is exact", "[terrain][container]") {
    TempDir dir("wide");
    const u32 res = 129;
    TerrainHeightmap heights = makeHeights(res, {}, 41);
    // Tile (0, 0): arbitrary floats. Tile (1, 0): centimetre-exact but rising 800 m over a 1000 m terrain, more than
    // the 655 m a 16-bit tile spans. Tile (0, 1): exact, special values included.
    std::mt19937 rng(42);
    std::uniform_real_distribution<f32> any(0.0f, 1.0f);
    const TileGrid grid(res, res, 512.0, 512.0);
    for (u32 z = 0; z < res; ++z) {
        for (u32 x = 0; x < res; ++x) {
            const TileCoord t = grid.tileOfVertex(x, z);
            f32& h = heights.heights()[static_cast<size_t>(z) * res + x];
            if (t.x == 0 && t.z == 0) h = any(rng);
            if (t.x == 1 && t.z == 0) h = (x % 2 == 0) ? exactCm(0, 1000.0f) : exactCm(80000, 1000.0f);
        }
    }
    heights.heights()[static_cast<size_t>(70) * res + 3] = -0.0f;
    heights.heights()[static_cast<size_t>(80) * res + 9] = std::numeric_limits<f32>::quiet_NaN();
    heights.heights()[static_cast<size_t>(90) * res + 11] = std::numeric_limits<f32>::infinity();

    const TerrainDimensions tall{512.0, 512.0, 1000.0f};
    const auto path = dir.path / "t.terrain";
    TerrainWriteStats stats;
    REQUIRE(TerrainContainer::save(path, heights, TerrainSplatmap(), false, nullptr, tall, {}, &stats));
    CHECK(stats.tiles == 4);
    // (0,0) arbitrary, (1,0) too tall; (0,1) holds NaN/inf/-0 and falls back; (1,1) is exact in 16 bit... except that
    // it is cm-exact only for the 80 m scale, not for 1000 m, so it is wide as well. What matters is exactness.
    CHECK(stats.wideHeightTiles >= 3);

    TerrainHeightmap back;
    TerrainSplatmap s;
    REQUIRE(TerrainContainer::load(path, back, s));
    CHECK(sameBits(back.heights(), heights.heights()));  // bit for bit, NaN payload and -0.0 included

    TerrainContainer file;
    REQUIRE(file.open(path));
    TerrainTileData data;
    REQUIRE(file.loadTile(TileCoord{1, 0}, data));
    CHECK(data.heightsWide);

    // And a terrain whose every tile is centimetre-exact is stored in 16 bit: the file is much smaller.
    const TerrainHeightmap exact = makeHeights(res, {}, 43);
    const auto small = dir.path / "small.terrain";
    TerrainWriteStats smallStats;
    REQUIRE(TerrainContainer::save(small, exact, TerrainSplatmap(), false, nullptr, dims(), {}, &smallStats));
    CHECK(smallStats.wideHeightTiles == 0);
    CHECK(smallStats.fileBytes < static_cast<u64>(res) * res * sizeof(f32) * 6 / 10);
    TerrainHeightmap exactBack;
    REQUIRE(TerrainContainer::load(small, exactBack, s));
    CHECK(sameBits(exactBack.heights(), exact.heights()));
}

TEST_CASE("Terrain container - splat weights are stored exactly or on request in bytes", "[terrain][container]") {
    TempDir dir("splat");
    const u32 res = 129;
    const TerrainHeightmap heights = makeHeights(res, {}, 51);
    const TerrainSplatmap floats = makeSplat(res, false, 52);

    TerrainWriteStats exactStats;
    const auto exactPath = dir.path / "exact.terrain";
    REQUIRE(TerrainContainer::save(exactPath, heights, floats, true, nullptr, dims(), {}, &exactStats));
    CHECK(exactStats.wideSplatTiles == exactStats.tiles);
    TerrainHeightmap h;
    TerrainSplatmap s;
    REQUIRE(TerrainContainer::load(exactPath, h, s));
    CHECK(sameBits(s.weights(), floats.weights()));

    // The compact save quantises on purpose: every weight comes back as the nearest byte / 255.
    TerrainWriteStats compactStats;
    const auto compactPath = dir.path / "compact.terrain";
    TerrainWriteOptions options;
    options.quantizeSplat = true;
    REQUIRE(TerrainContainer::save(compactPath, heights, floats, true, nullptr, dims(), options, &compactStats));
    CHECK(compactStats.wideSplatTiles == 0);
    CHECK(compactStats.fileBytes < exactStats.fileBytes);
    TerrainSplatmap compact;
    REQUIRE(TerrainContainer::load(compactPath, h, compact));
    for (size_t i = 0; i < compact.weights().size(); ++i) {
        REQUIRE(std::fabs(compact.weights()[i].x - floats.weights()[i].x) <= 0.5f / 255.0f + 1e-6f);
        REQUIRE(std::fabs(compact.weights()[i].w - floats.weights()[i].w) <= 0.5f / 255.0f + 1e-6f);
    }
    // Bytes saved once reload to the same bytes: a second save loses nothing more.
    TerrainWriteStats again;
    const auto againPath = dir.path / "again.terrain";
    REQUIRE(TerrainContainer::save(againPath, h, compact, true, nullptr, dims(), {}, &again));
    CHECK(again.wideSplatTiles == 0);
}

TEST_CASE("Terrain container - 4097 vertices through the legacy converter", "[terrain][container]") {
    TempDir dir("big");
    const u32 res = 4097;
    // A few arbitrary-float tiles among centimetre-exact ones, like a legacy terrain a sculptor touched.
    std::set<std::pair<i32, i32>> wide = {{0, 0}, {10, 20}, {63, 63}, {32, 5}};
    TerrainHeightmap heights = makeHeights(res, wide, 61);
    TerrainSplatmap splat = makeSplat(513, true, 62);
    TerrainMasks masks;
    const auto maskData = makeMaskBytes(1025, 1025, 63);
    REQUIRE(masks.set("flow", 1025, 1025, maskData.data()));

    const auto legacy = dir.path / "big.cterrain";
    REQUIRE(TerrainSerializer::save(legacy, heights, splat, true, &masks));

    TerrainConvertReport report;
    const auto native = dir.path / "big.terrain";
    REQUIRE(convertLegacyTerrain(legacy, native, TerrainDimensions{8192.0, 8192.0, kMaxHeight}, &report));
    CHECK(report.verified);
    CHECK(report.stats.tiles == 64u * 64u);
    CHECK(report.stats.wideHeightTiles == wide.size());
    CHECK(report.stats.wideSplatTiles == 0);

    TerrainContainer file;
    REQUIRE(file.open(native));
    CHECK(file.info().tilesX == 64);
    CHECK(file.info().tilesZ == 64);
    TerrainTileData data;
    REQUIRE(file.loadTile(TileCoord{63, 63}, data));  // the last tile owns the closing vertex
    CHECK(data.owned.maxX == res - 1);
    CHECK(data.owned.maxX - data.owned.minX + 1 == 65);
    CHECK(data.heightsWide);
    REQUIRE(file.loadTile(TileCoord{31, 31}, data));
    CHECK_FALSE(data.heightsWide);
    CHECK(file.verifyWholeFile());
}

TEST_CASE("Terrain container - a 257 terrain keeps the same grid of tiles as TileGrid", "[terrain][container]") {
    TempDir dir("grid");
    const u32 res = 257;
    const TerrainHeightmap heights = makeHeights(res, {}, 71);
    const auto path = dir.path / "t.terrain";
    REQUIRE(TerrainContainer::save(path, heights, TerrainSplatmap(), false, nullptr, TerrainDimensions{256.0, 256.0, kMaxHeight}));

    TerrainContainer file;
    REQUIRE(file.open(path));
    const TileGrid expected(res, res, 256.0, 256.0);
    CHECK(file.grid().tilesX() == expected.tilesX());
    CHECK(file.grid().tilesZ() == expected.tilesZ());
    CHECK(file.info().tileCount() == 16);
    CHECK(file.info().dimensions.sizeX == 256.0);

    // Every vertex is delivered by exactly one tile.
    std::vector<u8> seen(static_cast<size_t>(res) * res, 0);
    TerrainTileData data;
    for (u32 t = 0; t < expected.tileCount(); ++t) {
        REQUIRE(file.loadTile(expected.coordOf(t), data));
        const TerrainVertexBounds b = expected.owned(expected.coordOf(t));
        CHECK(data.owned.minX == b.minX);
        CHECK(data.owned.maxZ == b.maxZ);
        for (u32 z = b.minZ; z <= b.maxZ; ++z)
            for (u32 x = b.minX; x <= b.maxX; ++x) ++seen[static_cast<size_t>(z) * res + x];
    }
    for (const u8 count : seen) REQUIRE(count == 1);
}

TEST_CASE("Terrain container - the terrain cache opens an old project and saves the new format", "[terrain][container]") {
    TempDir dir("cache");
    ECS::World world;
    ECS::Entity entity = world.create();
    auto& terrain = world.add<ECS::TerrainComponent>(entity);
    terrain.resolutionX = 33;
    terrain.resolutionZ = 33;
    terrain.splatResolutionScale = 1;
    terrain.useChunks = false;
    terrain.worldSizeX = 128.0f;
    terrain.worldSizeZ = 128.0f;
    terrain.maxHeight = 40.0f;

    auto& cache = TerrainCache::instance();
    cache.initializeEntity(world, entity);
    TerrainHeightmap* heights = cache.heightmapFor(entity);
    REQUIRE(heights != nullptr);
    std::mt19937 rng(81);
    std::uniform_real_distribution<f32> any(0.0f, 1.0f);
    for (f32& h : heights->heights()) h = any(rng);
    const std::vector<f32> sculpted = heights->heights();

    // An old project: the terrain sits in a legacy file the scene points at.
    const auto legacy = dir.path / "old.cterrain";
    REQUIRE(cache.saveTerrainFile(entity, legacy, true));
    CHECK_FALSE(cache.saveTerrainFile(entity, dir.path / "refused.terrain", true));  // the native one needs the extent

    std::fill(heights->heights().begin(), heights->heights().end(), 0.0f);
    REQUIRE(cache.loadTerrainFile(world, entity, terrain, legacy));  // opens by reading the legacy format
    CHECK(sameBits(cache.heightmapFor(entity)->heights(), sculpted));

    // It offers the native path and saves there, with the component's extent and scale.
    const auto native = nativeTerrainPath(legacy);
    REQUIRE(cache.saveNativeTerrainFile(entity, terrain, native, true));
    std::fill(cache.heightmapFor(entity)->heights().begin(), cache.heightmapFor(entity)->heights().end(), 0.0f);
    REQUIRE(cache.loadTerrainFile(world, entity, terrain, native));
    CHECK(sameBits(cache.heightmapFor(entity)->heights(), sculpted));

    TerrainContainer file;
    REQUIRE(file.open(native));
    CHECK(file.info().dimensions.sizeX == 128.0);
    CHECK(file.info().dimensions.maxHeight == 40.0f);
    cache.removeEntity(entity);
}
