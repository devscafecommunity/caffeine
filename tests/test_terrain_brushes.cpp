#include "catch.hpp"
#include "terrain/edit/BrushStamps.hpp"
#include "terrain/edit/TerrainStroke.hpp"

#include <chrono>
#include <cmath>
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
void put(TerrainHeightmap& map, u32 x, u32 z, f32 v) { map.heights()[static_cast<size_t>(z) * map.resolutionX() + x] = v; }

/// A terrain 256 m wide on 129 vertices (2 m cells): vertex 64 is the centre.
constexpr u32 kRes = 129;
constexpr f32 kSize = 256.0f;
u32 vertexOf(f32 metres) { return static_cast<u32>(std::llround(64.0 + metres / 2.0)); }

TerrainStroke make(TerrainHeightmap& map, const StrokeBrush& brush) {
    return TerrainStroke(map, settingsFor(kRes, kSize, 80.0f), brush);
}

/// A rolling terrain from arithmetic only, so every test starts from the same ground.
TerrainHeightmap rolling() {
    TerrainHeightmap map(kRes, kRes);
    for (u32 z = 0; z < kRes; ++z) {
        for (u32 x = 0; x < kRes; ++x) put(map, x, z, 0.4f + static_cast<f32>((x * 7 + z * 13) % 17) * 0.006f + static_cast<f32>(x) * 0.0012f);
    }
    return map;
}

}  // namespace

// ------------------------------------------------------------------------------------------------------------ stamps

TEST_CASE("Brush stamps - the library has at least forty, with unique ids", "[terrain][brush]") {
    const auto& stamps = proceduralStamps();
    REQUIRE(stamps.size() >= 40);
    std::set<std::string> ids;
    std::set<std::string> categories;
    for (const StampInfo& info : stamps) {
        REQUIRE(ids.insert(info.id).second);
        REQUIRE(std::string(info.name).size() > 0);
        REQUIRE(info.radiusM > 0.0f);
        REQUIRE(info.strength > 0.0f);
        REQUIRE(info.spacingRatio > 0.0f);
        categories.insert(info.category);
    }
    REQUIRE(categories.size() >= 6);
    REQUIRE(findProceduralStamp("round_soft") != nullptr);
    REQUIRE(findProceduralStamp("no_such_stamp") == nullptr);
}

TEST_CASE("Brush stamps - every stamp is a finite 0..1 image with a shape, the same on every call", "[terrain][brush]") {
    for (const StampInfo& info : proceduralStamps()) {
        INFO(info.id);
        BrushStamp a, b;
        REQUIRE(generateProceduralStamp(info.id, 64, 7, a));
        REQUIRE(generateProceduralStamp(info.id, 64, 7, b));
        REQUIRE(a.size == 64);
        REQUIRE(a.alpha == b.alpha);  // deterministic
        f32 low = 2.0f, high = -1.0f;
        for (const f32 v : a.alpha) {
            REQUIRE(std::isfinite(v));
            REQUIRE(v >= 0.0f);
            REQUIRE(v <= 1.0f);
            low = std::min(low, v);
            high = std::max(high, v);
        }
        REQUIRE(high - low > 0.25f);  // not a flat image
        if (info.signedStamp) {  // a signed stamp digs (a valley, a bed) and a crater also raises its rim
            REQUIRE(low < 0.45f);
            REQUIRE(high >= 0.5f - 1e-4f);
            if (std::string(info.id).rfind("crater", 0) == 0) REQUIRE(high > 0.55f);
        }
    }
}

TEST_CASE("Brush stamps - the seed changes the noisy ones and only them", "[terrain][brush]") {
    BrushStamp a, b;
    generateProceduralStamp("perlin_fine", 64, 1, a);
    generateProceduralStamp("perlin_fine", 64, 2, b);
    REQUIRE(a.alpha != b.alpha);
    generateProceduralStamp("round_soft", 64, 1, a);
    generateProceduralStamp("round_soft", 64, 2, b);
    REQUIRE(a.alpha == b.alpha);
    // The round tip is symmetric left to right and top to bottom.
    for (u32 z = 0; z < 64; ++z) {
        for (u32 x = 0; x < 32; ++x) REQUIRE(a.alpha[z * 64 + x] == Approx(a.alpha[z * 64 + (63 - x)]).margin(1e-5f));
    }
}

TEST_CASE("Brush stamps - bad ids and sizes are refused, and the whole library is quick", "[terrain][brush][budget]") {
    BrushStamp stamp;
    REQUIRE_FALSE(generateProceduralStamp("nope", 64, 0, stamp));
    REQUIRE_FALSE(generateProceduralStamp("round_soft", 2, 0, stamp));
    REQUIRE_FALSE(generateProceduralStamp("round_soft", 4096, 0, stamp));
    const auto start = std::chrono::steady_clock::now();
    for (const StampInfo& info : proceduralStamps()) REQUIRE(generateProceduralStamp(info.id, 128, 3, stamp));
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    INFO(ms << " ms for the library at 128 x 128");
    REQUIRE(ms < 1500.0);
}

