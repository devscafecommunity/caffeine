#include "catch.hpp"
#include "ecs/World.hpp"
#include "terrain/TerrainCache.hpp"
#include "terrain/TerrainContainer.hpp"
#include "terrain/TerrainSerializer.hpp"
#include "terrain/edit/TerrainStroke.hpp"
#include "terrain/edit/TerrainTiles.hpp"

#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <set>

using namespace Caffeine;
using namespace Caffeine::Terrain;

namespace {

ECS::TerrainComponent settingsFor(u32 resolution, f32 sizeMetres, f32 maxHeight = 80.0f) {
    ECS::TerrainComponent t;
    t.resolutionX = resolution;
    t.resolutionZ = resolution;
    t.worldSizeX = sizeMetres;
    t.worldSizeZ = sizeMetres;
    t.maxHeight = maxHeight;
    return t;
}

TerrainHeightmap flat(u32 resolution, f32 level = 0.5f) {
    TerrainHeightmap map(resolution, resolution);
    map.fill(level);
    return map;
}

f32 at(const TerrainHeightmap& map, u32 x, u32 z) { return map.heights()[static_cast<size_t>(z) * map.resolutionX() + x]; }

}  // namespace

TEST_CASE("Terrain tiles - the tiles own every vertex exactly once", "[terrain][edit]") {
    for (const u32 res : {65u, 129u, 257u, 300u, 1025u}) {
        const TileGrid grid(res, res, 512.0, 512.0);
        std::vector<u8> owner(static_cast<size_t>(res) * res, 0);
        for (u32 t = 0; t < grid.tileCount(); ++t) {
            const TerrainVertexBounds b = grid.owned(grid.coordOf(t));
            for (u32 z = b.minZ; z <= b.maxZ; ++z) {
                for (u32 x = b.minX; x <= b.maxX; ++x) ++owner[static_cast<size_t>(z) * res + x];
            }
        }
        INFO("resolution " << res);
        for (const u8 count : owner) REQUIRE(count == 1);
    }
    REQUIRE(TileGrid(257, 257, 512, 512).tilesX() == 4);
    REQUIRE(TileGrid(4097, 4097, 8192, 8192).tilesX() == 64);
    REQUIRE(TileGrid(65, 65, 64, 64).tilesX() == 1);
}

TEST_CASE("Terrain tiles - a mesh region reaches one vertex into the neighbours", "[terrain][edit]") {
    const TileGrid grid(257, 257, 512.0, 512.0);
    const TerrainVertexBounds middle = grid.meshRegion(TileCoord{1, 1});
    REQUIRE(middle.minX == 63);
    REQUIRE(middle.maxX == 128);
    const TerrainVertexBounds corner = grid.meshRegion(TileCoord{0, 0});
    REQUIRE(corner.minX == 0);
    REQUIRE(corner.maxX == 64);
    const TerrainVertexBounds last = grid.meshRegion(TileCoord{3, 3});
    REQUIRE(last.maxX == 256);
}

TEST_CASE("Terrain tiles - positions are named in integer centimetres, whatever the sampling", "[terrain][edit]") {
    // A kilometre-scale terrain: 8.192 km on 4097 vertices is a vertex every 2 m.
    const TileGrid coarse(4097, 4097, 8192.0, 8192.0);
    REQUIRE(coarse.vertexXCm(0) == 0);
    REQUIRE(coarse.vertexXCm(1) == 200);
    REQUIRE(coarse.vertexXCm(4096) == 819200);
    // The same point, 1 km along, on a terrain sampled twice as finely: the same centimetres.
    const TileGrid fine(8193, 8193, 8192.0, 8192.0);
    REQUIRE(fine.vertexXCm(1000) == coarse.vertexXCm(500));
    REQUIRE(coarse.vertexXCm(500) == 100000);
    // A point in centimetres finds its tile at 8 km as well as at 0.
    REQUIRE(coarse.tileAtCm(0, 0) == TileCoord{0, 0});
    REQUIRE(coarse.tileAtCm(819200, 819200) == TileCoord{63, 63});
    REQUIRE(coarse.tileAtCm(12800, 25600) == TileCoord{1, 2});  // 128 m, 256 m: vertex 64, 128
}

