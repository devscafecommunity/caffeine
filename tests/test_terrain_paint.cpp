#include "catch.hpp"
#include "terrain/paint/TerrainAutoLayer.hpp"
#include "terrain/paint/TerrainLayerMap.hpp"
#include "terrain/paint/TerrainPaintStroke.hpp"
#include "terrain/paint/TerrainPalette.hpp"

#include <chrono>
#include <filesystem>
#include <cmath>

using namespace Caffeine;
using namespace Caffeine::Terrain;

namespace {

constexpr u32 kRes = 129;
constexpr f32 kSize = 256.0f;

ECS::TerrainComponent settings() {
    ECS::TerrainComponent t;
    t.resolutionX = kRes;
    t.resolutionZ = kRes;
    t.worldSizeX = kSize;
    t.worldSizeZ = kSize;
    t.maxHeight = 80.0f;
    return t;
}

TerrainHeightmap flatGround() {
    TerrainHeightmap map(kRes, kRes);
    map.fill(0.3f);
    return map;
}

/// Flat on the left, a 45 degree ramp on the right (x >= 64), like the brush tests.
TerrainHeightmap rampGround() {
    TerrainHeightmap map = flatGround();
    for (u32 z = 0; z < kRes; ++z) {
        for (u32 x = 64; x < kRes; ++x) map.heights()[static_cast<size_t>(z) * kRes + x] = 0.3f + static_cast<f32>(x - 64) * 2.0f / 80.0f;
    }
    return map;
}

u32 sum(const LayerTexel& t) { return static_cast<u32>(t.weight[0]) + t.weight[1] + t.weight[2] + t.weight[3]; }

bool everyTexelAddsUp(const TerrainLayerMap& map) {
    for (u32 z = 0; z < map.resolutionZ(); ++z) {
        for (u32 x = 0; x < map.resolutionX(); ++x) {
            if (sum(map.at(x, z)) != 255) return false;
        }
    }
    return true;
}

PaintBrush paintBrush(u32 layer, f32 radius = 10.0f, f32 strength = 1.0f) {
    PaintBrush brush;
    brush.shape.radiusM = radius;
    brush.shape.strength = strength;
    brush.shape.falloff = FalloffCurve::Constant;
    brush.mode = PaintMode::Paint;
    brush.layer = layer;
    return brush;
}

LayerStrokeRecord paintAt(TerrainLayerMap& map, const TerrainHeightmap& ground, const PaintBrush& brush, f64 x, f64 z) {
    TerrainPaintStroke stroke(map, ground, settings(), brush);
    stroke.addPoint({x, z, 1.0f});
    return stroke.finish();
}

constexpr u32 centre = 64;  // texel 64 is the middle of the terrain

}  // namespace

// --------------------------------------------------------------------------------------------------------- palette

TEST_CASE("Palette - holds 64 layers, no more, and renumbers when one goes", "[terrain][paint]") {
    TerrainPalette palette;
    for (u32 i = 0; i < kMaxPaletteLayers; ++i) {
        PaletteLayer layer;
        layer.name = "Layer " + std::to_string(i);
        REQUIRE(palette.add(layer) == static_cast<i32>(i));
    }
    REQUIRE(palette.add(PaletteLayer{}) == -1);
    REQUIRE(palette.size() == 64);
    REQUIRE(palette.indexOf("layer 7") == 7);  // case-insensitive
    REQUIRE(palette.remove(3));
    REQUIRE(palette.size() == 63);
    REQUIRE(palette.indexOf("Layer 4") == 3);
    REQUIRE(palette.move(0, 10));
    REQUIRE(palette.layers()[10].name == "Layer 0");
    REQUIRE_FALSE(palette.move(0, 99));
}

TEST_CASE("Palette - the sharpness classes are powers of two between 256 and 4096", "[terrain][paint]") {
    REQUIRE(TerrainPalette::resolutionClass(1) == 256);
    REQUIRE(TerrainPalette::resolutionClass(256) == 256);
    REQUIRE(TerrainPalette::resolutionClass(300) == 256);
    REQUIRE(TerrainPalette::resolutionClass(400) == 512);
    REQUIRE(TerrainPalette::resolutionClass(700) == 512);
    REQUIRE(TerrainPalette::resolutionClass(800) == 1024);
    REQUIRE(TerrainPalette::resolutionClass(2500) == 2048);
    REQUIRE(TerrainPalette::resolutionClass(100000) == 4096);
}