TEST_CASE("Brush stamps - a crater stamp digs a bowl inside a raised rim", "[terrain][brush]") {
    TerrainHeightmap map = flat(kRes, 0.5f);
    StrokeBrush brush;
    brush.mode = TerrainBrushMode::Raise;
    brush.radiusM = 20.0f;
    brush.strength = 4.0f;
    brush.falloff = FalloffCurve::Constant;
    brush.signedStamp = true;
    generateProceduralStamp("crater_simple", 128, 0, brush.stamp);
    TerrainStroke stroke = make(map, brush);
    stroke.addPoint({0.0, 0.0, 1.0f});
    const u32 c = 64;
    REQUIRE(at(map, c, c) < 0.5f - 2.0f / 80.0f);       // the centre is lower by metres
    REQUIRE(at(map, c + 8, c) > 0.5f);                  // the rim (about 16 m out) is higher
}

// ------------------------------------------------------------------------------------------------------------- modes

TEST_CASE("Brush modes - ramp grades a slope between where the stroke began and the cursor", "[terrain][brush]") {
    TerrainHeightmap map = flat(kRes, 0.5f);  // 40 m everywhere
    StrokeBrush brush;
    brush.mode = TerrainBrushMode::Ramp;
    brush.radiusM = 8.0f;
    brush.strength = 1.0f;
    brush.falloff = FalloffCurve::Constant;
    brush.spacingM = 2.0f;
    brush.targetHeightM = 60.0f;  // end of the ramp, metres
    TerrainStroke stroke = make(map, brush);
    stroke.addPoint({-40.0, 0.0, 1.0f});
    stroke.addPoint({40.0, 0.0, 1.0f});
    // 40 m at the start, 60 m at the end, linear between.
    REQUIRE(at(map, vertexOf(-40.0f), 64) * 80.0f == Approx(40.0f).margin(1.5f));
    REQUIRE(at(map, vertexOf(0.0f), 64) * 80.0f == Approx(50.0f).margin(1.5f));
    REQUIRE(at(map, vertexOf(40.0f), 64) * 80.0f == Approx(60.0f).margin(1.5f));
    REQUIRE(at(map, vertexOf(0.0f), 80) == Approx(0.5f).margin(1e-6f));  // beside the stroke nothing moved
}

TEST_CASE("Brush modes - terrace pulls heights onto steps", "[terrain][brush]") {
    auto onSteps = [](const TerrainHeightmap& map) {
        u32 near = 0, total = 0;
        for (u32 z = 40; z < 90; ++z) {
            for (u32 x = 40; x < 90; ++x) {
                const f32 metres = at(map, x, z) * 80.0f;
                const f32 off = std::fabs(metres - 2.0f * std::round(metres / 2.0f));
                near += off < 0.35f ? 1 : 0;
                ++total;
            }
        }
        return static_cast<f32>(near) / static_cast<f32>(total);
    };
    TerrainHeightmap map = rolling();
    const f32 before = onSteps(map);
    StrokeBrush brush;
    brush.mode = TerrainBrushMode::Terrace;
    brush.radiusM = 60.0f;
    brush.strength = 1.0f;
    brush.stepM = 2.0f;
    brush.falloff = FalloffCurve::Constant;
    brush.spacingM = 15.0f;
    TerrainStroke stroke = make(map, brush);
    stroke.addPoint({0.0, 0.0, 1.0f});
    stroke.addPoint({0.0, 1.0, 1.0f});
    REQUIRE(onSteps(map) > before + 0.2f);
    REQUIRE(onSteps(map) > 0.6f);
}

TEST_CASE("Brush modes - pinch narrows a hill and bulge widens it", "[terrain][brush]") {
    auto hill = [] {
        TerrainHeightmap map = flat(kRes, 0.3f);
        for (u32 z = 0; z < kRes; ++z) {
            for (u32 x = 0; x < kRes; ++x) {
                const f32 d = std::sqrt(static_cast<f32>((x - 64) * (x - 64) + (z - 64) * (z - 64))) * 2.0f;  // metres
                put(map, x, z, 0.3f + std::max(0.0f, 1.0f - d / 30.0f) * 0.3f);
            }
        }
        return map;
    };
    auto sum = [](const TerrainHeightmap& map) {
        f64 total = 0.0;
        for (u32 z = 44; z < 85; ++z) for (u32 x = 44; x < 85; ++x) total += at(map, x, z);
        return total;
    };
    StrokeBrush brush;
    brush.radiusM = 40.0f;
    brush.strength = 1.0f;
    brush.falloff = FalloffCurve::Constant;
    TerrainHeightmap base = hill();
    TerrainHeightmap pinched = hill(), bulged = hill();
    brush.mode = TerrainBrushMode::Pinch;
    { TerrainStroke s = make(pinched, brush); s.addPoint({0.0, 0.0, 1.0f}); }
    brush.mode = TerrainBrushMode::Bulge;
    { TerrainStroke s = make(bulged, brush); s.addPoint({0.0, 0.0, 1.0f}); }
    REQUIRE(sum(pinched) < sum(base));
    REQUIRE(sum(bulged) > sum(base));
}