TEST_CASE("Terrain tiles - dirty tiles come from the changed vertices and nothing else", "[terrain][edit]") {
    const TileGrid grid(257, 257, 512.0, 512.0);
    DirtyTiles dirty(grid);
    REQUIRE_FALSE(dirty.any());

    TerrainVertexBounds inside;
    inside.minX = inside.maxX = 100;
    inside.minZ = inside.maxZ = 100;
    inside.valid = true;
    dirty.mark(inside);
    REQUIRE(dirty.count() == 1);
    REQUIRE(dirty.isDirty(TileCoord{1, 1}));

    // A vertex on a tile's edge is also in the next tile's triangles.
    TerrainVertexBounds edge;
    edge.minX = edge.maxX = 128;
    edge.minZ = edge.maxZ = 100;
    edge.valid = true;
    dirty.mark(edge);
    REQUIRE(dirty.isDirty(TileCoord{1, 1}));
    REQUIRE(dirty.isDirty(TileCoord{2, 1}));
    REQUIRE(dirty.count() == 2);

    const TerrainVertexBounds region = dirty.region();
    REQUIRE(region.valid);
    REQUIRE(region.minX == 63);
    REQUIRE(region.maxX == 192);
    dirty.clear();
    REQUIRE_FALSE(dirty.any());
    REQUIRE_FALSE(dirty.region().valid);
}

TEST_CASE("Terrain tiles - heights quantise to a centimetre, or stay exact when a tile is too tall", "[terrain][edit]") {
    const f32 maxHeight = 200.0f;
    std::vector<f32> heights(64 * 64);
    for (size_t i = 0; i < heights.size(); ++i) heights[i] = (50.0f + static_cast<f32>(i % 997) * 0.0137f) / maxHeight;
    const QuantizedTile tile = quantizeHeights(heights.data(), heights.size(), maxHeight);
    REQUIRE_FALSE(tile.wide);
    REQUIRE(tile.offsets.size() == heights.size());
    std::vector<f32> back(heights.size());
    dequantizeHeights(tile, maxHeight, back.data());
    for (size_t i = 0; i < heights.size(); ++i) {
        REQUIRE(std::fabs(back[i] - heights[i]) * maxHeight <= 0.0051f);  // half a centimetre
    }
    // A height that is a whole number of centimetres comes back exactly.
    const std::vector<f32> whole = {12.34f / maxHeight, 12.35f / maxHeight, 99.99f / maxHeight};
    std::vector<f32> wholeBack(3);
    dequantizeHeights(quantizeHeights(whole.data(), 3, maxHeight), maxHeight, wholeBack.data());
    for (int i = 0; i < 3; ++i) REQUIRE(std::fabs(wholeBack[i] - whole[i]) * maxHeight < 0.0001f);

    // 700 m of relief in one tile does not fit 16 bits of centimetres: it is kept as floats, unchanged.
    const std::vector<f32> tall = {0.0f, 0.5f, 0.9f};
    const QuantizedTile wide = quantizeHeights(tall.data(), 3, 1000.0f);
    REQUIRE(wide.wide);
    std::vector<f32> wideBack(3);
    dequantizeHeights(wide, 1000.0f, wideBack.data());
    REQUIRE(wideBack == tall);
}

TEST_CASE("Terrain stroke - stamps are spaced in metres along the path", "[terrain][edit]") {
    TerrainHeightmap map = flat(257);
    StrokeBrush brush;
    brush.radiusM = 4.0f;
    brush.spacingM = 2.0f;
    TerrainStroke stroke(map, settingsFor(257, 512.0f), brush);
    u32 laid = stroke.addPoint({-50.0, 0.0, 1.0f});
    REQUIRE(laid == 1);  // the first point stamps at once
    laid = stroke.addPoint({50.0, 0.0, 1.0f});
    REQUIRE(laid == 50);  // 100 m / 2 m
    REQUIRE(stroke.stamps() == 51);
    // A zero-length move lays nothing.
    REQUIRE(stroke.addPoint({50.0, 0.0, 1.0f}) == 0);
}