TEST_CASE("Palette - the .terrainpalette text round trips and reports bad lines", "[terrain][paint]") {
    TerrainPalette palette;
    PaletteLayer rock;
    rock.name = "Mossy rock";
    rock.albedo = "Textures/rock albedo.png";
    rock.normal = "Textures/rock_n.png";
    rock.tileSizeM = 4.5f;
    rock.rotationDegrees = 30.0f;
    rock.tint[0] = 0.8f;
    rock.resolution = 2048;
    rock.locked = true;
    palette.add(rock);
    palette.add(PaletteLayer{});

    TerrainPalette back;
    std::string error;
    REQUIRE(TerrainPalette::fromText(palette.toText(), back, &error));
    REQUIRE(back == palette);
    REQUIRE(back.layers()[0].albedo == "Textures/rock albedo.png");  // a path with a space survives

    TerrainPalette keep = palette;
    REQUIRE_FALSE(TerrainPalette::fromText("layer A\n  tile many\n", keep, &error));
    REQUIRE(error.find("line 2") != std::string::npos);
    REQUIRE(keep == palette);  // a failed read leaves the palette as it was
    REQUIRE_FALSE(TerrainPalette::fromText("albedo x.png\n", keep, &error));
    REQUIRE(error.find("after a `layer`") != std::string::npos);
    // An unknown key is skipped, so an old build opens a newer file.
    REQUIRE(TerrainPalette::fromText("layer A\n  future_key 3\n  tile 2\n", keep, &error));
    REQUIRE(keep.layers()[0].tileSizeM == 2.0f);
}

TEST_CASE("Palette - an older terrain's four layers become layers 0 to 3", "[terrain][paint]") {
    ECS::TerrainComponent terrain;
    std::snprintf(terrain.splatLayerPaths[2], sizeof(terrain.splatLayerPaths[2]), "Ground/dirt.png");
    terrain.splatTileSize = 6.0f;
    const TerrainPalette palette = TerrainPalette::fromLegacyLayers(terrain);
    REQUIRE(palette.size() == 4);
    REQUIRE(palette.layers()[2].albedo == "Ground/dirt.png");
    REQUIRE(palette.layers()[2].tileSizeM == 6.0f);
}

// ------------------------------------------------------------------------------------------------------- layer map

TEST_CASE("Layer map - starts as layer 0 and every texel adds up to 255", "[terrain][paint]") {
    TerrainLayerMap map(kRes, kRes);
    REQUIRE(map.weightOf(5, 5, 0) == 1.0f);
    REQUIRE(map.dominant(5, 5) == 0);
    REQUIRE(everyTexelAddsUp(map));
}

TEST_CASE("Layer map - blending moves shares, keeps the total, and solid paint is exactly solid", "[terrain][paint]") {
    TerrainLayerMap map(8, 8);
    map.blendToward(3, 3, 5, 0.5f, 1);
    REQUIRE(sum(map.at(3, 3)) == 255);
    REQUIRE(std::fabs(map.weightOf(3, 3, 5) - 0.5f) < 0.01f);
    REQUIRE(std::fabs(map.weightOf(3, 3, 0) - 0.5f) < 0.01f);
    map.blendToward(3, 3, 5, 1.0f, 2);
    REQUIRE(map.weightOf(3, 3, 5) == 1.0f);
    REQUIRE(map.dominant(3, 3) == 5);
    // The map reads back what the texel names: no layer is named for no weight.
    for (u32 s = 0; s < 4; ++s) {
        if (map.at(3, 3).weight[s] == 0) REQUIRE(map.at(3, 3).index[s] == map.at(3, 3).index[0]);
    }
}

TEST_CASE("Layer map - a texel keeps its four heaviest layers", "[terrain][paint]") {
    TerrainLayerMap map(4, 4);
    // Five layers one after another, each heavier than the last: the lightest goes.
    for (u32 layer = 1; layer <= 5; ++layer) map.blendToward(1, 1, layer, 0.35f, layer);
    u32 distinct = 0;
    for (u32 s = 0; s < 4; ++s) distinct += map.at(1, 1).weight[s] > 0;
    REQUIRE(distinct <= 4);
    REQUIRE(sum(map.at(1, 1)) == 255);
    REQUIRE(map.weightOf(1, 1, 5) > map.weightOf(1, 1, 0));
}