TEST_CASE("Brush modes - the stamp-shape modes keep the higher or the lower of the ground and the shape", "[terrain][brush]") {
    StrokeBrush brush;
    brush.radiusM = 10.0f;
    brush.falloff = FalloffCurve::Constant;
    brush.strength = 5.0f;
    // Max: the centre rises to the ground plus the stamp.
    TerrainHeightmap up = flat(kRes, 0.5f);
    brush.mode = TerrainBrushMode::StampMax;
    { TerrainStroke s = make(up, brush); s.addPoint({0.0, 0.0, 1.0f}); }
    REQUIRE(at(up, 64, 64) * 80.0f == Approx(45.0f).margin(0.01f));
    brush.strength = 2.0f;  // the second stamp builds on the new ground: it adds, it never takes back
    { TerrainStroke s = make(up, brush); s.addPoint({0.0, 0.0, 1.0f}); }
    REQUIRE(at(up, 64, 64) * 80.0f == Approx(47.0f).margin(0.01f));
    // Min: cut down from the centre.
    TerrainHeightmap down = flat(kRes, 0.5f);
    brush.mode = TerrainBrushMode::StampMin;
    brush.strength = 3.0f;
    { TerrainStroke s = make(down, brush); s.addPoint({0.0, 0.0, 1.0f}); }
    REQUIRE(at(down, 64, 64) * 80.0f == Approx(37.0f).margin(0.01f));
    // Absolute: the ground under the stamp takes the stamp's height over the ground at its centre.
    TerrainHeightmap slope = flat(kRes, 0.5f);
    for (u32 z = 0; z < kRes; ++z) for (u32 x = 0; x < kRes; ++x) put(slope, x, z, 0.4f + static_cast<f32>(x) * 0.001f);
    brush.mode = TerrainBrushMode::StampAbsolute;
    brush.strength = 6.0f;
    { TerrainStroke s = make(slope, brush); s.addPoint({0.0, 0.0, 1.0f}); }
    const f32 centre = (0.4f + 64.0f * 0.001f) * 80.0f;
    REQUIRE(at(slope, 64, 64) * 80.0f == Approx(centre + 6.0f).margin(0.05f));
    REQUIRE(at(slope, 62, 64) * 80.0f == Approx(centre + 6.0f).margin(0.05f));  // levelled across the stamp
}

TEST_CASE("Brush modes - clone copies the relief from an offset", "[terrain][brush]") {
    TerrainHeightmap map = flat(kRes, 0.5f);
    for (u32 z = 0; z < kRes; ++z) {
        for (u32 x = 0; x < kRes; ++x) {
            const f32 d = std::sqrt(static_cast<f32>((x - 74) * (x - 74) + (z - 64) * (z - 64))) * 2.0f;  // a bump at x = +20 m
            put(map, x, z, 0.5f + std::max(0.0f, 1.0f - d / 10.0f) * 0.125f);
        }
    }
    StrokeBrush brush;
    brush.mode = TerrainBrushMode::Clone;
    brush.radiusM = 14.0f;
    brush.strength = 1.0f;
    brush.falloff = FalloffCurve::Constant;
    brush.offsetX = 40.0f;  // copy from 40 m to the right
    TerrainStroke stroke = make(map, brush);
    stroke.addPoint({-20.0, 0.0, 1.0f});
    // The bump of x = +20 m appears at x = -20 m, 10 m high.
    REQUIRE(at(map, vertexOf(-20.0f), 64) * 80.0f == Approx(50.0f).margin(1.0f));
    REQUIRE(at(map, vertexOf(-20.0f), 90) * 80.0f == Approx(40.0f).margin(0.01f));
}

TEST_CASE("Brush modes - smudge drags the relief along the stroke", "[terrain][brush]") {
    TerrainHeightmap map = flat(kRes, 0.4f);
    for (u32 z = 62; z <= 66; ++z) for (u32 x = 60; x <= 64; ++x) put(map, x, z, 0.8f);  // a block left of the middle
    const f32 ahead = at(map, 70, 64);
    StrokeBrush brush;
    brush.mode = TerrainBrushMode::Smudge;
    brush.radiusM = 14.0f;
    brush.strength = 0.8f;
    brush.falloff = FalloffCurve::Constant;
    brush.spacingM = 2.0f;
    TerrainStroke stroke = make(map, brush);
    stroke.addPoint({-10.0, 0.0, 1.0f});
    stroke.addPoint({14.0, 0.0, 1.0f});
    REQUIRE(at(map, 70, 64) > ahead + 0.05f);  // material arrived in front of the block
}