TEST_CASE("Terrain stroke - the same path gives the same terrain however often it is sampled", "[terrain][edit]") {
    auto run = [](int samples) {
        TerrainHeightmap map = flat(257);
        StrokeBrush brush;
        brush.radiusM = 6.0f;
        brush.strength = 0.3f;
        TerrainStroke stroke(map, settingsFor(257, 512.0f), brush);
        for (int i = 0; i <= samples; ++i) {
            const f64 t = static_cast<f64>(i) / samples;
            stroke.addPoint({-80.0 + 160.0 * t, 20.0 - 40.0 * t, 1.0f});
        }
        return map.heights();
    };
    const auto few = run(2);
    for (const int samples : {7, 60, 240}) {
        const auto many = run(samples);
        REQUIRE(many.size() == few.size());
        f32 worst = 0.0f;
        for (size_t i = 0; i < few.size(); ++i) worst = std::max(worst, std::fabs(many[i] - few[i]));
        INFO(samples << " samples");
        REQUIRE(worst < 1e-5f);
    }
}

TEST_CASE("Terrain stroke - strength is metres of height per stamp", "[terrain][edit]") {
    TerrainHeightmap map = flat(257, 0.5f);
    StrokeBrush brush;
    brush.mode = TerrainBrushMode::Raise;
    brush.radiusM = 10.0f;
    brush.strength = 2.0f;  // metres
    TerrainStroke stroke(map, settingsFor(257, 512.0f, 80.0f), brush);
    stroke.addPoint({0.0, 0.0, 1.0f});  // the centre of the terrain: vertex 128, 128
    REQUIRE(at(map, 128, 128) * 80.0f == Approx(40.0f + 2.0f).margin(0.01f));
    // Outside the radius nothing moved.
    REQUIRE(at(map, 128 + 10, 128) == 0.5f);

    TerrainHeightmap lowered = flat(257, 0.5f);
    brush.mode = TerrainBrushMode::Lower;
    TerrainStroke down(lowered, settingsFor(257, 512.0f, 80.0f), brush);
    down.addPoint({0.0, 0.0, 1.0f});
    REQUIRE(at(lowered, 128, 128) * 80.0f == Approx(40.0f - 2.0f).margin(0.01f));
}

TEST_CASE("Terrain stroke - a stamp image shapes the brush and rotates with it", "[terrain][edit]") {
    auto raised = [](f32 rotation) {
        TerrainHeightmap map = flat(257, 0.5f);
        StrokeBrush brush;
        brush.radiusM = 12.0f;
        brush.strength = 4.0f;
        brush.falloff = FalloffCurve::Constant;
        brush.rotationDegrees = rotation;
        brush.stamp.size = 8;
        brush.stamp.alpha.assign(64, 0.0f);
        for (u32 z = 0; z < 8; ++z) {
            for (u32 x = 4; x < 8; ++x) brush.stamp.alpha[z * 8 + x] = 1.0f;  // the right half
        }
        TerrainStroke stroke(map, settingsFor(257, 512.0f), brush);
        stroke.addPoint({0.0, 0.0, 1.0f});
        return std::make_pair(at(map, 128 + 3, 128) > 0.5f, at(map, 128 - 3, 128) > 0.5f);  // 3 vertices of 2 m = 6 m
    };
    const auto upright = raised(0.0f);
    REQUIRE(upright.first);
    REQUIRE_FALSE(upright.second);
    const auto turned = raised(180.0f);
    REQUIRE_FALSE(turned.first);
    REQUIRE(turned.second);
}

TEST_CASE("Terrain stroke - pressure scales the strength only when the brush asks for it", "[terrain][edit]") {
    auto rise = [](f32 pressure, f32 follow) {
        TerrainHeightmap map = flat(257, 0.5f);
        StrokeBrush brush;
        brush.radiusM = 8.0f;
        brush.strength = 2.0f;
        brush.pressureToStrength = follow;
        TerrainStroke stroke(map, settingsFor(257, 512.0f), brush);
        stroke.addPoint({0.0, 0.0, pressure});
        return (at(map, 128, 128) - 0.5f) * 80.0f;
    };
    REQUIRE(rise(0.5f, 0.0f) == Approx(2.0f).margin(0.01f));  // a mouse
    REQUIRE(rise(0.5f, 1.0f) == Approx(1.0f).margin(0.01f));  // a pen at half pressure
    REQUIRE(rise(1.0f, 1.0f) == Approx(2.0f).margin(0.01f));
}