TEST_CASE("Layer map - small shares are not lost to the byte: the rounding is unbiased and deterministic", "[terrain][paint]") {
    // 400 texels, each given 0.2 % of layer 1 (half a byte): on average 0.2 % must arrive, which truncation would lose.
    TerrainLayerMap map(20, 20);
    for (u32 z = 0; z < 20; ++z) {
        for (u32 x = 0; x < 20; ++x) map.blendToward(x, z, 1, 0.002f, 7);
    }
    f64 total = 0.0;
    for (u32 z = 0; z < 20; ++z) {
        for (u32 x = 0; x < 20; ++x) total += map.weightOf(x, z, 1);
    }
    REQUIRE(std::fabs(total / 400.0 - 0.002) < 0.001);
    // Run it again: byte for byte the same.
    TerrainLayerMap again(20, 20);
    for (u32 z = 0; z < 20; ++z) {
        for (u32 x = 0; x < 20; ++x) again.blendToward(x, z, 1, 0.002f, 7);
    }
    REQUIRE(again == map);
}

TEST_CASE("Layer map - a locked layer never changes, whatever is painted", "[terrain][paint]") {
    TerrainLayerMap map(6, 6);
    map.blendToward(2, 2, 1, 0.4f, 1);  // 60 % layer 0, 40 % layer 1
    const f32 locked = map.weightOf(2, 2, 0);
    map.setLocked(0, true);
    map.blendToward(2, 2, 2, 1.0f, 2);
    REQUIRE(map.weightOf(2, 2, 0) == locked);
    map.eraseLayer(2, 2, 0, 1.0f, 3);
    REQUIRE(map.weightOf(2, 2, 0) == locked);
    map.replaceLayer(2, 2, 0, 3, 1.0f, 4);
    REQUIRE(map.weightOf(2, 2, 0) == locked);
    map.blendToward(2, 2, 0, 1.0f, 5);  // painting the locked layer itself does nothing either
    REQUIRE(map.weightOf(2, 2, 0) == locked);
    REQUIRE(sum(map.at(2, 2)) == 255);
    map.setLocked(0, false);
    map.blendToward(2, 2, 0, 1.0f, 6);
    REQUIRE(map.weightOf(2, 2, 0) == 1.0f);
}

TEST_CASE("Layer map - erase gives a layer's weight to the others, replace moves it", "[terrain][paint]") {
    TerrainLayerMap map(4, 4);
    map.blendToward(1, 1, 1, 0.5f, 1);  // half 0, half 1
    map.eraseLayer(1, 1, 1, 1.0f, 2);
    REQUIRE(map.weightOf(1, 1, 1) == 0.0f);
    REQUIRE(map.weightOf(1, 1, 0) == 1.0f);
    // A texel that is all one layer has nothing to give the weight to: it stays.
    map.eraseLayer(1, 1, 0, 1.0f, 3);
    REQUIRE(map.weightOf(1, 1, 0) == 1.0f);
    map.replaceLayer(1, 1, 0, 4, 0.5f, 4);
    REQUIRE(std::fabs(map.weightOf(1, 1, 4) - 0.5f) < 0.01f);
    map.replaceLayer(1, 1, 0, 4, 1.0f, 5);
    REQUIRE(map.weightOf(1, 1, 4) == 1.0f);
}

TEST_CASE("Layer map - smoothing blends toward the neighbours and keeps the total", "[terrain][paint]") {
    TerrainLayerMap map(8, 8);
    for (u32 z = 0; z < 8; ++z) {
        for (u32 x = 4; x < 8; ++x) map.blendToward(x, z, 1, 1.0f, 1);  // left layer 0, right layer 1
    }
    const f32 before = map.weightOf(3, 4, 1);
    map.smoothTexel(3, 4, 1.0f, 2);
    REQUIRE(map.weightOf(3, 4, 1) > before);
    REQUIRE(sum(map.at(3, 4)) == 255);
    // `smoothed` writes nothing.
    TerrainLayerMap copy = map;
    (void)copy.smoothed(2, 4, 1.0f, 3);
    REQUIRE(copy == map);
}