TEST_CASE("Brush modes - thermal erosion lets a cliff slide down without losing material", "[terrain][brush]") {
    TerrainHeightmap map = flat(kRes, 0.2f);
    for (u32 z = 0; z < kRes; ++z) for (u32 x = 64; x < kRes; ++x) put(map, x, z, 0.5f);  // a 24 m wall at x = 0
    auto volume = [](const TerrainHeightmap& m) { f64 v = 0.0; for (const f32 h : m.heights()) v += h; return v; };
    auto steepest = [](const TerrainHeightmap& m) {
        f32 s = 0.0f;
        for (u32 z = 55; z < 80; ++z) for (u32 x = 50; x < 80; ++x) s = std::max(s, std::fabs(at(m, x + 1, z) - at(m, x, z)) * 80.0f / 2.0f);
        return s;
    };
    const f64 before = volume(map);
    const f32 steep = steepest(map);
    StrokeBrush brush;
    brush.mode = TerrainBrushMode::ErodeThermal;
    brush.radiusM = 30.0f;
    brush.strength = 1.0f;
    brush.talusDegrees = 30.0f;
    brush.iterations = 16;
    brush.falloff = FalloffCurve::Constant;
    TerrainStroke stroke = make(map, brush);
    stroke.addPoint({0.0, 0.0, 1.0f});
    stroke.addPoint({0.0, 20.0, 1.0f});
    REQUIRE(steepest(map) < steep * 0.7f);
    REQUIRE(volume(map) == Approx(before).margin(1e-3));  // what slid is still there
}

TEST_CASE("Brush modes - hydraulic erosion is deterministic, bounded and cuts the ground", "[terrain][brush]") {
    auto run = [](u64 seed) {
        TerrainHeightmap map = rolling();
        StrokeBrush brush;
        brush.mode = TerrainBrushMode::ErodeHydraulic;
        brush.radiusM = 30.0f;
        brush.strength = 1.0f;
        brush.droplets = 64;
        brush.seed = seed;
        TerrainStroke stroke = make(map, brush);
        stroke.addPoint({0.0, 0.0, 1.0f});
        stroke.addPoint({20.0, 0.0, 1.0f});
        return map.heights();
    };
    const auto a = run(5);
    REQUIRE(a == run(5));
    REQUIRE_FALSE(a == run(6));
    REQUIRE_FALSE(a == rolling().heights());
    for (const f32 h : a) { REQUIRE(h >= 0.0f); REQUIRE(h <= 1.0f); }
}

TEST_CASE("Brush modes - an erosion stamp costs a fraction of a frame", "[terrain][brush][budget]") {
    for (const auto mode : {TerrainBrushMode::ErodeThermal, TerrainBrushMode::ErodeHydraulic}) {
        TerrainHeightmap map = rolling();
        StrokeBrush brush;
        brush.mode = mode;
        brush.radiusM = 40.0f;
        brush.strength = 1.0f;
        TerrainStroke stroke = make(map, brush);
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 20; ++i) stroke.addPoint({-40.0 + 4.0 * i, 0.0, 1.0f});
        const double perStamp = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / std::max(1u, stroke.stamps());
        INFO(static_cast<int>(mode) << ": " << perStamp << " ms per stamp");
        REQUIRE(perStamp < 12.0);
    }
}

// ---------------------------------------------------------------------------------- jitter, symmetry, protection

TEST_CASE("Brush variation - jitter is deterministic for a seed and changes with it", "[terrain][brush]") {
    auto run = [](f32 position, f32 rotation, f32 size, f32 opacity, u64 seed) {
        TerrainHeightmap map = flat(kRes, 0.5f);
        StrokeBrush brush;
        brush.radiusM = 10.0f;
        brush.strength = 1.0f;
        brush.spacingM = 4.0f;
        brush.positionJitter = position;
        brush.rotationJitterDegrees = rotation;
        brush.sizeJitter = size;
        brush.opacityJitter = opacity;
        brush.seed = seed;
        generateProceduralStamp("boulders", 64, 3, brush.stamp);
        TerrainStroke stroke = make(map, brush);
        stroke.addPoint({-30.0, 0.0, 1.0f});
        stroke.addPoint({30.0, 0.0, 1.0f});
        return map.heights();
    };
    const auto plain = run(0, 0, 0, 0, 1);
    REQUIRE(run(0.8f, 0, 0, 0, 1) == run(0.8f, 0, 0, 0, 1));
    REQUIRE_FALSE(run(0.8f, 0, 0, 0, 1) == plain);
    REQUIRE_FALSE(run(0.8f, 0, 0, 0, 1) == run(0.8f, 0, 0, 0, 2));
    REQUIRE_FALSE(run(0, 90.0f, 0, 0, 1) == plain);
    REQUIRE_FALSE(run(0, 0, 0.8f, 0, 1) == plain);
    REQUIRE_FALSE(run(0, 0, 0, 0.8f, 1) == plain);
}