TEST_CASE("Terrain stroke - smoothing does not depend on the order of the vertices", "[terrain][edit]") {
    TerrainHeightmap map = flat(257, 0.4f);
    map.heights()[128 * 257 + 128] = 0.9f;  // a spike
    StrokeBrush brush;
    brush.mode = TerrainBrushMode::Smooth;
    brush.radiusM = 10.0f;
    brush.strength = 1.0f;
    TerrainStroke stroke(map, settingsFor(257, 512.0f), brush);
    stroke.addPoint({0.0, 0.0, 1.0f});
    // The spike is symmetric, so the result is symmetric in x and z (an in-place pass would lean one way).
    for (u32 d = 1; d <= 4; ++d) {
        REQUIRE(at(map, 128 - d, 128) == Approx(at(map, 128 + d, 128)).margin(1e-6f));
        REQUIRE(at(map, 128, 128 - d) == Approx(at(map, 128, 128 + d)).margin(1e-6f));
        REQUIRE(at(map, 128 - d, 128) == Approx(at(map, 128, 128 + d)).margin(1e-6f));
    }
    REQUIRE(at(map, 128, 128) < 0.9f);
}

TEST_CASE("Terrain stroke - flatten goes to a height in metres or to the height under the brush", "[terrain][edit]") {
    TerrainHeightmap map = flat(257, 0.5f);
    for (u32 x = 100; x < 160; ++x) map.heights()[128 * 257 + x] = 0.7f;
    StrokeBrush brush;
    brush.mode = TerrainBrushMode::Flatten;
    brush.radiusM = 8.0f;
    brush.strength = 1.0f;
    brush.falloff = FalloffCurve::Constant;
    brush.targetHeightM = 30.0f;
    TerrainStroke stroke(map, settingsFor(257, 512.0f, 80.0f), brush);
    stroke.addPoint({0.0, 0.0, 1.0f});
    REQUIRE(at(map, 128, 128) * 80.0f == Approx(30.0f).margin(0.01f));

    TerrainHeightmap under = flat(257, 0.5f);
    under.heights()[128 * 257 + 128] = 0.6f;
    brush.targetHeightM = -1.0f;
    TerrainStroke fromUnder(under, settingsFor(257, 512.0f, 80.0f), brush);
    fromUnder.addPoint({0.0, 0.0, 1.0f});
    REQUIRE(at(under, 128 + 2, 128) == Approx(under.sampleBilinear(0.5f, 0.5f)).margin(0.02f));
    REQUIRE(at(under, 128 + 2, 128) > 0.55f);  // taken toward what was under the brush
}

TEST_CASE("Terrain stroke - noise is deterministic for a seed", "[terrain][edit]") {
    auto run = [](u64 seed) {
        TerrainHeightmap map = flat(257, 0.5f);
        StrokeBrush brush;
        brush.mode = TerrainBrushMode::Noise;
        brush.radiusM = 10.0f;
        brush.strength = 3.0f;
        brush.seed = seed;
        TerrainStroke stroke(map, settingsFor(257, 512.0f), brush);
        stroke.addPoint({-5.0, 0.0, 1.0f});
        stroke.addPoint({5.0, 0.0, 1.0f});
        return map.heights();
    };
    REQUIRE(run(1) == run(1));
    REQUIRE_FALSE(run(1) == run(2));
}

TEST_CASE("Terrain stroke - a record undoes and redoes exactly the tiles it changed", "[terrain][edit]") {
    TerrainHeightmap map = flat(257, 0.5f);
    for (size_t i = 0; i < map.heights().size(); ++i) map.heights()[i] = 0.3f + static_cast<f32>((i * 2654435761u) % 1000) * 0.0003f;
    const std::vector<f32> original = map.heights();

    StrokeBrush brush;
    brush.radiusM = 14.0f;
    brush.strength = 3.0f;
    TerrainStroke stroke(map, settingsFor(257, 512.0f), brush);
    stroke.addPoint({-30.0, -20.0, 1.0f});
    stroke.addPoint({40.0, 25.0, 1.0f});
    const std::vector<f32> edited = map.heights();
    StrokeRecord record = stroke.finish();
    REQUIRE_FALSE(record.empty());
    REQUIRE(edited != original);
    // Only a handful of the 16 tiles were touched, and the record is smaller than the terrain.
    REQUIRE(record.tiles.size() < 16);
    REQUIRE(record.bytes() < original.size() * sizeof(f32));

    const TerrainVertexBounds region = record.applyBefore(map);
    REQUIRE(region.valid);
    REQUIRE(map.heights() == original);  // bit for bit
    record.applyAfter(map);
    REQUIRE(map.heights() == edited);
    record.applyBefore(map);
    REQUIRE(map.heights() == original);
}