TEST_CASE("Layer map - removing a layer renumbers the rest and nothing is left bare", "[terrain][paint]") {
    TerrainLayerMap map(4, 4);
    map.blendToward(0, 0, 2, 1.0f, 1);  // solid layer 2
    map.blendToward(1, 0, 1, 1.0f, 1);  // solid layer 1
    map.blendToward(2, 0, 3, 0.5f, 1);  // half 0, half 3
    map.setLocked(3, true);
    map.removeLayer(1);
    REQUIRE(map.weightOf(0, 0, 1) == 1.0f);  // layer 2 is layer 1 now
    REQUIRE(map.weightOf(1, 0, 0) == 1.0f);  // layer 1 stood alone: it falls back to layer 0
    REQUIRE(std::fabs(map.weightOf(2, 0, 2) - 0.5f) < 0.01f);  // layer 3 is layer 2 now
    REQUIRE(map.locked(2));
    REQUIRE_FALSE(map.locked(3));
    REQUIRE(everyTexelAddsUp(map));
}

TEST_CASE("Layer map - moving a layer renumbers every index and the locks", "[terrain][paint]") {
    TerrainLayerMap map(2, 1);
    map.blendToward(0, 0, 3, 1.0f, 1);
    map.blendToward(1, 0, 1, 1.0f, 1);
    map.setLocked(3, true);
    map.moveLayer(3, 0);  // 3 -> 0, the others up by one
    REQUIRE(map.weightOf(0, 0, 0) == 1.0f);
    REQUIRE(map.weightOf(1, 0, 2) == 1.0f);
    REQUIRE(map.locked(0));
    REQUIRE_FALSE(map.locked(3));
}

TEST_CASE("Layer map - the legacy four-layer splat map converts both ways without loss", "[terrain][paint]") {
    TerrainSplatmap splat(16, 16);
    for (u32 z = 0; z < 16; ++z) {
        for (u32 x = 0; x < 16; ++x) {
            // Whole bytes only (n / 255), the way the old files hold them.
            const f32 a = static_cast<f32>(x * 15) / 255.0f, b = static_cast<f32>(z * 15) / 255.0f;
            const f32 c = static_cast<f32>(255 - x * 15 - z * 15 > 0 ? 255 - x * 15 - z * 15 : 0) / 255.0f;
            const f32 total = a + b + c;
            splat.set(x, z, Vec4{a / total, b / total, c / total, 0.0f});
        }
    }
    const TerrainLayerMap map = TerrainLayerMap::fromSplatmap(splat);
    REQUIRE(everyTexelAddsUp(map));
    const TerrainSplatmap back = map.toSplatmap();
    for (u32 z = 0; z < 16; ++z) {
        for (u32 x = 0; x < 16; ++x) {
            const Vec4 want = splat.sample(x, z), got = back.sample(x, z);
            REQUIRE(std::fabs(want.x - got.x) < 0.01f);
            REQUIRE(std::fabs(want.y - got.y) < 0.01f);
            REQUIRE(std::fabs(want.z - got.z) < 0.01f);
        }
    }
    // Layers above 3 cannot be shown by the old renderer: the four-layer view drops them and renormalises.
    TerrainLayerMap wide(2, 2);
    wide.blendToward(0, 0, 9, 1.0f, 1);
    REQUIRE(wide.toSplatmap().sample(0, 0).x == 1.0f);
}

TEST_CASE("Layer map - the file round trips and notices damage", "[terrain][paint]") {
    TerrainLayerMap map(33, 17);
    map.blendToward(5, 5, 7, 0.6f, 1);
    map.blendToward(6, 5, 63, 1.0f, 1);
    map.setLocked(2, true);
    const std::vector<u8> bytes = map.toBytes();
    TerrainLayerMap back;
    std::string error;
    REQUIRE(TerrainLayerMap::fromBytes(bytes.data(), bytes.size(), back, &error));
    REQUIRE(back == map);
    REQUIRE(back.locked(2));

    std::vector<u8> damaged = bytes;
    damaged[100] ^= 0x40;
    TerrainLayerMap other;
    REQUIRE_FALSE(TerrainLayerMap::fromBytes(damaged.data(), damaged.size(), other, &error));
    REQUIRE(error.find("checksum") != std::string::npos);
    REQUIRE_FALSE(TerrainLayerMap::fromBytes(bytes.data(), bytes.size() - 9, other, &error));
    std::vector<u8> wrong = bytes;
    wrong[0] = 'X';
    REQUIRE_FALSE(TerrainLayerMap::fromBytes(wrong.data(), wrong.size(), other, &error));
    REQUIRE(other.empty());
}