TEST_CASE("Brush variation - mirror symmetry repeats a stamp across the axis, image included", "[terrain][brush]") {
    StrokeBrush brush;
    brush.radiusM = 12.0f;
    brush.strength = 4.0f;
    brush.falloff = FalloffCurve::Constant;
    brush.stamp.size = 8;  // only the right half of the brush is solid
    brush.stamp.alpha.assign(64, 0.0f);
    for (u32 z = 0; z < 8; ++z) for (u32 x = 4; x < 8; ++x) brush.stamp.alpha[z * 8 + x] = 1.0f;

    TerrainHeightmap map = flat(kRes, 0.5f);
    brush.symmetry = Symmetry::MirrorX;
    TerrainStroke stroke = make(map, brush);
    stroke.addPoint({30.0, 0.0, 1.0f});
    // The stamp at +30 m raised its +x half; the mirror at -30 m raised its -x half.
    REQUIRE(at(map, vertexOf(33.0f), 64) > 0.5f);
    REQUIRE(at(map, vertexOf(27.0f), 64) == 0.5f);
    REQUIRE(at(map, vertexOf(-33.0f), 64) > 0.5f);
    REQUIRE(at(map, vertexOf(-27.0f), 64) == 0.5f);
    // The whole result is a mirror image.
    for (u32 z = 0; z < kRes; ++z) for (u32 x = 0; x < kRes; ++x) REQUIRE(at(map, x, z) == Approx(at(map, kRes - 1 - x, z)).margin(1e-6f));

    // Both axes: four copies; radial 4: four copies turned a quarter each.
    brush.stamp = {};
    TerrainHeightmap both = flat(kRes, 0.5f);
    brush.symmetry = Symmetry::MirrorBoth;
    { TerrainStroke s = make(both, brush); s.addPoint({30.0, 20.0, 1.0f}); }
    for (const f32 sx : {-1.0f, 1.0f}) for (const f32 sz : {-1.0f, 1.0f}) REQUIRE(at(both, vertexOf(30.0f * sx), vertexOf(20.0f * sz)) > 0.5f);
    TerrainHeightmap radial = flat(kRes, 0.5f);
    brush.symmetry = Symmetry::Radial;
    brush.radialCount = 4;
    { TerrainStroke s = make(radial, brush); s.addPoint({30.0, 0.0, 1.0f}); }
    REQUIRE(at(radial, vertexOf(30.0f), vertexOf(0.0f)) > 0.5f);
    REQUIRE(at(radial, vertexOf(0.0f), vertexOf(30.0f)) > 0.5f);
    REQUIRE(at(radial, vertexOf(-30.0f), vertexOf(0.0f)) > 0.5f);
    REQUIRE(at(radial, vertexOf(0.0f), vertexOf(-30.0f)) > 0.5f);
    REQUIRE(at(radial, vertexOf(21.0f), vertexOf(21.0f)) == 0.5f);  // nothing between the copies
    // A stamp on the axis is laid once (not twice as strong).
    TerrainHeightmap axis = flat(kRes, 0.5f);
    brush.symmetry = Symmetry::MirrorX;
    { TerrainStroke s = make(axis, brush); s.addPoint({0.0, 0.0, 1.0f}); }
    REQUIRE(at(axis, 64, 64) * 80.0f == Approx(40.0f + 4.0f).margin(0.01f));
}

TEST_CASE("Brush variation - protection keeps the brush off what its masks exclude", "[terrain][brush]") {
    // Flat on the left, a 45 degree ramp on the right.
    auto ground = [] {
        TerrainHeightmap map = flat(kRes, 0.3f);
        for (u32 z = 0; z < kRes; ++z) for (u32 x = 64; x < kRes; ++x) put(map, x, z, 0.3f + static_cast<f32>(x - 64) * 2.0f / 80.0f);
        return map;
    };
    StrokeBrush brush;
    brush.mode = TerrainBrushMode::Raise;
    brush.radiusM = 40.0f;
    brush.strength = 3.0f;
    brush.falloff = FalloffCurve::Constant;
    BiomeTerm gentle;
    gentle.mask = "slope";  // 0 flat .. 1 vertical
    gentle.rise0 = 0.0f; gentle.rise1 = 0.0f; gentle.fall1 = 0.1f; gentle.fall0 = 0.2f;
    brush.protect = {gentle};
    TerrainHeightmap map = ground();
    const TerrainHeightmap before = ground();
    TerrainStroke stroke = make(map, brush);
    stroke.addPoint({0.0, 0.0, 1.0f});
    REQUIRE(at(map, 50, 64) > at(before, 50, 64));   // the flat side was raised
    REQUIRE(at(map, 80, 64) == at(before, 80, 64));  // the steep side was left alone

    // A height band: only what lies between 20 and 30 m changes.
    TerrainHeightmap banded = flat(kRes, 0.5f);
    for (u32 x = 0; x < 64; ++x) for (u32 z = 0; z < kRes; ++z) put(banded, x, z, 0.3f);  // 24 m on the left, 40 m on the right
    StrokeBrush band = brush;
    BiomeTerm height;
    height.mask = "height";
    height.rise0 = 0.2f; height.rise1 = 0.25f; height.fall1 = 0.35f; height.fall0 = 0.4f;  // 16 m .. 32 m
    band.protect = {height};
    TerrainStroke s = make(banded, band);
    s.addPoint({0.0, 0.0, 1.0f});
    REQUIRE(at(banded, 50, 64) > 0.3f);
    REQUIRE(at(banded, 80, 64) == 0.5f);

    // A mask that does not exist protects everything.
    TerrainHeightmap none = flat(kRes, 0.5f);
    StrokeBrush unknown = brush;
    BiomeTerm ghost;
    ghost.mask = "no_such_mask";
    unknown.protect = {ghost};
    TerrainStroke t = make(none, unknown);
    t.addPoint({0.0, 0.0, 1.0f});
    REQUIRE(t.finish().empty());
}

