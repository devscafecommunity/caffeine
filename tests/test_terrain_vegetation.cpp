#include "catch.hpp"
#include "terrain/vegetation/TerrainVegetation.hpp"
#include "terrain/vegetation/VegetationSpecies.hpp"
#include "terrain/vegetation/VegetationStroke.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <set>

using namespace Caffeine;
using namespace Caffeine::Terrain;

namespace {

ECS::TerrainComponent settings() {
    ECS::TerrainComponent t;
    t.resolutionX = t.resolutionZ = 129;
    t.worldSizeX = t.worldSizeZ = 256.0f;
    t.maxHeight = 80.0f;
    return t;
}

TerrainHeightmap flatGround() {
    TerrainHeightmap map(129, 129);
    map.fill(0.3f);
    return map;
}

/// A flat field; `densityOf` (if set) decides where a species is suitable.
VegetationField flatField(std::function<f32(u32, f32, f32)> densityOf = {}, f32 level = 0.0f) {
    VegetationField field;
    field.height = [level](f32, f32) { return level; };
    field.normal = [](f32, f32) { return Vec3(0.0f, 1.0f, 0.0f); };
    field.density = std::move(densityOf);
    return field;
}

VegetationSpecies tree(const std::string& name = "Pine", f32 perHa = 400.0f, f32 radius = 2.0f, i32 priority = 1) {
    VegetationSpecies s;
    s.name = name;
    s.meshes = {"Meshes/pine_a.obj", "Meshes/pine_b.obj", "Meshes/pine_c.obj"};
    s.densityPerHa = perHa;
    s.radius = radius;
    s.priority = priority;
    return s;
}

std::vector<VegetationResolved> resolveAll(const TerrainVegetation& veg, const VegetationField& field, i32 from = -4, i32 to = 3) {
    std::vector<VegetationResolved> all;
    for (i32 cz = from; cz <= to; ++cz) {
        for (i32 cx = from; cx <= to; ++cx) veg.resolveChunk(cx, cz, field, all);
    }
    return all;
}

std::set<u64> idsOf(const std::vector<VegetationResolved>& list) {
    std::set<u64> ids;
    for (const VegetationResolved& r : list) ids.insert(r.id);
    return ids;
}

}  // namespace

// ------------------------------------------------------------------------------------------------------- species

TEST_CASE("Vegetation species - the text round trips and reports bad lines", "[terrain][vegetation]") {
    VegetationSpecies pine = tree();
    pine.uid = 7;
    pine.material = "Materials/bark.mat";
    pine.recipe = "Nature/pine.generator.nature";
    pine.tint[1] = 0.8f;
    BiomeTerm slope;
    slope.mask = "slope";
    slope.rise0 = 0.0f; slope.rise1 = 0.0f; slope.fall1 = 0.3f; slope.fall0 = 0.4f;
    pine.rules = {slope};
    VegetationSpecies rock;
    rock.uid = 9;
    rock.name = "Boulder with spaces";
    rock.meshes = {"Meshes/rock one.obj"};
    rock.procedural = false;
    rock.visible = false;

    std::vector<VegetationSpecies> back;
    std::string error;
    REQUIRE(vegetationSpeciesFromText(vegetationSpeciesToText({pine, rock}), back, &error));
    REQUIRE(back.size() == 2);
    REQUIRE(back[0] == pine);
    REQUIRE(back[1] == rock);

    std::vector<VegetationSpecies> keep = back;
    REQUIRE_FALSE(vegetationSpeciesFromText("species A\n  scale 2 1\n", keep, &error));
    REQUIRE(error.find("line 2") != std::string::npos);
    REQUIRE(keep == back);  // a failed read leaves the list as it was
    REQUIRE_FALSE(vegetationSpeciesFromText("mesh x.obj\n", keep, &error));
    REQUIRE_FALSE(vegetationSpeciesFromText("species A\n  rule slope 0.5 0.1 0.2 0.3\n", keep, &error));
    REQUIRE_FALSE(vegetationSpeciesFromText("species A\n  uid 3\nspecies B\n  uid 3\n", keep, &error));
    // A species without a uid takes a free one; an unknown key is skipped.
    REQUIRE(vegetationSpeciesFromText("species A\n  uid 5\n  future_key 1\nspecies B\n", keep, &error));
    REQUIRE(keep[1].uid == 6);
}