TEST_CASE("Layer map - saving to disk and loading back", "[terrain][paint]") {
    TerrainLayerMap map(20, 20);
    map.blendToward(3, 3, 12, 0.7f, 2);
    const std::string path = (std::filesystem::temp_directory_path() / "caffeine_test.terrainlayers").string();
    std::string error;
    REQUIRE(map.save(path, &error));
    TerrainLayerMap back;
    REQUIRE(TerrainLayerMap::load(path, back, &error));
    REQUIRE(back == map);
    std::filesystem::remove(path);
    REQUIRE_FALSE(TerrainLayerMap::load(path, back, &error));
}

// ------------------------------------------------------------------------------------------------------- painting

TEST_CASE("Paint stroke - a dab paints its layer inside the radius and nowhere else", "[terrain][paint]") {
    TerrainLayerMap map(kRes, kRes);
    const TerrainHeightmap ground = flatGround();
    const LayerStrokeRecord record = paintAt(map, ground, paintBrush(3, 10.0f), 0.0, 0.0);
    REQUIRE_FALSE(record.empty());
    REQUIRE(map.weightOf(centre, centre, 3) == 1.0f);
    REQUIRE(map.weightOf(centre + 4, centre, 3) == 1.0f);   // 8 m away, inside 10 m
    REQUIRE(map.weightOf(centre + 8, centre, 3) == 0.0f);   // 16 m away
    REQUIRE(everyTexelAddsUp(map));
    REQUIRE(record.region.valid);
    REQUIRE(record.region.minX <= centre - 4);
}

TEST_CASE("Paint stroke - the same path gives the same layers at any sampling rate", "[terrain][paint]") {
    const TerrainHeightmap ground = flatGround();
    auto run = [&](int points) {
        TerrainLayerMap map(kRes, kRes);
        PaintBrush brush = paintBrush(2, 8.0f, 0.4f);
        brush.shape.falloff = FalloffCurve::Smooth;
        TerrainPaintStroke stroke(map, ground, settings(), brush);
        for (int i = 0; i <= points; ++i) stroke.addPoint({-60.0 + 120.0 * i / points, 10.0 * std::sin(i * 0.0 + 1.0), 1.0f});
        stroke.finish();
        return map;
    };
    REQUIRE(run(30) == run(144));
    REQUIRE(run(2) == run(500));
}

TEST_CASE("Paint stroke - every mode undoes bit for bit and redoes exactly", "[terrain][paint]") {
    const TerrainHeightmap ground = flatGround();
    for (PaintMode mode : {PaintMode::Paint, PaintMode::Erase, PaintMode::Replace, PaintMode::Smooth}) {
        TerrainLayerMap map(kRes, kRes);
        // Some variety to act on: a patch of layer 1 and a half-blended edge.
        paintAt(map, ground, paintBrush(1, 14.0f, 1.0f), -10.0, 0.0);
        PaintBrush soft = paintBrush(2, 12.0f, 0.5f);
        soft.shape.falloff = FalloffCurve::Smooth;
        paintAt(map, ground, soft, 6.0, 0.0);
        const TerrainLayerMap start = map;

        PaintBrush brush = paintBrush(mode == PaintMode::Replace ? 3 : 1, 16.0f, 0.7f);
        brush.mode = mode;
        brush.fromLayer = 1;
        brush.shape.falloff = FalloffCurve::Smooth;
        TerrainPaintStroke stroke(map, ground, settings(), brush);
        for (int i = 0; i <= 12; ++i) stroke.addPoint({-20.0 + 4.0 * i, 0.0, 1.0f});
        const LayerStrokeRecord record = stroke.finish();
        const TerrainLayerMap done = map;
        REQUIRE(done == map);
        if (mode != PaintMode::Smooth) REQUIRE(!(done == start));
        REQUIRE(everyTexelAddsUp(map));

        record.applyBefore(map);
        REQUIRE(map == start);
        record.applyAfter(map);
        REQUIRE(map == done);
    }
}