TEST_CASE("Brush variation - a stored mask protects too", "[terrain][brush]") {
    TerrainHeightmap map = flat(kRes, 0.5f);
    TerrainMasks masks;
    std::vector<u8> data(65 * 65, 0);
    for (u32 z = 0; z < 65; ++z) for (u32 x = 0; x < 32; ++x) data[z * 65 + x] = 255;  // the left half is allowed
    REQUIRE(masks.set("allowed", 65, 65, data.data()));
    StrokeBrush brush;
    brush.radiusM = 40.0f;
    brush.strength = 2.0f;
    brush.falloff = FalloffCurve::Constant;
    BiomeTerm term;
    term.mask = "allowed";
    term.rise0 = 0.4f; term.rise1 = 0.5f; term.fall1 = 1.0f; term.fall0 = 1.1f;
    brush.protect = {term};
    TerrainStroke stroke = make(map, brush);
    stroke.setMasks(&masks);
    stroke.addPoint({0.0, 0.0, 1.0f});
    REQUIRE(at(map, 50, 64) > 0.5f);
    REQUIRE(at(map, 80, 64) == 0.5f);
}

// ----------------------------------------------------------------------------------------------- every mode, as a whole

TEST_CASE("Brush modes - every mode undoes exactly and repeats exactly", "[terrain][brush]") {
    const TerrainBrushMode modes[] = {
        TerrainBrushMode::Raise, TerrainBrushMode::Lower, TerrainBrushMode::Smooth, TerrainBrushMode::Flatten,
        TerrainBrushMode::Noise, TerrainBrushMode::Ramp, TerrainBrushMode::Terrace, TerrainBrushMode::Pinch,
        TerrainBrushMode::Bulge, TerrainBrushMode::StampAbsolute, TerrainBrushMode::StampMax, TerrainBrushMode::StampMin,
        TerrainBrushMode::Clone, TerrainBrushMode::Smudge, TerrainBrushMode::ErodeThermal, TerrainBrushMode::ErodeHydraulic};
    for (const TerrainBrushMode mode : modes) {
        INFO("mode " << static_cast<int>(mode));
        auto run = [&](TerrainHeightmap& map, StrokeRecord* record) {
            StrokeBrush brush;
            brush.mode = mode;
            brush.radiusM = 24.0f;
            brush.strength = mode == TerrainBrushMode::Smooth || mode == TerrainBrushMode::Flatten ? 0.7f : 1.5f;
            brush.seed = 9;
            brush.offsetX = 30.0f;
            brush.symmetry = Symmetry::MirrorBoth;
            generateProceduralStamp("fractured", 64, 1, brush.stamp);
            TerrainStroke stroke = make(map, brush);
            stroke.addPoint({-30.0, -20.0, 1.0f});
            stroke.addPoint({25.0, 15.0, 1.0f});
            stroke.addPoint({30.0, 40.0, 1.0f});
            if (record) *record = stroke.finish();
        };
        TerrainHeightmap map = rolling();
        const std::vector<f32> original = map.heights();
        StrokeRecord record;
        run(map, &record);
        const std::vector<f32> edited = map.heights();
        for (const f32 h : edited) { REQUIRE(h >= 0.0f); REQUIRE(h <= 1.0f); REQUIRE(std::isfinite(h)); }
        REQUIRE(edited != original);
        REQUIRE_FALSE(record.empty());
        record.applyBefore(map);
        REQUIRE(map.heights() == original);  // bit for bit, for every tile the mode reached
        record.applyAfter(map);
        REQUIRE(map.heights() == edited);
        TerrainHeightmap again = rolling();
        run(again, nullptr);
        REQUIRE(again.heights() == edited);
    }
}

TEST_CASE("Brush modes - the stamps are laid by metre whatever the path's sampling, for the modes that depend only on position", "[terrain][brush]") {
    const TerrainBrushMode modes[] = {TerrainBrushMode::Raise, TerrainBrushMode::Terrace, TerrainBrushMode::Pinch,
                                      TerrainBrushMode::StampMax, TerrainBrushMode::Clone, TerrainBrushMode::ErodeThermal};
    for (const TerrainBrushMode mode : modes) {
        INFO("mode " << static_cast<int>(mode));
        auto run = [&](int samples) {
            TerrainHeightmap map = rolling();
            StrokeBrush brush;
            brush.mode = mode;
            brush.radiusM = 16.0f;
            brush.strength = mode == TerrainBrushMode::Raise ? 1.0f : 0.8f;
            brush.offsetX = 20.0f;
            TerrainStroke stroke = make(map, brush);
            for (int i = 0; i <= samples; ++i) {
                const f64 t = static_cast<f64>(i) / samples;
                stroke.addPoint({-60.0 + 120.0 * t, 10.0 - 20.0 * t, 1.0f});
            }
            return map.heights();
        };
        const auto few = run(2);
        const auto many = run(90);
        f32 worst = 0.0f;
        for (size_t i = 0; i < few.size(); ++i) worst = std::max(worst, std::fabs(few[i] - many[i]));
        REQUIRE(worst < 1e-3f);
    }
}