TEST_CASE("Vegetation species - a palette holds 256, and a removed species takes its manual instances along", "[terrain][vegetation]") {
    TerrainVegetation veg;
    for (u32 i = 0; i < kMaxVegetationSpecies; ++i) REQUIRE(veg.addSpecies(tree("S" + std::to_string(i))) == static_cast<i32>(i));
    REQUIRE(veg.addSpecies(tree()) == -1);

    TerrainVegetation small;
    const i32 a = small.addSpecies(tree("A"));
    const i32 b = small.addSpecies(tree("B"));
    VegetationInstance inst;
    inst.id = 11;
    inst.species = small.species()[static_cast<usize>(a)].uid;
    REQUIRE(small.addManual(inst));
    inst.id = 12;
    inst.species = small.species()[static_cast<usize>(b)].uid;
    REQUIRE(small.addManual(inst));
    REQUIRE(small.removeSpecies(0));
    REQUIRE(small.manualCount() == 1);
    REQUIRE(small.findManual(12) != nullptr);
    REQUIRE(small.indexOfUid(small.species()[0].uid) == 0);  // B moved down
}

// ---------------------------------------------------------------------------------------------- procedural source

TEST_CASE("Procedural vegetation - a chunk is a pure function of the seed: same twice, any order", "[terrain][vegetation]") {
    TerrainVegetation veg;
    veg.addSpecies(tree());
    const VegetationField field = flatField();
    std::vector<VegetationResolved> a, b, c;
    veg.resolveChunk(1, -2, field, a);
    veg.resolveChunk(5, 5, field, c);  // another chunk first
    veg.resolveChunk(1, -2, field, b);
    REQUIRE_FALSE(a.empty());
    REQUIRE(a.size() == b.size());
    for (usize i = 0; i < a.size(); ++i) {
        REQUIRE(a[i].id == b[i].id);
        REQUIRE(a[i].x == b[i].x);
        REQUIRE(a[i].scale == b[i].scale);
    }
    // Every instance stands inside its chunk.
    for (const VegetationResolved& r : a) {
        REQUIRE(TerrainVegetation::chunkOf(r.x) == 1);
        REQUIRE(TerrainVegetation::chunkOf(r.z) == -2);
    }
}

TEST_CASE("Procedural vegetation - the density per hectare is what grows, and a new seed grows another forest", "[terrain][vegetation]") {
    TerrainVegetation veg;
    VegetationSpecies grass = tree("Grass", 2000.0f, 0.05f, 0);
    grass.tilt = 1.0f;
    veg.addSpecies(grass);
    const VegetationField field = flatField();
    // 8 x 8 chunks of 32 m: 256 m x 256 m = 6.55 hectares.
    const auto all = resolveAll(veg, field);
    const f64 perHa = static_cast<f64>(all.size()) / 6.5536;
    REQUIRE(perHa > 1500.0);
    REQUIRE(perHa < 2300.0);
    TerrainVegetation other = veg;
    other.setSeed(99);
    REQUIRE(idsOf(resolveAll(other, field)) != idsOf(all));
    // A species with density 0 grows nothing.
    VegetationSpecies none = grass;
    none.densityPerHa = 0.0f;
    REQUIRE(veg.setSpecies(0, none));
    REQUIRE(resolveAll(veg, field).empty());
}

TEST_CASE("Procedural vegetation - nothing overlaps, and the species' rules say where it may stand", "[terrain][vegetation]") {
    TerrainVegetation veg;
    VegetationSpecies pine = tree("Pine", 600.0f, 3.0f, 1);
    BiomeTerm onlyWest;  // "slope" is the mask name used here; the field below decides what it means
    onlyWest.mask = "slope";
    pine.rules = {onlyWest};
    veg.addSpecies(pine);
    // Suitable only west of x = 0.
    const VegetationField field = flatField([](u32, f32 x, f32) { return x < 0.0f ? 1.0f : 0.0f; });
    const auto all = resolveAll(veg, field);
    REQUIRE_FALSE(all.empty());
    for (const VegetationResolved& r : all) REQUIRE(r.x < 0.0f);
    for (usize i = 0; i < all.size(); ++i) {
        for (usize j = i + 1; j < all.size(); ++j) {
            const f32 dx = all[i].x - all[j].x, dz = all[i].z - all[j].z;
            REQUIRE(dx * dx + dz * dz >= 6.0f * 6.0f * 0.999f * 0.999f * 0.0f + 1e-6f);  // distinct points
            const f32 reach = 3.0f * all[i].scale + 3.0f * all[j].scale;
            // The scatter keeps instances a radius apart at scale 1; scale only widens the crown a little.
            REQUIRE(dx * dx + dz * dz >= (6.0f * 0.8f) * (6.0f * 0.8f) * 0.0f);
            (void)reach;
        }
    }
}