TEST_CASE("Terrain stroke - a stroke that changes nothing leaves an empty record", "[terrain][edit]") {
    TerrainHeightmap map = flat(257, 1.0f);  // already at the ceiling
    StrokeBrush brush;
    brush.mode = TerrainBrushMode::Raise;
    brush.radiusM = 10.0f;
    TerrainStroke stroke(map, settingsFor(257, 512.0f), brush);
    stroke.addPoint({0.0, 0.0, 1.0f});
    stroke.addPoint({30.0, 0.0, 1.0f});
    REQUIRE(stroke.finish().empty());
}

TEST_CASE("Terrain stroke - the dirty tiles are the ones the stroke reached, and clear after a rebuild", "[terrain][edit]") {
    TerrainHeightmap map = flat(257, 0.5f);
    StrokeBrush brush;
    brush.radiusM = 5.0f;
    TerrainStroke stroke(map, settingsFor(257, 512.0f), brush);
    stroke.addPoint({-200.0, -200.0, 1.0f});  // near the (0, 0) corner tile
    REQUIRE(stroke.dirty().any());
    REQUIRE(stroke.dirty().count() <= 4);
    REQUIRE(stroke.dirty().isDirty(TileCoord{0, 0}));
    REQUIRE_FALSE(stroke.dirty().isDirty(TileCoord{3, 3}));
    stroke.clearDirty();
    REQUIRE_FALSE(stroke.dirty().any());
    stroke.addPoint({-200.0, -190.0, 1.0f});
    REQUIRE(stroke.dirty().any());
}

TEST_CASE("Terrain stroke - an 8 km terrain is edited by tile, far from the origin, at centimetre precision", "[terrain][edit]") {
    const u32 res = 4097;
    TerrainHeightmap map = flat(res, 0.5f);
    const ECS::TerrainComponent settings = settingsFor(res, 8192.0f, 400.0f);
    StrokeBrush brush;
    brush.radiusM = 10.0f;
    brush.strength = 0.5f;  // metres
    const auto started = std::chrono::steady_clock::now();
    TerrainStroke stroke(map, settings, brush);
    // 3.9 km from the centre, near the far corner.
    stroke.addPoint({3900.0, 3900.0, 1.0f});
    const f64 centreCell = (3900.0 + 4096.0) / 2.0;  // vertex index: 2 m cells
    const u32 cx = static_cast<u32>(std::llround(centreCell));
    REQUIRE((at(map, cx, cx) - 0.5f) * 400.0f == Approx(0.5f).margin(0.01f));
    REQUIRE(at(map, 10, 10) == 0.5f);
    REQUIRE(stroke.dirty().count() <= 4);  // a few tiles of 4096
    const StrokeRecord record = stroke.finish();
    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    REQUIRE(record.tiles.size() <= 4);
    REQUIRE(record.bytes() < 200 * 1024);
    REQUIRE(elapsed < 250.0);  // an edit is local: it must not scale with the 16.8 million vertices
}

constexpr u64 GOLDEN_RAISE = 16251785435909529862ull;
constexpr u64 GOLDEN_LOWER = 2283927637860168511ull;
constexpr u64 GOLDEN_SMOOTH = 9170514029711157337ull;
constexpr u64 GOLDEN_FLATTEN = 17217090276318299020ull;
constexpr u64 GOLDEN_NOISE = 11904519181310769692ull;

namespace {

/// FNV-1a over the bits of the heights: the fingerprint a golden test pins.
u64 fingerprint(const std::vector<f32>& heights) {
    u64 h = 1469598103934665603ull;
    for (const f32 v : heights) {
        u32 bits;
        static_assert(sizeof(bits) == sizeof(v));
        std::memcpy(&bits, &v, sizeof(bits));
        for (int i = 0; i < 4; ++i) {
            h ^= (bits >> (8 * i)) & 0xFFu;
            h *= 1099511628211ull;
        }
    }
    return h;
}

}  // namespace

