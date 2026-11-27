#include "catch.hpp"
#include "terrain/paint/TerrainMaskPaint.hpp"

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

MaskPaintBrush brush(MaskPaintMode mode, f32 radius = 10.0f, f32 strength = 1.0f, FalloffCurve falloff = FalloffCurve::Constant) {
    MaskPaintBrush b;
    b.shape.radiusM = radius;
    b.shape.strength = strength;
    b.shape.falloff = falloff;
    b.mode = mode;
    return b;
}

u8 at(const TerrainMasks& masks, f32 x, f32 z) {
    const TerrainMasks::Stored* stored = masks.findStored("wet");
    const u32 tx = static_cast<u32>(std::lround(x / kSize * static_cast<f32>(stored->resolutionX - 1)));
    const u32 tz = static_cast<u32>(std::lround(z / kSize * static_cast<f32>(stored->resolutionZ - 1)));
    return stored->data[static_cast<size_t>(tz) * stored->resolutionX + tx];
}

}  // namespace

TEST_CASE("A mask is made empty, filled with a level or copied from another mask, and undoes", "[terrain][mask][paint]") {
    TerrainHeightmap ground = flatGround();
    const TerrainFrame frame{&ground, kSize, kSize, 80.0f};
    TerrainMasks masks;
    MaskRecord record;
    std::string error;

    REQUIRE(createMask(masks, frame, "wet", 65, 65, 0.0f, "", record, &error));
    REQUIRE(masks.has("wet"));
    CHECK(masks.resolutionX("wet") == 65);
    CHECK(at(masks, 100.0f, 100.0f) == 0);
    CHECK(record.whole);
    CHECK(record.applyBefore(masks));
    CHECK_FALSE(masks.has("wet"));
    CHECK(record.applyAfter(masks));
    CHECK(masks.has("wet"));

    // Names: taken, built-in, invalid, and the 17th mask.
    CHECK_FALSE(createMask(masks, frame, "wet", 65, 65, 0.0f, "", record, &error));
    CHECK(error.find("exists") != std::string::npos);
    CHECK_FALSE(createMask(masks, frame, "slope", 65, 65, 0.0f, "", record, &error));
    CHECK_FALSE(createMask(masks, frame, "no spaces", 65, 65, 0.0f, "", record, &error));
    CHECK_FALSE(createMask(masks, frame, "tiny", 1, 65, 0.0f, "", record, &error));
    CHECK_FALSE(createMask(masks, frame, "copy", 33, 33, 0.0f, "missing", record, &error));
    for (u32 i = 1; i < TerrainMasks::kMaxStoredMasks; ++i) {
        REQUIRE(createMask(masks, frame, "m" + std::to_string(i), 8, 8, 1.0f, "", record, &error));
    }
    CHECK_FALSE(createMask(masks, frame, "one-too-many", 8, 8, 0.0f, "", record, &error));
    CHECK(error.find("16") != std::string::npos);

    // A copy of a built-in mask: flat ground has a slope of 0 and a curvature of one half.
    REQUIRE(masks.remove("m1"));
    REQUIRE(createMask(masks, frame, "flat", 33, 33, 0.0f, "curvature", record, &error));
    CHECK(masks.findStored("flat")->data[40] == Approx(128).margin(1));
}

TEST_CASE("Paint, Erase and Smooth move a mask under the brush and a stroke undoes and redoes exactly", "[terrain][mask][paint]") {
    TerrainHeightmap ground = flatGround();
    const TerrainFrame frame{&ground, kSize, kSize, 80.0f};
    const ECS::TerrainComponent terrain = settings();
    TerrainMasks masks;
    MaskRecord made;
    REQUIRE(createMask(masks, frame, "wet", kRes, kRes, 0.0f, "", made, nullptr));

    // Paint toward 0.8 at the centre (the terrain's centre is x = z = 0 in stroke coordinates).
    MaskPaintBrush paint = brush(MaskPaintMode::Paint);
    paint.value = 0.8f;
    TerrainMaskStroke stroke(masks, "wet", ground, terrain, paint);
    CHECK(stroke.valid());
    CHECK(stroke.addPoint({0.0, 0.0, 1.0f}) == 1);
    CHECK(stroke.addPoint({20.0, 0.0, 1.0f}) > 1);
    CHECK(stroke.dirty().valid);
    const std::vector<u8> before = masks.findStored("wet")->data;
    MaskRecord record = stroke.finish();
    CHECK(record.mask == "wet");
    CHECK_FALSE(record.empty());
    CHECK(record.region.valid);
    CHECK(at(masks, 128.0f, 128.0f) == Approx(204).margin(1));  // 0.8 * 255
    CHECK(at(masks, 128.0f + 20.0f, 128.0f) == Approx(204).margin(1));
    CHECK(at(masks, 10.0f, 10.0f) == 0);

    const std::vector<u8> painted = masks.findStored("wet")->data;
    CHECK(record.applyBefore(masks));
    CHECK(masks.findStored("wet")->data != painted);
    CHECK(at(masks, 128.0f, 128.0f) == 0);
    CHECK(record.applyAfter(masks));
    CHECK(masks.findStored("wet")->data == painted);
    (void)before;

    // Erase takes it back.
    TerrainMaskStroke erase(masks, "wet", ground, terrain, brush(MaskPaintMode::Erase, 40.0f));
    erase.addPoint({0.0, 0.0, 1.0f});
    CHECK(at(masks, 128.0f, 128.0f) == 0);

    // Smooth blends a hard edge: a paint stamp with a hard edge, then smoothing it makes values in between.
    TerrainMaskStroke hard(masks, "wet", ground, terrain, brush(MaskPaintMode::Paint, 20.0f));
    hard.addPoint({0.0, 0.0, 1.0f});
    CHECK(at(masks, 128.0f + 22.0f, 128.0f) == 0);
    const u8 edgeBefore = at(masks, 128.0f + 18.0f, 128.0f);
    CHECK(edgeBefore == 255);
    TerrainMaskStroke smooth(masks, "wet", ground, terrain, brush(MaskPaintMode::Smooth, 30.0f));
    smooth.addPoint({18.0, 0.0, 1.0f});
    const u8 edgeAfter = at(masks, 128.0f + 20.0f, 128.0f);
    CHECK(edgeAfter > 0);
    CHECK(edgeAfter < 255);
}