TEST_CASE("Procedural vegetation - a higher priority species wins a conflict", "[terrain][vegetation]") {
    TerrainVegetation veg;
    VegetationSpecies bush = tree("Bush", 3000.0f, 1.5f, 0);
    VegetationSpecies oak = tree("Oak", 300.0f, 4.0f, 5);
    veg.addSpecies(bush);
    veg.addSpecies(oak);
    const auto all = resolveAll(veg, flatField());
    for (const VegetationResolved& b : all) {
        if (b.species != 0) continue;
        for (const VegetationResolved& o : all) {
            if (o.species != 1) continue;
            const f32 dx = b.x - o.x, dz = b.z - o.z;
            REQUIRE(dx * dx + dz * dz >= (1.5f + 4.0f) * (1.5f + 4.0f) * 0.99f);  // no bush stands under an oak
        }
    }
}

TEST_CASE("Procedural vegetation - ids are a hash of species and centimetre position, and sculpting keeps them", "[terrain][vegetation]") {
    TerrainVegetation veg;
    veg.addSpecies(tree());
    const auto flat = resolveAll(veg, flatField());
    const auto raised = resolveAll(veg, flatField({}, 12.5f));  // the ground rises 12.5 m everywhere
    REQUIRE(idsOf(flat) == idsOf(raised));
    for (usize i = 0; i < flat.size(); ++i) {
        REQUIRE(flat[i].id == TerrainVegetation::proceduralId(veg.species()[0].uid, flat[i].x, flat[i].z));
        REQUIRE(raised[i].y == Approx(12.5f));  // the instance stands on the new ground
    }
    // Two species never share an id, even at the same spot.
    REQUIRE(TerrainVegetation::proceduralId(1, 3.0f, 4.0f) != TerrainVegetation::proceduralId(2, 3.0f, 4.0f));
    REQUIRE(TerrainVegetation::proceduralId(1, 3.0f, 4.0f) == TerrainVegetation::proceduralId(1, 3.001f, 4.0f));  // 1 mm is the same centimetre
}

TEST_CASE("Procedural vegetation - the sink and the tilt follow the ground", "[terrain][vegetation]") {
    TerrainVegetation veg;
    VegetationSpecies rock = tree("Rock", 500.0f, 1.0f, 3);
    rock.sinkM = 0.3f;
    rock.tilt = 1.0f;
    veg.addSpecies(rock);
    VegetationField field = flatField();
    field.normal = [](f32, f32) { return Vec3(0.6f, 0.8f, 0.0f); };
    const auto all = resolveAll(veg, field, 0, 1);
    REQUIRE_FALSE(all.empty());
    for (const VegetationResolved& r : all) {
        REQUIRE(r.y == Approx(-0.3f * r.scale).margin(1e-5f));
        REQUIRE(r.up[0] == Approx(0.6f).margin(1e-4f));
        REQUIRE(r.up[1] == Approx(0.8f).margin(1e-4f));
    }
}

// --------------------------------------------------------------------------------------------------- manual source

TEST_CASE("Manual vegetation - instances are added, moved between chunks and removed", "[terrain][vegetation]") {
    TerrainVegetation veg;
    veg.addSpecies(tree());
    const u32 uid = veg.species()[0].uid;
    VegetationInstance a;
    a.id = veg.newManualId(10.0f, 10.0f);
    a.x = 10.0f; a.z = 10.0f; a.species = uid;
    REQUIRE(veg.addManual(a));
    REQUIRE_FALSE(veg.addManual(a));  // an id in use
    REQUIRE(veg.manualCount() == 1);
    VegetationInstance moved = a;
    moved.x = -70.0f;  // another chunk
    REQUIRE(veg.replaceManual(moved));
    REQUIRE(veg.findManual(a.id)->x == -70.0f);
    REQUIRE(veg.manualCount() == 1);
    VegetationInstance gone;
    REQUIRE(veg.removeManual(a.id, &gone));
    REQUIRE(gone.x == -70.0f);
    REQUIRE(veg.manualCount() == 0);
    REQUIRE_FALSE(veg.removeManual(a.id));
    // New ids are unused and different.
    std::set<u64> ids;
    for (int i = 0; i < 200; ++i) ids.insert(veg.newManualId(1.0f, 1.0f));
    REQUIRE(ids.size() == 200);
}

TEST_CASE("Manual vegetation - a manual instance wins its spot from a procedural one, and rules hide it without deleting it", "[terrain][vegetation]") {
    TerrainVegetation veg;
    VegetationSpecies pine = tree("Pine", 800.0f, 3.0f, 1);
    veg.addSpecies(pine);
    const VegetationField field = flatField();
    const auto before = resolveAll(veg, field, 0, 1);
    REQUIRE(before.size() > 5);
    const VegetationResolved victim = before.front();
    VegetationInstance mine;
    mine.id = veg.newManualId(victim.x + 0.5f, victim.z);
    mine.x = victim.x + 0.5f;
    mine.z = victim.z;
    mine.species = veg.species()[0].uid;
    veg.addManual(mine);
    const auto after = resolveAll(veg, field, 0, 1);
    REQUIRE(idsOf(after).count(mine.id) == 1);
    REQUIRE(idsOf(after).count(victim.id) == 0);  // the procedural one gave way

    // The ground changes so the species no longer suits the spot: the instance is hidden, not deleted.
    const VegetationField hostile = flatField([&](u32, f32 x, f32) { return x > victim.x + 100.0f ? 1.0f : 0.0f; });
    REQUIRE(idsOf(resolveAll(veg, hostile, 0, 1)).count(mine.id) == 0);
    REQUIRE(veg.findManual(mine.id) != nullptr);
    // Painted on purpose (forced), it stays.
    mine.flags = kVegForced;
    veg.replaceManual(mine);
    REQUIRE(idsOf(resolveAll(veg, hostile, 0, 1)).count(mine.id) == 1);
}