// ------------------------------------------------------------------------------------------------- .brush documents

#include "assets/TextureMapGenerator.hpp"
#include "terrain/edit/BrushPreset.hpp"

#include <filesystem>

TEST_CASE("Brush files - a preset writes and reads back as it was", "[terrain][brush][file]") {
    BrushPreset preset;
    preset.name = "My rock brush";
    preset.category = "Rock";
    preset.source = StampSource::Procedural;
    preset.stamp = "strata";
    preset.stampSize = 96;
    StrokeBrush& b = preset.brush;
    b.mode = TerrainBrushMode::Terrace;
    b.radiusM = 14.5f;
    b.strength = 1.25f;
    b.spacingM = 3.0f;
    b.falloff = FalloffCurve::Sharp;
    b.rotationDegrees = 33.0f;
    b.targetHeightM = 12.5f;
    b.seed = 987654321ull;
    b.signedStamp = true;
    b.stepM = 1.5f;
    b.talusDegrees = 28.0f;
    b.iterations = 7;
    b.droplets = 50;
    b.offsetX = -4.0f;
    b.offsetZ = 9.0f;
    b.positionJitter = 0.3f;
    b.rotationJitterDegrees = 25.0f;
    b.sizeJitter = 0.2f;
    b.opacityJitter = 0.1f;
    b.symmetry = Symmetry::Radial;
    b.radialCount = 6;
    b.symmetryCenterX = 10.0f;
    b.symmetryCenterZ = -20.0f;
    b.pressureToStrength = 0.75f;
    BiomeTerm slope;
    slope.mask = "slope";
    slope.rise0 = 0.0f; slope.rise1 = 0.0f; slope.fall1 = 0.2f; slope.fall0 = 0.35f;
    BiomeTerm wet;
    wet.mask = "wetness";
    wet.rise0 = 0.1f; wet.rise1 = 0.3f; wet.fall1 = 0.9f; wet.fall0 = 1.0f;
    b.protect = {slope, wet};

    const std::string text = brushToText(preset);
    REQUIRE(text.find("# caffeine brush 1") == 0);
    BrushPreset back;
    std::string error;
    REQUIRE(brushFromText(text, back, &error));
    REQUIRE(error.empty());
    REQUIRE(back.name == preset.name);
    REQUIRE(back.category == "Rock");
    REQUIRE(back.source == StampSource::Procedural);
    REQUIRE(back.stamp == "strata");
    REQUIRE(back.stampSize == 96);
    const StrokeBrush& r = back.brush;
    REQUIRE(r.mode == TerrainBrushMode::Terrace);
    REQUIRE(r.radiusM == b.radiusM);
    REQUIRE(r.strength == b.strength);
    REQUIRE(r.spacingM == b.spacingM);
    REQUIRE(r.falloff == FalloffCurve::Sharp);
    REQUIRE(r.rotationDegrees == b.rotationDegrees);
    REQUIRE(r.targetHeightM == b.targetHeightM);
    REQUIRE(r.seed == b.seed);
    REQUIRE(r.signedStamp);
    REQUIRE(r.stepM == b.stepM);
    REQUIRE(r.talusDegrees == b.talusDegrees);
    REQUIRE(r.iterations == 7);
    REQUIRE(r.droplets == 50);
    REQUIRE(r.offsetX == b.offsetX);
    REQUIRE(r.offsetZ == b.offsetZ);
    REQUIRE(r.positionJitter == b.positionJitter);
    REQUIRE(r.rotationJitterDegrees == b.rotationJitterDegrees);
    REQUIRE(r.sizeJitter == b.sizeJitter);
    REQUIRE(r.opacityJitter == b.opacityJitter);
    REQUIRE(r.symmetry == Symmetry::Radial);
    REQUIRE(r.radialCount == 6);
    REQUIRE(r.symmetryCenterX == b.symmetryCenterX);
    REQUIRE(r.symmetryCenterZ == b.symmetryCenterZ);
    REQUIRE(r.pressureToStrength == b.pressureToStrength);
    REQUIRE(r.protect.size() == 2);
    REQUIRE(r.protect[0].mask == "slope");
    REQUIRE(r.protect[1].mask == "wetness");
    REQUIRE(r.protect[1].rise1 == wet.rise1);
    REQUIRE(brushToText(back) == text);  // stable
}