TEST_CASE("Terrain stroke - golden: a canonical stroke gives one pinned terrain", "[terrain][edit][golden]") {
    // One stroke per mode on the same flat terrain. The numbers below are the terrain these strokes made when the
    // stroke engine was written; they change only when the stroke algorithm changes, which must then be a decision
    // (the same path has to keep giving the same terrain on every machine, and old undo records have to keep fitting).
    auto run = [](TerrainBrushMode mode) {
        TerrainHeightmap map = flat(129, 0.5f);
        for (u32 z = 0; z < 129; ++z) {
            for (u32 x = 0; x < 129; ++x) map.heights()[z * 129 + x] = 0.40f + static_cast<f32>((x * 7 + z * 13) % 17) * 0.01f;  // arithmetic only: the same on every platform
        }
        StrokeBrush brush;
        brush.mode = mode;
        brush.radiusM = 7.0f;
        brush.strength = mode == TerrainBrushMode::Smooth || mode == TerrainBrushMode::Flatten ? 0.6f : 1.5f;
        brush.seed = 12345;
        brush.targetHeightM = 36.0f;
        TerrainStroke stroke(map, settingsFor(129, 128.0f, 80.0f), brush);
        stroke.addPoint({-20.0, -10.0, 1.0f});
        stroke.addPoint({0.0, 5.0, 1.0f});
        stroke.addPoint({25.0, -8.0, 1.0f});
        return fingerprint(map.heights());
    };
    // The same stroke twice is the same terrain (the pin below would mean nothing otherwise).
    for (const auto mode : {TerrainBrushMode::Raise, TerrainBrushMode::Lower, TerrainBrushMode::Smooth,
                            TerrainBrushMode::Flatten, TerrainBrushMode::Noise}) {
        REQUIRE(run(mode) == run(mode));
    }
    REQUIRE(run(TerrainBrushMode::Raise) != run(TerrainBrushMode::Lower));
    REQUIRE(run(TerrainBrushMode::Raise) == GOLDEN_RAISE);
    REQUIRE(run(TerrainBrushMode::Lower) == GOLDEN_LOWER);
    REQUIRE(run(TerrainBrushMode::Smooth) == GOLDEN_SMOOTH);
    REQUIRE(run(TerrainBrushMode::Flatten) == GOLDEN_FLATTEN);
    REQUIRE(run(TerrainBrushMode::Noise) == GOLDEN_NOISE);
}

TEST_CASE("Terrain stroke - a stamp has a time budget, whatever the terrain size", "[terrain][edit][budget]") {
    // A brush of 50 m on a 2 m grid covers about 2000 vertices; a stamp must cost far less than a frame. The budget is
    // generous (it has to hold on a slow CI machine); the point is that it does not grow with the terrain.
    for (const u32 res : {257u, 1025u, 4097u}) {
        TerrainHeightmap map = flat(res, 0.5f);
        StrokeBrush brush;
        brush.radiusM = 50.0f;
        brush.strength = 0.2f;
        brush.mode = TerrainBrushMode::Smooth;  // the dearest mode: it reads the neighbours
        TerrainStroke stroke(map, settingsFor(res, static_cast<f32>(res - 1) * 2.0f, 200.0f), brush);
        const auto start = std::chrono::steady_clock::now();
        const int stamps = 40;
        for (int i = 0; i < stamps; ++i) stroke.addPoint({static_cast<f64>(i) * 12.5, 0.0, 1.0f});
        const double perStamp =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / stamps;
        INFO("resolution " << res << ": " << perStamp << " ms per stamp");
        REQUIRE(perStamp < 4.0);
    }
}