TEST_CASE("A soft brush of low strength keeps its falloff in 8 bits and a stroke does not depend on the sampling", "[terrain][mask][paint]") {
    TerrainHeightmap ground = flatGround();
    const TerrainFrame frame{&ground, kSize, kSize, 80.0f};
    const ECS::TerrainComponent terrain = settings();
    MaskRecord made;

    auto run = [&](u32 samples) {
        TerrainMasks masks;
        REQUIRE(createMask(masks, frame, "wet", kRes, kRes, 0.0f, "", made, nullptr));
        MaskPaintBrush b = brush(MaskPaintMode::Paint, 16.0f, 0.1f, FalloffCurve::Smooth);
        TerrainMaskStroke stroke(masks, "wet", ground, terrain, b);
        for (u32 i = 0; i <= samples; ++i) {
            stroke.addPoint({-30.0 + 60.0 * static_cast<f64>(i) / samples, 0.0, 1.0f});
        }
        return masks.findStored("wet")->data;
    };
    const std::vector<u8> coarse = run(4);
    const std::vector<u8> fine = run(240);
    CHECK(coarse == fine);

    // The middle of the line is stronger than its fringe, and the fringe is not a single flat step.
    TerrainMasks masks;
    REQUIRE(createMask(masks, frame, "wet", kRes, kRes, 0.0f, "", made, nullptr));
    TerrainMaskStroke stroke(masks, "wet", ground, terrain, brush(MaskPaintMode::Paint, 16.0f, 0.1f, FalloffCurve::Smooth));
    stroke.addPoint({0.0, 0.0, 1.0f});
    const u8 centre = at(masks, 128.0f, 128.0f), middle = at(masks, 128.0f + 8.0f, 128.0f), rim = at(masks, 128.0f + 14.0f, 128.0f);
    CHECK(centre > middle);
    CHECK(middle > rim);
    CHECK(rim > 0);
}

TEST_CASE("Protection masks, symmetry and an empty stroke behave like the other brushes", "[terrain][mask][paint]") {
    TerrainHeightmap ground = flatGround();
    const TerrainFrame frame{&ground, kSize, kSize, 80.0f};
    const ECS::TerrainComponent terrain = settings();
    TerrainMasks masks;
    MaskRecord made;
    REQUIRE(createMask(masks, frame, "wet", kRes, kRes, 0.0f, "", made, nullptr));

    // Mirror about the centre: one stamp at x = +40 also paints x = -40.
    MaskPaintBrush mirrored = brush(MaskPaintMode::Paint, 8.0f);
    mirrored.shape.symmetry = Symmetry::MirrorX;
    TerrainMaskStroke stroke(masks, "wet", ground, terrain, mirrored);
    stroke.addPoint({40.0, 0.0, 1.0f});
    CHECK(at(masks, 128.0f + 40.0f, 128.0f) == 255);
    CHECK(at(masks, 128.0f - 40.0f, 128.0f) == 255);
    CHECK(at(masks, 128.0f, 128.0f) == 0);

    // Protected by a mask that is zero everywhere: nothing is painted, and the record is empty.
    MaskRecord other;
    REQUIRE(createMask(masks, frame, "none", 33, 33, 0.0f, "", other, nullptr));
    MaskPaintBrush guarded = brush(MaskPaintMode::Paint, 8.0f);
    guarded.shape.protect.push_back(BiomeTerm{"none", 0.5f, 0.6f, 1.0f, 1.0f});
    TerrainMaskStroke blocked(masks, "wet", ground, terrain, guarded);
    blocked.addPoint({0.0, 0.0, 1.0f});
    CHECK(blocked.finish().empty());
    CHECK(at(masks, 128.0f, 128.0f) == 0);

    // Fill, invert and delete are one undo step each; a no-op leaves no record.
    MaskRecord record;
    std::string error;
    REQUIRE(editMask(masks, "none", MaskOp::Fill, 0.0f, record, &error));
    CHECK(record.empty());
    REQUIRE(editMask(masks, "none", MaskOp::Invert, 0.0f, record, &error));
    CHECK(masks.findStored("none")->data[5] == 255);
    CHECK(record.applyBefore(masks));
    CHECK(masks.findStored("none")->data[5] == 0);
    REQUIRE(editMask(masks, "none", MaskOp::Delete, 0.0f, record, &error));
    CHECK_FALSE(masks.has("none"));
    CHECK(record.applyBefore(masks));
    CHECK(masks.has("none"));
    CHECK_FALSE(editMask(masks, "slope", MaskOp::Fill, 1.0f, record, &error));
    CHECK_FALSE(editMask(masks, "missing", MaskOp::Fill, 1.0f, record, &error));
}