TEST_CASE("Brush files - comments, unknown keys, defaults and errors", "[terrain][brush][file]") {
    BrushPreset p;
    std::string error;
    REQUIRE(brushFromText("# a comment\n\nname  Hello world  # trailing\nmode lower\nfuture_key 1 2 3\nradius 9\n", p, &error));
    REQUIRE(p.name == "Hello world");
    REQUIRE(p.brush.mode == TerrainBrushMode::Lower);
    REQUIRE(p.brush.radiusM == 9.0f);
    REQUIRE(p.source == StampSource::None);  // a key a newer version knows is skipped, the rest still reads

    REQUIRE_FALSE(brushFromText("radius -3\n", p, &error));
    REQUIRE(error.find("line 1") == 0);
    REQUIRE_FALSE(brushFromText("name x\nmode melt\n", p, &error));
    REQUIRE(error.find("line 2") == 0);
    REQUIRE(error.find("melt") != std::string::npos);
    REQUIRE_FALSE(brushFromText("falloff bumpy\n", p, &error));
    REQUIRE_FALSE(brushFromText("stamp teleport x\n", p, &error));
    REQUIRE_FALSE(brushFromText("protect slope 1 2\n", p, &error));
    REQUIRE_FALSE(brushFromText("jitter 1 2\n", p, &error));
    REQUIRE(brushFromText("", p, &error));  // an empty file is the default brush
    REQUIRE(p.brush.mode == TerrainBrushMode::Raise);

    // A stamp path may hold spaces; the size is the last word when it is a number.
    REQUIRE(brushFromText("stamp image Textures/my crack.png 256\n", p, &error));
    REQUIRE(p.source == StampSource::Image);
    REQUIRE(p.stamp == "Textures/my crack.png");
    REQUIRE(p.stampSize == 256);
    REQUIRE(brushFromText("stamp image Textures/crack.png\n", p, &error));
    REQUIRE(p.stampSize == 128);
}

TEST_CASE("Brush files - a stamp is generated, or read from a picture, and the stroke uses it", "[terrain][brush][file]") {
    BrushPreset p;
    p.source = StampSource::Procedural;
    p.stamp = "cone";
    p.stampSize = 32;
    std::string error;
    REQUIRE(resolveBrushStamp(p, nullptr, &error));
    REQUIRE(p.brush.stamp.size == 32);
    p.stamp = "no_such";
    REQUIRE_FALSE(resolveBrushStamp(p, nullptr, &error));
    REQUIRE(error.find("no_such") != std::string::npos);
    REQUIRE(p.brush.stamp.empty());

    // A picture: a white square on black, found through the path function (the engine passes the asset manager's).
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "caffeine-brush-stamp";
    std::error_code ec;
    fs::create_directories(dir, ec);
    Assets::TextureImage image;
    image.width = 16;
    image.height = 16;
    image.valid = true;
    image.rgba.assign(16 * 16 * 4, 0);
    for (u32 z = 0; z < 16; ++z) {
        for (u32 x = 0; x < 16; ++x) {
            const bool white = x >= 4 && x < 12 && z >= 4 && z < 12;
            for (u32 c = 0; c < 4; ++c) image.rgba[(z * 16 + x) * 4 + c] = (c == 3 || white) ? 255 : 0;
        }
    }
    REQUIRE(Assets::saveTextureImage(dir / "square.png", image));
    BrushPreset picture;
    picture.source = StampSource::Image;
    picture.stamp = "Textures/square.png";
    picture.stampSize = 24;
    REQUIRE(resolveBrushStamp(picture, [&](const std::string& path) { return (dir / fs::path(path).filename()).string(); }, &error));
    REQUIRE(picture.brush.stamp.size == 24);
    REQUIRE(picture.brush.stamp.sample(0.0f, 0.0f) > 0.9f);   // the white middle
    REQUIRE(picture.brush.stamp.sample(-0.95f, 0.0f) < 0.1f);  // the black border
    REQUIRE_FALSE(resolveBrushStamp(picture, [](const std::string&) { return std::string("/no/such/file.png"); }, &error));
    REQUIRE(error.find("cannot read") != std::string::npos);
    fs::remove_all(dir, ec);
}

TEST_CASE("Brush library - the built-in brushes cover every stamp and every tool, and each one works", "[terrain][brush]") {
    const auto& brushes = builtinBrushes();
    REQUIRE(brushes.size() >= 56);  // 42 stamps and 16 tools
    std::set<std::string> names;
    std::set<TerrainBrushMode> modes;
    for (const BrushPreset& preset : brushes) {
        REQUIRE(names.insert(preset.name).second);
        modes.insert(preset.brush.mode);
        BrushPreset copy = preset;
        std::string error;
        REQUIRE(resolveBrushStamp(copy, nullptr, &error));
        // It writes, reads back, and makes a stroke that changes the ground.
        BrushPreset back;
        REQUIRE(brushFromText(brushToText(preset), back, &error));
        REQUIRE(back.name == preset.name);
        TerrainHeightmap map = rolling();
        const std::vector<f32> before = map.heights();
        TerrainStroke stroke = make(map, copy.brush);
        stroke.addPoint({-20.0, 0.0, 1.0f});
        stroke.addPoint({20.0, 0.0, 1.0f});
        INFO(preset.name);
        REQUIRE(map.heights() != before);
    }
    REQUIRE(modes.size() == 16);  // every mode has a brush
    for (const char* name : {"Hydraulic erosion", "Round soft", "Crater", "River bed", "Terrace"}) REQUIRE(names.count(name) == 1);
}
