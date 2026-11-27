#include "catch.hpp"
#include "terrain/spline/TerrainSpline.hpp"

#include <cmath>

using namespace Caffeine;
using namespace Caffeine::Terrain;

namespace {

constexpr u32 kRes = 129;
constexpr f32 kSize = 256.0f;
constexpr f32 kMax = 80.0f;

ECS::TerrainComponent settings() {
    ECS::TerrainComponent t;
    t.resolutionX = kRes;
    t.resolutionZ = kRes;
    t.worldSizeX = kSize;
    t.worldSizeZ = kSize;
    t.maxHeight = kMax;
    return t;
}

/// Rolling hills: 20 m of sine bumps along x, a little across z.
TerrainHeightmap hills() {
    TerrainHeightmap map(kRes, kRes);
    for (u32 z = 0; z < kRes; ++z) {
        for (u32 x = 0; x < kRes; ++x) {
            const f32 metresX = static_cast<f32>(x) * kSize / (kRes - 1);
            const f32 metresZ = static_cast<f32>(z) * kSize / (kRes - 1);
            const f32 h = 30.0f + 10.0f * std::sin(metresX * 0.08f) + 2.0f * std::sin(metresZ * 0.05f);
            map.heights()[static_cast<size_t>(z) * kRes + x] = h / kMax;
        }
    }
    return map;
}

/// A ground that falls 20 m from x = 0 to x = 256, with a 6 m bump in the middle.
TerrainHeightmap slope() {
    TerrainHeightmap map(kRes, kRes);
    for (u32 z = 0; z < kRes; ++z) {
        for (u32 x = 0; x < kRes; ++x) {
            const f32 metresX = static_cast<f32>(x) * kSize / (kRes - 1);
            const f32 bump = 6.0f * std::exp(-std::pow((metresX - 128.0f) / 15.0f, 2.0f));
            map.heights()[static_cast<size_t>(z) * kRes + x] = (40.0f - metresX * 20.0f / kSize + bump) / kMax;
        }
    }
    return map;
}

f32 metres(const TerrainHeightmap& map, f32 x, f32 z) { return map.sampleBilinear(x / kSize, z / kSize) * kMax; }

SplineDesc straight(SplineKind kind) {
    SplineDesc desc;
    desc.kind = kind;
    desc.points = {{20.0f, 128.0f, 0.0f}, {80.0f, 128.0f, 0.0f}, {160.0f, 128.0f, 0.0f}, {236.0f, 128.0f, 0.0f}};
    desc.widthM = 6.0f;
    desc.shoulderM = 6.0f;
    return desc;
}

}  // namespace

TEST_CASE("A road is levelled and graded along its path and the ground away from it is untouched", "[terrain][spline]") {
    TerrainHeightmap ground = hills();
    const TerrainHeightmap original = ground;
    const ECS::TerrainComponent terrain = settings();
    SplineDesc desc = straight(SplineKind::Road);
    desc.smoothM = 60.0f;
    desc.maxGrade = 0.04f;

    SplineRecord record;
    std::string error;
    REQUIRE(applySpline(ground, nullptr, nullptr, terrain, desc, record, &error));
    CHECK(error.empty());
    CHECK_FALSE(record.empty());
    CHECK(record.lengthM == Approx(216.0f).margin(3.0f));

    // The centreline: a gentle profile, nowhere steeper than the grade, and flatter than the hills it crosses.
    f32 worstBefore = 0.0f, worstAfter = 0.0f, lastBefore = metres(original, 20.0f, 128.0f), lastAfter = metres(ground, 20.0f, 128.0f);
    for (f32 x = 22.0f; x <= 234.0f; x += 2.0f) {
        const f32 b = metres(original, x, 128.0f), a = metres(ground, x, 128.0f);
        worstBefore = std::max(worstBefore, std::fabs(b - lastBefore) / 2.0f);
        worstAfter = std::max(worstAfter, std::fabs(a - lastAfter) / 2.0f);
        lastBefore = b;
        lastAfter = a;
    }
    CHECK(worstBefore > 0.3f);
    CHECK(worstAfter < 0.07f);

    // Across the road the surface is level inside the corridor.
    const f32 centre = metres(ground, 120.0f, 128.0f);
    CHECK(metres(ground, 120.0f, 130.0f) == Approx(centre).margin(0.15f));
    CHECK(metres(ground, 120.0f, 126.0f) == Approx(centre).margin(0.15f));
    // Away from it, no vertex moved.
    for (u32 x = 0; x < kRes; ++x) {
        CHECK(ground.heights()[static_cast<size_t>(10) * kRes + x] == original.heights()[static_cast<size_t>(10) * kRes + x]);
    }
    // Same description, same road.
    TerrainHeightmap again = hills();
    SplineRecord second;
    REQUIRE(applySpline(again, nullptr, nullptr, terrain, desc, second, &error));
    CHECK(again.heights() == ground.heights());
}