TEST_CASE("Paint stroke - a smoothing stamp does not depend on the order of the texels", "[terrain][paint]") {
    const TerrainHeightmap ground = flatGround();
    TerrainLayerMap map(kRes, kRes);
    for (u32 z = 0; z < kRes; ++z) {
        for (u32 x = centre; x < kRes; ++x) map.blendToward(x, z, 1, 1.0f, 1);  // a hard edge down the middle
    }
    PaintBrush brush = paintBrush(0, 20.0f, 1.0f);
    brush.mode = PaintMode::Smooth;
    brush.shape.falloff = FalloffCurve::Smooth;
    paintAt(map, ground, brush, 0.0, 0.0);
    // Mirror symmetry about the edge: the blend is the same distance each side of it.
    REQUIRE(std::fabs(map.weightOf(centre - 1, centre, 1) - (1.0f - map.weightOf(centre, centre, 1))) < 0.05f);
    REQUIRE(map.weightOf(centre - 1, centre, 1) > 0.0f);
    REQUIRE(map.weightOf(centre, centre, 0) > 0.0f);
}

TEST_CASE("Paint stroke - protection keeps a layer off steep ground", "[terrain][paint]") {
    const TerrainHeightmap ground = rampGround();
    TerrainLayerMap map(kRes, kRes);
    PaintBrush brush = paintBrush(2, 40.0f, 1.0f);
    BiomeTerm gentle;
    gentle.mask = "slope";
    gentle.rise0 = 0.0f;
    gentle.rise1 = 0.0f;
    gentle.fall1 = 0.1f;
    gentle.fall0 = 0.2f;
    brush.shape.protect = {gentle};
    paintAt(map, ground, brush, 0.0, 0.0);
    REQUIRE(map.weightOf(centre - 10, centre, 2) == 1.0f);   // flat side
    REQUIRE(map.weightOf(centre + 10, centre, 2) == 0.0f);   // the 45 degree side stayed as it was
    // An unknown mask protects everything.
    TerrainLayerMap none(kRes, kRes);
    BiomeTerm ghost;
    ghost.mask = "no_such_mask";
    PaintBrush blocked = paintBrush(2, 40.0f, 1.0f);
    blocked.shape.protect = {ghost};
    REQUIRE(paintAt(none, ground, blocked, 0.0, 0.0).empty());
}

TEST_CASE("Paint stroke - mirror symmetry paints both sides once", "[terrain][paint]") {
    const TerrainHeightmap ground = flatGround();
    TerrainLayerMap map(kRes, kRes);
    PaintBrush brush = paintBrush(4, 6.0f, 1.0f);
    brush.shape.symmetry = Symmetry::MirrorX;
    paintAt(map, ground, brush, 30.0, 0.0);
    REQUIRE(map.weightOf(centre + 15, centre, 4) == 1.0f);
    REQUIRE(map.weightOf(centre - 15, centre, 4) == 1.0f);
    REQUIRE(map.weightOf(centre, centre, 4) == 0.0f);
}

TEST_CASE("Paint stroke - a stamp picture shapes the paint, and a locked layer is left alone", "[terrain][paint]") {
    const TerrainHeightmap ground = flatGround();
    TerrainLayerMap map(kRes, kRes);
    PaintBrush brush = paintBrush(1, 20.0f, 1.0f);
    BrushStamp stamp;  // lit only in the left half
    stamp.size = 8;
    stamp.alpha.assign(64, 0.0f);
    for (u32 z = 0; z < 8; ++z) {
        for (u32 x = 0; x < 4; ++x) stamp.alpha[z * 8 + x] = 1.0f;
    }
    brush.shape.stamp = stamp;
    paintAt(map, ground, brush, 0.0, 0.0);
    REQUIRE(map.weightOf(centre - 5, centre, 1) == 1.0f);
    REQUIRE(map.weightOf(centre + 5, centre, 1) == 0.0f);

    map.setLocked(0, true);
    TerrainLayerMap lockedMap(kRes, kRes);
    lockedMap.setLocked(0, true);
    paintAt(lockedMap, ground, paintBrush(3, 10.0f), 0.0, 0.0);
    REQUIRE(lockedMap.weightOf(centre, centre, 0) == 1.0f);  // all of the texel is the locked layer: nothing can give way
}