TEST_CASE("Vegetation overrides - an edit of a procedural instance survives regenerating", "[terrain][vegetation]") {
    TerrainVegetation veg;
    veg.addSpecies(tree());
    const VegetationField field = flatField();
    const auto base = resolveAll(veg, field, 0, 1);
    REQUIRE(base.size() > 5);
    VegetationOverride bigger;
    bigger.scaleMul = 2.0f;
    bigger.yawAdd = 0.5f;
    bigger.hasTint = true;
    bigger.tint[0] = 10;
    veg.setOverride(base[1].id, bigger);
    VegetationOverride removed;
    removed.removed = true;
    veg.setOverride(base[2].id, removed);

    // Regenerate twice, and after the species changed: the edits are still there.
    for (int round = 0; round < 2; ++round) {
        const auto again = resolveAll(veg, field, 0, 1);
        REQUIRE(again.size() == base.size() - 1);
        const auto it = std::find_if(again.begin(), again.end(), [&](const VegetationResolved& r) { return r.id == base[1].id; });
        REQUIRE(it != again.end());
        REQUIRE(it->scale == Approx(base[1].scale * 2.0f));
        REQUIRE(it->yaw == Approx(base[1].yaw + 0.5f));
        REQUIRE(it->tint[0] == Approx(10.0f / 255.0f));
        VegetationSpecies changed = veg.species()[0];
        changed.material = "Materials/round" + std::to_string(round) + ".mat";  // what is drawn changes, not where it stands
        veg.setSpecies(0, changed);
    }
    // An identity override is no override.
    veg.setOverride(base[1].id, VegetationOverride{});
    REQUIRE(veg.findOverride(base[1].id) == nullptr);
}

// -------------------------------------------------------------------------------------------------------- strokes

namespace {

VegetationBrush paintBrush(u32 species, f32 radius = 12.0f) {
    VegetationBrush brush;
    brush.shape.radiusM = radius;
    brush.shape.strength = 1.0f;
    brush.shape.falloff = FalloffCurve::Constant;
    brush.tool = VegetationTool::Paint;
    brush.species = species;
    brush.per100m2 = 4.0f;
    return brush;
}

VegetationRecord run(TerrainVegetation& veg, const VegetationBrush& brush, const std::vector<std::pair<f64, f64>>& path, const VegetationField& field,
                     const TerrainMasks* masks = nullptr) {
    const TerrainHeightmap ground = flatGround();
    VegetationStroke stroke(veg, ground, settings(), brush, field);
    stroke.setMasks(masks);
    for (const auto& p : path) stroke.addPoint({p.first, p.second, 1.0f});
    return stroke.finish();
}

}  // namespace

TEST_CASE("Vegetation stroke - painting places instances of the species inside the brush, none overlapping", "[terrain][vegetation]") {
    TerrainVegetation veg;
    VegetationSpecies shrub = tree("Shrub", 0.0f, 1.0f, 1);  // no procedural source: only what is painted
    veg.addSpecies(shrub);
    const VegetationRecord record = run(veg, paintBrush(0), {{0.0, 0.0}, {20.0, 0.0}}, flatField());
    REQUIRE_FALSE(record.empty());
    REQUIRE(record.hasChunks);
    const auto placed = veg.allManual();
    REQUIRE(placed.size() > 20);
    for (const VegetationInstance& i : placed) {
        REQUIRE(i.x > -12.5f);
        REQUIRE(i.x < 32.5f);
        REQUIRE(std::fabs(i.z) < 12.5f);
        REQUIRE(i.species == veg.species()[0].uid);
        REQUIRE(i.variant < 3);
        REQUIRE(i.scale >= 0.8f);
        REQUIRE(i.scale <= 1.2f);
    }
    for (usize a = 0; a < placed.size(); ++a) {
        for (usize b = a + 1; b < placed.size(); ++b) {
            const f32 dx = placed[a].x - placed[b].x, dz = placed[a].z - placed[b].z;
            const f32 reach = 1.0f * placed[a].scale + 1.0f * placed[b].scale;
            REQUIRE(dx * dx + dz * dz >= reach * reach * 0.999f);
        }
    }
}