TEST_CASE("A road paints its layer and writes its mask, and one undo step puts everything back", "[terrain][spline]") {
    TerrainHeightmap ground = hills();
    const TerrainHeightmap originalGround = ground;
    const ECS::TerrainComponent terrain = settings();
    TerrainLayerMap layers(kRes, kRes);
    const TerrainLayerMap originalLayers = layers;
    TerrainMasks masks;

    SplineDesc desc = straight(SplineKind::Road);
    desc.layer = 2;
    desc.mask = "road";
    SplineRecord record;
    std::string error;
    REQUIRE(applySpline(ground, &layers, &masks, terrain, desc, record, &error));
    CHECK(record.madeMask);
    REQUIRE(masks.has("road"));
    const TerrainMasks::Stored* road = masks.findStored("road");
    auto maskAt = [&](f32 x, f32 z) {
        const u32 tx = static_cast<u32>(std::lround(x / kSize * (road->resolutionX - 1)));
        const u32 tz = static_cast<u32>(std::lround(z / kSize * (road->resolutionZ - 1)));
        return road->data[static_cast<size_t>(tz) * road->resolutionX + tx];
    };
    CHECK(maskAt(120.0f, 128.0f) == 255);
    CHECK(maskAt(120.0f, 20.0f) == 0);
    const u8 shoulder = maskAt(120.0f, 128.0f + 5.0f);  // 3 m of half width, then the shoulder
    CHECK(shoulder > 0);
    CHECK(shoulder < 255);
    // The layer is under the road and not far from it.
    CHECK(layers.weightOf(60, 64, 2) > 0.95f);
    CHECK(layers.weightOf(60, 64, 0) < 0.05f);
    CHECK(layers.weightOf(60, 10, 0) > 0.99f);
    CHECK(layers.at(60, 10) == originalLayers.at(60, 10));

    const std::vector<f32> paved = ground.heights();
    const std::vector<LayerTexel> pavedLayer = [&] {
        std::vector<LayerTexel> all;
        for (u32 z = 0; z < kRes; ++z) for (u32 x = 0; x < kRes; ++x) all.push_back(layers.at(x, z));
        return all;
    }();
    SplineRegions undone = revertSpline(record, ground, &layers, &masks);
    CHECK(undone.height.valid);
    CHECK(ground.heights() == originalGround.heights());
    CHECK_FALSE(masks.has("road"));
    for (u32 z = 0; z < kRes; ++z) for (u32 x = 0; x < kRes; ++x) CHECK(layers.at(x, z) == originalLayers.at(x, z));

    reapplySpline(record, ground, &layers, &masks);
    CHECK(ground.heights() == paved);
    CHECK(masks.has("road"));
    for (u32 z = 0; z < kRes; ++z) for (u32 x = 0; x < kRes; ++x) CHECK(layers.at(x, z) == pavedLayer[static_cast<size_t>(z) * kRes + x]);

    // A second road over the same mask only adds to it, and its record holds just the changed blocks.
    SplineDesc cross;
    cross.points = {{120.0f, 30.0f, 0.0f}, {120.0f, 220.0f, 0.0f}};
    cross.mask = "road";
    SplineRecord crossRecord;
    REQUIRE(applySpline(ground, &layers, &masks, terrain, cross, crossRecord, &error));
    CHECK_FALSE(crossRecord.madeMask);
    CHECK(maskAt(120.0f, 60.0f) == 255);
    CHECK(maskAt(120.0f, 128.0f) == 255);
    revertSpline(crossRecord, ground, &layers, &masks);
    CHECK(masks.has("road"));
    CHECK(maskAt(120.0f, 60.0f) == 0);
    CHECK(maskAt(120.0f, 128.0f) == 255);
}