TEST_CASE("Paint stroke - a stroke on a large map touches only the blocks it needs, and is quick", "[terrain][paint]") {
    constexpr u32 big = 2049;
    TerrainLayerMap map(big, big);
    ECS::TerrainComponent t = settings();
    t.resolutionX = t.resolutionZ = big;
    t.worldSizeX = t.worldSizeZ = 2048.0f;
    TerrainHeightmap ground(big, big);
    ground.fill(0.3f);
    PaintBrush brush = paintBrush(5, 12.0f, 0.5f);
    brush.shape.falloff = FalloffCurve::Smooth;
    const auto begin = std::chrono::steady_clock::now();
    TerrainPaintStroke stroke(map, ground, t, brush);
    for (int i = 0; i <= 40; ++i) stroke.addPoint({-100.0 + 5.0 * i, 0.0, 1.0f});
    const LayerStrokeRecord record = stroke.finish();
    const f64 ms = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - begin).count();
    REQUIRE(record.blocks.size() <= 8);
    REQUIRE(record.bytes() < 1024 * 1024);
    REQUIRE(ms < 400.0);
}

// ----------------------------------------------------------------------------------------------------- auto-layer

TEST_CASE("Auto-layer - rules on slope paint rock on the ramp and grass on the flat", "[terrain][paint]") {
    const TerrainHeightmap ground = rampGround();
    TerrainLayerMap map(kRes, kRes);
    BiomeTable rules;
    std::string error;
    REQUIRE(BiomeTable::fromText("class 1 base 1\n  slope 0.3 0.4 1 1\n", rules, &error));  // layer 1: steep ground
    AutoLayerOptions options;
    options.fallbackLayer = 0;
    const LayerStrokeRecord record = autoLayer(map, ground, settings(), nullptr, rules, options);
    REQUIRE_FALSE(record.empty());
    REQUIRE(map.weightOf(centre - 20, centre, 0) == 1.0f);
    REQUIRE(map.weightOf(centre + 20, centre, 1) == 1.0f);
    REQUIRE(everyTexelAddsUp(map));
    // One auto-layering is one undo step.
    record.applyBefore(map);
    REQUIRE(map == TerrainLayerMap(kRes, kRes));
    record.applyAfter(map);
    REQUIRE(map.weightOf(centre + 20, centre, 1) == 1.0f);
}

TEST_CASE("Auto-layer - rules share a texel by density, and a locked layer keeps its share", "[terrain][paint]") {
    const TerrainHeightmap ground = flatGround();
    TerrainLayerMap map(kRes, kRes);
    BiomeTable rules;
    std::string error;
    REQUIRE(BiomeTable::fromText("class 1 base 0.75\nclass 2 base 0.25\n", rules, &error));
    autoLayer(map, ground, settings(), nullptr, rules);
    REQUIRE(std::fabs(map.weightOf(10, 10, 1) - 0.75f) < 0.01f);
    REQUIRE(std::fabs(map.weightOf(10, 10, 2) - 0.25f) < 0.01f);

    TerrainLayerMap locked(kRes, kRes);
    locked.blendToward(10, 10, 5, 0.4f, 1);  // 40 % layer 5
    locked.setLocked(5, true);
    const f32 kept = locked.weightOf(10, 10, 5);
    autoLayer(locked, ground, settings(), nullptr, rules);
    REQUIRE(locked.weightOf(10, 10, 5) == kept);
    REQUIRE(sum(locked.at(10, 10)) == 255);
}

TEST_CASE("Auto-layer - a region covers only its texels", "[terrain][paint]") {
    const TerrainHeightmap ground = flatGround();
    TerrainLayerMap map(kRes, kRes);
    BiomeTable rules;
    std::string error;
    REQUIRE(BiomeTable::fromText("class 3 base 1\n", rules, &error));
    AutoLayerOptions options;
    options.region = {10, 10, 30, 30, true};
    const LayerStrokeRecord record = autoLayer(map, ground, settings(), nullptr, rules, options);
    REQUIRE(map.weightOf(20, 20, 3) == 1.0f);
    REQUIRE(map.weightOf(40, 40, 3) == 0.0f);
    REQUIRE(record.region.minX == 10);
    REQUIRE(record.region.maxX == 30);
}