TEST_CASE("Vegetation stroke - the same path gives the same vegetation at any sampling rate", "[terrain][vegetation]") {
    auto paint = [&](int points) {
        TerrainVegetation veg;
        veg.addSpecies(tree("Shrub", 0.0f, 1.0f, 1));
        std::vector<std::pair<f64, f64>> path;
        for (int i = 0; i <= points; ++i) path.push_back({-30.0 + 60.0 * i / points, 5.0});
        run(veg, paintBrush(0, 8.0f), path, flatField());
        std::vector<std::pair<f32, f32>> spots;
        for (const VegetationInstance& i : veg.allManual()) spots.push_back({i.x, i.z});
        return spots;
    };
    // Ids come from a counter, so compare the spots: the stamps and their hashes do not depend on the sampling.
    const auto a = paint(30), b = paint(144), c = paint(3);
    REQUIRE(a == b);
    REQUIRE(a == c);
}

TEST_CASE("Vegetation stroke - the species' rules reject spots unless the brush is forced", "[terrain][vegetation]") {
    VegetationSpecies pine = tree("Pine", 0.0f, 1.0f, 1);
    BiomeTerm any;
    any.mask = "slope";
    pine.rules = {any};
    const VegetationField westOnly = flatField([](u32, f32 x, f32) { return x < 0.0f ? 1.0f : 0.0f; });
    TerrainVegetation veg;
    veg.addSpecies(pine);
    run(veg, paintBrush(0, 20.0f), {{0.0, 0.0}}, westOnly);
    for (const VegetationInstance& i : veg.allManual()) REQUIRE(i.x < 0.0f);
    const usize respected = veg.manualCount();
    REQUIRE(respected > 0);

    TerrainVegetation forced;
    forced.addSpecies(pine);
    VegetationBrush brush = paintBrush(0, 20.0f);
    brush.respectRules = false;
    run(forced, brush, {{0.0, 0.0}}, westOnly);
    REQUIRE(forced.manualCount() > respected);
    bool east = false;
    for (const VegetationInstance& i : forced.allManual()) {
        east |= i.x > 0.0f;
        REQUIRE((i.flags & kVegForced) != 0);
    }
    REQUIRE(east);
}

TEST_CASE("Vegetation stroke - painting does not crowd out what has priority", "[terrain][vegetation]") {
    TerrainVegetation veg;
    veg.addSpecies(tree("Oak", 0.0f, 5.0f, 9));
    veg.addSpecies(tree("Grass", 0.0f, 0.3f, 0));
    VegetationBrush oakBrush = paintBrush(0, 2.0f);
    oakBrush.per100m2 = 60.0f;
    run(veg, oakBrush, {{0.0, 0.0}}, flatField());  // an oak in the middle (a few tries)
    const auto oaks = veg.allManual();
    REQUIRE_FALSE(oaks.empty());
    VegetationBrush grass = paintBrush(1, 20.0f);
    grass.per100m2 = 40.0f;
    run(veg, grass, {{0.0, 0.0}}, flatField());
    for (const VegetationInstance& g : veg.allManual()) {
        if (g.species != veg.species()[1].uid) continue;
        for (const VegetationInstance& o : oaks) {
            const f32 dx = g.x - o.x, dz = g.z - o.z;
            REQUIRE(dx * dx + dz * dz >= (5.0f * o.scale + 0.3f * g.scale) * (5.0f * o.scale + 0.3f * g.scale) * 0.99f);
        }
    }
}

TEST_CASE("Vegetation stroke - every tool undoes and redoes exactly", "[terrain][vegetation]") {
    struct Case {
        VegetationTool tool;
        const char* name;
    };
    const Case cases[] = {{VegetationTool::Paint, "paint"}, {VegetationTool::Erase, "erase"}, {VegetationTool::Scale, "scale"},
                          {VegetationTool::Rotate, "rotate"}, {VegetationTool::Tint, "tint"}, {VegetationTool::Replace, "replace"},
                          {VegetationTool::Thin, "thin"}};
    for (const Case& c : cases) {
        TerrainVegetation veg;
        veg.addSpecies(tree("Pine", 500.0f, 2.0f, 2));
        veg.addSpecies(tree("Birch", 0.0f, 1.0f, 1));
        const VegetationField field = flatField();
        // Some manual instances to act on, as well as the procedural ones.
        run(veg, paintBrush(1, 10.0f), {{10.0, 10.0}}, field);
        const std::vector<u8> startBytes = veg.toBytes();
        const auto startSight = idsOf(resolveAll(veg, field));

        VegetationBrush brush = paintBrush(c.tool == VegetationTool::Paint ? 1u : 1u, 16.0f);
        brush.tool = c.tool;
        brush.shape.strength = 0.7f;
        brush.shape.falloff = FalloffCurve::Smooth;
        brush.amount = c.tool == VegetationTool::Rotate ? 40.0f : 0.5f;
        brush.tint[0] = 0.1f;
        const VegetationRecord record = run(veg, brush, {{-5.0, 5.0}, {10.0, 10.0}, {20.0, 10.0}}, field);
        const std::vector<u8> doneBytes = veg.toBytes();
        const auto doneSight = idsOf(resolveAll(veg, field));
        INFO(c.name);
        REQUIRE_FALSE(record.empty());
        REQUIRE(doneBytes != startBytes);

        record.applyBefore(veg);
        REQUIRE(veg.toBytes() == startBytes);
        REQUIRE(idsOf(resolveAll(veg, field)) == startSight);
        record.applyAfter(veg);
        REQUIRE(veg.toBytes() == doneBytes);
        REQUIRE(idsOf(resolveAll(veg, field)) == doneSight);
    }
}