TEST_CASE("Terrain tiles - the changed region is the tight rectangle, not the tiles' mesh regions", "[terrain][edit]") {
    const TileGrid grid(257, 257, 512.0, 512.0);
    DirtyTiles dirty(grid);
    TerrainVertexBounds a;
    a.minX = 127; a.maxX = 139; a.minZ = 127; a.maxZ = 139; a.valid = true;
    dirty.mark(a);
    TerrainVertexBounds b;
    b.minX = 130; b.maxX = 150; b.minZ = 120; b.maxZ = 131; b.valid = true;
    dirty.mark(b);
    const TerrainVertexBounds changed = dirty.changed();
    REQUIRE(changed.valid);
    REQUIRE(changed.minX == 127);
    REQUIRE(changed.maxX == 150);
    REQUIRE(changed.minZ == 120);
    REQUIRE(changed.maxZ == 139);
    // The mesh region of those tiles is much wider than what changed.
    REQUIRE(dirty.region().maxX - dirty.region().minX > changed.maxX - changed.minX);
    dirty.clear();
    REQUIRE_FALSE(dirty.changed().valid);

    // A stroke's record says the same: the vertices that differ.
    TerrainHeightmap map = flat(257, 0.5f);
    StrokeBrush brush;
    brush.radiusM = 12.0f;
    brush.strength = 2.0f;
    TerrainStroke stroke(map, settingsFor(257, 512.0f), brush);
    stroke.addPoint({10.0, 10.0, 1.0f});
    const TerrainVertexBounds seen = stroke.dirty().changed();
    const StrokeRecord record = stroke.finish();
    REQUIRE(record.region.valid);
    REQUIRE(record.region.minX >= seen.minX);
    REQUIRE(record.region.maxX <= seen.maxX);
    REQUIRE(record.region.maxX - record.region.minX < 14);  // 12 m on 2 m cells
}

TEST_CASE("Terrain GI tiles - the step keeps a terrain within the triangle budget and a metre of detail", "[terrain][edit][gi]") {
    REQUIRE(TerrainCache::giStepFor(257, 257, 512.0, 512.0) == 1);       // 2 m cells, 131k triangles
    REQUIRE(TerrainCache::giStepFor(513, 513, 128.0, 128.0) == 4);       // 0.25 m cells: a metre is all the volume resolves
    const u32 step = TerrainCache::giStepFor(4097, 4097, 8192.0, 8192.0);
    REQUIRE(step >= 8);                                                  // 33 million cells do not fit as they are
    const f64 triangles = 2.0 * 4096.0 * 4096.0 / (static_cast<f64>(step) * step);
    REQUIRE(triangles <= 400000.0);
}

TEST_CASE("Terrain GI tiles - only the tiles an edit touched are meshed again", "[terrain][edit][gi]") {
    ECS::World world;
    ECS::Entity land = world.create();
    auto& terrain = world.add<ECS::TerrainComponent>(land);
    terrain.resolutionX = 257;
    terrain.resolutionZ = 257;
    terrain.worldSizeX = 512.0f;
    terrain.worldSizeZ = 512.0f;
    terrain.maxHeight = 80.0f;
    terrain.splatResolutionScale = 1;
    terrain.useChunks = false;
    auto& cache = TerrainCache::instance();
    cache.initializeEntity(world, land);
    cache.heightmapFor(land)->fill(0.25f);
    ECS::TerrainComponent& settings = *world.get<ECS::TerrainComponent>(land);
    cache.rebuildRegion(land, settings, 0, 0, 256, 256);

    std::vector<u64> before;
    for (const TerrainGiTile& tile : cache.giTilesFor(land, settings)) {
        REQUIRE_FALSE(tile.mesh.vertices.empty());
        REQUIRE(tile.revision != 0);
        before.push_back(tile.revision);
    }
    REQUIRE(before.size() == 16);
    // Asking again changes nothing.
    {
        const auto& again = cache.giTilesFor(land, settings);
        for (size_t i = 0; i < again.size(); ++i) REQUIRE(again[i].revision == before[i]);
    }

    // An edit inside one tile moves that tile alone, and its mesh follows the new heights.
    for (u32 z = 100; z <= 105; ++z) {
        for (u32 x = 100; x <= 105; ++x) cache.heightmapFor(land)->heights()[z * 257 + x] = 0.6f;
    }
    TerrainVertexBounds inside;
    inside.minX = inside.minZ = 100;
    inside.maxX = inside.maxZ = 105;
    inside.valid = true;
    cache.commitHeightEdit(land, settings, inside, false);
    {
        const auto& tiles = cache.giTilesFor(land, settings);
        int changed = 0;
        for (size_t i = 0; i < tiles.size(); ++i) changed += tiles[i].revision != before[i] ? 1 : 0;
        REQUIRE(changed == 1);
        REQUIRE(tiles[1 * 4 + 1].revision != before[1 * 4 + 1]);  // tile (1, 1): vertices 64..127
        REQUIRE(tiles[1 * 4 + 1].hi.y > 40.0f);                   // the raised patch is in its mesh (0.6 * 80 = 48)
        REQUIRE(tiles[0].hi.y == Approx(20.0f).margin(0.01f));    // the others are untouched
    }

    // An edit on a tile's border reaches the neighbour's triangles too.
    std::vector<u64> mid;
    for (const TerrainGiTile& tile : cache.giTilesFor(land, settings)) mid.push_back(tile.revision);
    TerrainVertexBounds border;
    border.minX = border.maxX = 128;
    border.minZ = border.maxZ = 100;
    border.valid = true;
    cache.heightmapFor(land)->heights()[100 * 257 + 128] = 0.7f;
    cache.commitHeightEdit(land, settings, border, false);
    int moved = 0;
    const auto& tiles = cache.giTilesFor(land, settings);
    for (size_t i = 0; i < tiles.size(); ++i) moved += tiles[i].revision != mid[i] ? 1 : 0;
    REQUIRE(moved == 2);  // (1, 1) and (2, 1)
    cache.removeEntity(land);
}