TEST_CASE("A river cuts a channel whose bed never rises downstream", "[terrain][spline]") {
    TerrainHeightmap ground = slope();
    const TerrainHeightmap original = ground;
    const ECS::TerrainComponent terrain = settings();
    SplineDesc desc = straight(SplineKind::River);
    desc.widthM = 8.0f;
    desc.depthM = 2.0f;
    desc.smoothM = 20.0f;
    desc.mask = "river";
    TerrainMasks masks;
    SplineRecord record;
    std::string error;
    REQUIRE(applySpline(ground, nullptr, &masks, terrain, desc, record, &error));

    // The bed, source to mouth: never up.
    f32 last = metres(ground, 22.0f, 128.0f);
    for (f32 x = 24.0f; x <= 232.0f; x += 2.0f) {
        const f32 bed = metres(ground, x, 128.0f);
        CHECK(bed <= last + 0.05f);
        last = bed;
    }
    // The bump in the middle was cut through: the bed is below the bump, which stays beside the river.
    CHECK(metres(ground, 128.0f, 128.0f) < metres(original, 128.0f, 128.0f) - 4.0f);
    CHECK(metres(ground, 128.0f, 128.0f + 30.0f) == Approx(metres(original, 128.0f, 128.0f + 30.0f)).margin(0.01f));
    // A rounded bed: deepest in the middle, level with the bank at the edge of the channel.
    const f32 middle = metres(ground, 60.0f, 128.0f), side = metres(ground, 60.0f, 128.0f + 2.5f), bank = metres(ground, 60.0f, 128.0f + 4.0f);
    CHECK(middle < side);
    CHECK(side < bank);
    CHECK(masks.has("river"));
}

TEST_CASE("A spline that cannot be applied says why and changes nothing", "[terrain][spline]") {
    TerrainHeightmap ground = hills();
    const TerrainHeightmap original = ground;
    const ECS::TerrainComponent terrain = settings();
    TerrainMasks masks;
    SplineRecord record;
    std::string error;

    SplineDesc one = straight(SplineKind::Road);
    one.points.resize(1);
    CHECK_FALSE(applySpline(ground, nullptr, nullptr, terrain, one, record, &error));
    CHECK(error.find("two points") != std::string::npos);

    SplineDesc off = straight(SplineKind::Road);
    for (SplinePoint& p : off.points) p.x += 1000.0f;
    CHECK_FALSE(applySpline(ground, nullptr, nullptr, terrain, off, record, &error));
    CHECK(error.find("on the terrain") != std::string::npos);

    SplineDesc wide = straight(SplineKind::Road);
    wide.widthM = 0.0f;
    CHECK_FALSE(applySpline(ground, nullptr, nullptr, terrain, wide, record, &error));

    SplineDesc layer = straight(SplineKind::Road);
    layer.layer = 1;
    CHECK_FALSE(applySpline(ground, nullptr, nullptr, terrain, layer, record, &error));
    CHECK(error.find("layered painting") != std::string::npos);

    SplineDesc builtin = straight(SplineKind::Road);
    builtin.mask = "slope";
    CHECK_FALSE(applySpline(ground, nullptr, &masks, terrain, builtin, record, &error));
    CHECK(error.find("built-in") != std::string::npos);

    CHECK(ground.heights() == original.heights());
    CHECK(record.empty());
}