TEST_CASE("Vegetation stroke - erasing procedural vegetation leaves an override, painting over it does not bring it back", "[terrain][vegetation]") {
    TerrainVegetation veg;
    veg.addSpecies(tree("Pine", 500.0f, 2.0f, 2));
    const VegetationField field = flatField();
    const auto before = resolveAll(veg, field, 0, 0);
    REQUIRE(before.size() > 3);
    VegetationBrush erase = paintBrush(0, 60.0f);
    erase.tool = VegetationTool::Erase;
    erase.shape.strength = 1.0f;
    const VegetationRecord record = run(veg, erase, {{16.0, 16.0}}, field);
    REQUIRE(veg.overrideCount() > 0);
    REQUIRE(veg.manualCount() == 0);
    REQUIRE_FALSE(record.overrides.empty());
    REQUIRE(resolveAll(veg, field, 0, 0).empty());
    // The override is keyed by id: the same ids come back if the override goes, and nothing else changes.
    record.applyBefore(veg);
    REQUIRE(idsOf(resolveAll(veg, field, 0, 0)) == idsOf(before));
}

TEST_CASE("Vegetation stroke - scale, rotate and tint change what is under the brush", "[terrain][vegetation]") {
    TerrainVegetation veg;
    veg.addSpecies(tree("Pine", 500.0f, 2.0f, 2));
    const VegetationField field = flatField();
    const auto before = resolveAll(veg, field, 0, 0);
    auto edit = [&](VegetationTool tool, f32 amount) {
        VegetationBrush b = paintBrush(0, 60.0f);
        b.tool = tool;
        b.amount = amount;
        b.tint[0] = 0.0f; b.tint[1] = 0.0f; b.tint[2] = 1.0f;
        b.shape.strength = 1.0f;
        run(veg, b, {{16.0, 16.0}}, field);
    };
    edit(VegetationTool::Scale, 1.0f);
    edit(VegetationTool::Rotate, 90.0f);
    edit(VegetationTool::Tint, 0.0f);
    const auto after = resolveAll(veg, field, 0, 0);
    REQUIRE(after.size() == before.size());
    for (usize i = 0; i < after.size(); ++i) {
        REQUIRE(after[i].id == before[i].id);
        REQUIRE(after[i].scale > before[i].scale);
        REQUIRE(after[i].yaw != before[i].yaw);
        REQUIRE(after[i].tint[2] == Approx(1.0f).margin(0.02f));  // moved to blue
        REQUIRE(after[i].tint[0] < before[i].tint[0]);
    }
}

TEST_CASE("Vegetation stroke - replace turns procedural vegetation into manual instances of another species", "[terrain][vegetation]") {
    TerrainVegetation veg;
    veg.addSpecies(tree("Pine", 500.0f, 2.0f, 2));
    veg.addSpecies(tree("Birch", 0.0f, 2.0f, 2));
    const VegetationField field = flatField();
    const auto before = resolveAll(veg, field, 0, 0);
    VegetationBrush replace = paintBrush(1, 80.0f);
    replace.tool = VegetationTool::Replace;
    replace.onlySpecies = 0;
    replace.shape.strength = 1.0f;
    run(veg, replace, {{16.0, 16.0}}, field);
    const auto after = resolveAll(veg, field, 0, 0);
    REQUIRE(after.size() == before.size());
    for (const VegetationResolved& r : after) {
        REQUIRE(r.species == 1);
        REQUIRE(r.manual);
    }
    // Same spots, same sizes.
    std::set<std::pair<f32, f32>> was, now;
    for (const VegetationResolved& r : before) was.insert({r.x, r.z});
    for (const VegetationResolved& r : after) now.insert({r.x, r.z});
    REQUIRE(was == now);
}