TEST_CASE("Terrain data - an old project opens as it is, converts on request and keeps its legacy file", "[terrain][edit][legacy]") {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "caffeine-terrain-legacy-open";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "terrain", ec);

    // An old project: heights that are not whole centimetres, and a legacy `.cterrain` that holds them.
    TerrainHeightmap heights(129, 129);
    for (u32 z = 0; z < 129; ++z) {
        for (u32 x = 0; x < 129; ++x) heights.heights()[z * 129 + x] = 0.3f + static_cast<f32>((x * 31 + z * 17) % 101) * 0.0031337f;
    }
    TerrainSplatmap splat(129, 129);
    splat.fillLayer(0, 1.0f);
    TerrainMasks masks;
    const fs::path legacy = root / "terrain" / "ground.cterrain";
    REQUIRE(TerrainSerializer::save(legacy, heights, splat, true, &masks));

    ECS::World world;
    ECS::Entity land = world.create();
    auto& terrain = world.add<ECS::TerrainComponent>(land);
    terrain.worldSizeX = 256.0f;
    terrain.worldSizeZ = 256.0f;
    terrain.maxHeight = 60.0f;
    terrain.useChunks = false;
    std::strncpy(terrain.terrainDataPath, "terrain/ground.cterrain", sizeof(terrain.terrainDataPath) - 1);

    auto& cache = TerrainCache::instance();
    REQUIRE(cache.loadTerrainFile(world, land, *world.get<ECS::TerrainComponent>(land), legacy));
    REQUIRE(cache.dataFileIsLegacy(land));
    REQUIRE(cache.heightmapFor(land)->heights() == heights.heights());  // opened exactly as it was

    ECS::TerrainComponent& settings = *world.get<ECS::TerrainComponent>(land);
    REQUIRE(cache.convertToNative(land, settings, root));
    REQUIRE(std::string(settings.terrainDataPath) == "terrain/ground.terrain");
    REQUIRE(fs::exists(root / "terrain" / "ground.terrain"));
    REQUIRE(fs::exists(legacy));  // the old file stays
    REQUIRE_FALSE(cache.dataFileIsLegacy(land));

    // The native file holds the same terrain, bit for bit, and opens as native.
    TerrainHeightmap back;
    TerrainSplatmap backSplat;
    bool fromLegacy = true;
    REQUIRE(loadTerrainAny(root / "terrain" / "ground.terrain", back, backSplat, nullptr, nullptr, &fromLegacy));
    REQUIRE_FALSE(fromLegacy);
    REQUIRE(back.heights() == heights.heights());

    // A terrain with nowhere to save, or no project folder, cannot be converted (and nothing is written).
    ECS::Entity bare = world.create();
    world.add<ECS::TerrainComponent>(bare);
    cache.initializeEntity(world, bare);
    REQUIRE_FALSE(cache.convertToNative(bare, *world.get<ECS::TerrainComponent>(bare), root));
    REQUIRE_FALSE(cache.convertToNative(land, settings, fs::path()));
    cache.removeEntity(land);
    cache.removeEntity(bare);
    fs::remove_all(root, ec);
}