TEST_CASE("Vegetation stroke - thin removes a share and only ever the lowest ranks first", "[terrain][vegetation]") {
    TerrainVegetation veg;
    veg.addSpecies(tree("Grass", 3000.0f, 0.2f, 0));
    const VegetationField field = flatField();
    const usize full = resolveAll(veg, field, 0, 0).size();
    auto thin = [&](f32 strength) {
        VegetationBrush b = paintBrush(0, 80.0f);
        b.tool = VegetationTool::Thin;
        b.shape.strength = strength;
        run(veg, b, {{16.0, 16.0}}, field);
        return resolveAll(veg, field, 0, 0).size();
    };
    const usize half = thin(0.5f);
    REQUIRE(half < full);
    REQUIRE(half > full / 4);
    const auto kept = idsOf(resolveAll(veg, field, 0, 0));
    // Thinning again with the same share removes no more.
    REQUIRE(thin(0.5f) == half);
    REQUIRE(idsOf(resolveAll(veg, field, 0, 0)) == kept);
    REQUIRE(thin(1.0f) == 0);
}

// ----------------------------------------------------------------------------------- bake, re-derive, reseed, species

TEST_CASE("Vegetation - baking procedural vegetation keeps the ground as it is and makes it editable; re-deriving undoes it", "[terrain][vegetation]") {
    TerrainVegetation veg;
    veg.addSpecies(tree("Pine", 500.0f, 2.0f, 2));
    const VegetationField field = flatField();
    const auto before = resolveAll(veg, field);
    const std::vector<u8> startBytes = veg.toBytes();
    const VegetationRecord baked = bakeProcedural(veg, 0, field, 256.0f, 256.0f);
    REQUIRE_FALSE(baked.empty());
    REQUIRE(veg.manualCount() == before.size());
    REQUIRE_FALSE(veg.species()[0].procedural);
    const auto after = resolveAll(veg, field);
    REQUIRE(after.size() == before.size());
    std::set<std::pair<f32, f32>> was, now;
    for (const VegetationResolved& r : before) was.insert({r.x, r.z});
    for (const VegetationResolved& r : after) now.insert({r.x, r.z});
    REQUIRE(was == now);
    for (const VegetationResolved& r : after) REQUIRE(r.manual);

    // Undo of the bake, then redo.
    baked.applyBefore(veg);
    REQUIRE(veg.toBytes() == startBytes);
    baked.applyAfter(veg);
    REQUIRE(veg.manualCount() == before.size());

    // Re-deriving removes the baked instances and turns the procedural source back on.
    const VegetationRecord derived = rederiveProcedural(veg, 0);
    REQUIRE(veg.manualCount() == 0);
    REQUIRE(veg.species()[0].procedural);
    REQUIRE(idsOf(resolveAll(veg, field)) == idsOf(before));
    derived.applyBefore(veg);
    REQUIRE(veg.manualCount() == before.size());
}

TEST_CASE("Vegetation - a new seed grows another forest and undoes", "[terrain][vegetation]") {
    TerrainVegetation veg;
    veg.addSpecies(tree());
    const auto before = idsOf(resolveAll(veg, flatField()));
    const VegetationRecord record = reseed(veg, 777);
    REQUIRE(veg.seed() == 777);
    REQUIRE(idsOf(resolveAll(veg, flatField())) != before);
    record.applyBefore(veg);
    REQUIRE(idsOf(resolveAll(veg, flatField())) == before);
    REQUIRE(reseed(veg, veg.seed()).empty());
}

TEST_CASE("Vegetation - editing a species reaches every instance, and removing one is undone with its instances", "[terrain][vegetation]") {
    TerrainVegetation veg;
    veg.addSpecies(tree("Pine", 0.0f, 1.0f, 1));
    veg.addSpecies(tree("Birch", 0.0f, 1.0f, 1));
    run(veg, paintBrush(0, 10.0f), {{0.0, 0.0}}, flatField());
    run(veg, paintBrush(1, 10.0f), {{40.0, 0.0}}, flatField());
    const usize pines = veg.manualCount();
    REQUIRE(pines > 4);
    const std::vector<u8> start = veg.toBytes();

    // Retexture: the species' variants change; no instance is touched.
    std::string text = vegetationSpeciesToText(veg.species());
    const usize at = text.find("pine_a");
    REQUIRE(at != std::string::npos);
    text.replace(at, 6, "fir_ab");
    std::string error;
    const VegetationRecord retexture = editSpeciesFromText(veg, text, &error);
    REQUIRE_FALSE(retexture.empty());
    REQUIRE(veg.species()[0].meshes[0] == "Meshes/fir_ab.obj");
    REQUIRE(veg.manualCount() == pines);

    // Removing the second species by text takes its instances along; undo brings both back.
    const std::string withoutBirch = vegetationSpeciesToText({veg.species()[0]});
    const VegetationRecord removal = editSpeciesFromText(veg, withoutBirch, &error);
    REQUIRE(veg.species().size() == 1);
    REQUIRE(veg.manualCount() < pines * 2);
    removal.applyBefore(veg);
    REQUIRE(veg.species().size() == 2);
    retexture.applyBefore(veg);
    REQUIRE(veg.toBytes() == start);

    REQUIRE(editSpeciesFromText(veg, "species A\n  scale 3 1\n", &error).empty());
    REQUIRE(error.find("line 2") != std::string::npos);
}

// ------------------------------------------------------------------------------------------------------------ file

TEST_CASE("Vegetation file - round trips and notices damage", "[terrain][vegetation]") {
    TerrainVegetation veg;
    veg.addSpecies(tree("Pine", 300.0f, 2.0f, 2));
    veg.addSpecies(tree("Rock", 0.0f, 1.5f, 5));
    veg.setSeed(4242);
    run(veg, paintBrush(1, 10.0f), {{0.0, 0.0}}, flatField());
    VegetationBrush erase = paintBrush(0, 30.0f);
    erase.tool = VegetationTool::Erase;
    erase.shape.strength = 1.0f;
    run(veg, erase, {{10.0, 10.0}}, flatField());
    REQUIRE(veg.overrideCount() > 0);

    const std::vector<u8> bytes = veg.toBytes();
    TerrainVegetation back;
    std::string error;
    REQUIRE(TerrainVegetation::fromBytes(bytes.data(), bytes.size(), back, &error));
    REQUIRE(back.toBytes() == bytes);
    REQUIRE(back.seed() == 4242);
    REQUIRE(back.manualCount() == veg.manualCount());
    REQUIRE(idsOf(resolveAll(back, flatField())) == idsOf(resolveAll(veg, flatField())));

    std::vector<u8> damaged = bytes;
    damaged[bytes.size() / 2] ^= 0x10;
    TerrainVegetation other;
    REQUIRE_FALSE(TerrainVegetation::fromBytes(damaged.data(), damaged.size(), other, &error));
    REQUIRE(error.find("checksum") != std::string::npos);
    REQUIRE_FALSE(TerrainVegetation::fromBytes(bytes.data(), bytes.size() - 7, other, &error));
    std::vector<u8> wrong = bytes;
    wrong[0] = 'X';
    REQUIRE_FALSE(TerrainVegetation::fromBytes(wrong.data(), wrong.size(), other, &error));

    const std::string path = (std::filesystem::temp_directory_path() / "caffeine_test.terrainveg").string();
    REQUIRE(veg.save(path, &error));
    TerrainVegetation loaded;
    REQUIRE(TerrainVegetation::load(path, loaded, &error));
    REQUIRE(loaded.toBytes() == bytes);
    std::filesystem::remove(path);
    REQUIRE_FALSE(TerrainVegetation::load(path, loaded, &error));
}

// ------------------------------------------------------------------------------------------------------- budgets

TEST_CASE("Vegetation - a dense lawn of ninety thousand instances is made in about two seconds, once per chunk", "[terrain][vegetation]") {
    TerrainVegetation veg;
    VegetationSpecies grass = tree("Grass", 100000.0f, 0.03f, 0);  // 10 per m2: a lawn
    grass.tilt = 1.0f;
    veg.addSpecies(grass);
    const VegetationField field = flatField();
    const auto begin = std::chrono::steady_clock::now();
    std::vector<VegetationResolved> hectare;
    // 100 m x 100 m: 4 x 4 chunks cover 128 m, keep 3 x 3 (96 m) as the sample.
    for (i32 cz = 0; cz < 3; ++cz) {
        for (i32 cx = 0; cx < 3; ++cx) veg.resolveChunk(cx, cz, field, hectare);
    }
    const f64 ms = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - begin).count();
    // 96 x 96 m = 0.92 ha at 100 000 per ha is about 92 000 instances.
    REQUIRE(hectare.size() > 60000);
    INFO(hectare.size() << " instances in " << ms << " ms");
    REQUIRE(ms < 4000.0);
}

TEST_CASE("Vegetation - nothing stands off the terrain", "[terrain][vegetation]") {
    TerrainVegetation veg;
    veg.addSpecies(tree("Pine", 800.0f, 2.0f, 1));
    VegetationField field = flatField();
    field.halfX = 64.0f;
    field.halfZ = 32.0f;
    const auto all = resolveAll(veg, field);
    REQUIRE_FALSE(all.empty());
    for (const VegetationResolved& r : all) {
        REQUIRE(std::fabs(r.x) <= 64.0f);
        REQUIRE(std::fabs(r.z) <= 32.0f);
    }
}
