#include "catch.hpp"

#include "../src/caffeine/core/Hash.hpp"
#include "../src/caffeine/terrain/TerrainHeightmap.hpp"
#include "../src/caffeine/terrain/TerrainMasks.hpp"
#include "../src/caffeine/world/WorldBuild.hpp"
#include "../src/caffeine/world/WorldComposition.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <thread>
#include <tuple>
#include <vector>

using namespace Caffeine;
using namespace Caffeine::World;

namespace {

const char* const kValley = R"(# Valley
seed 1234
terrain Terrain/Alpine.generator.terrain size 256 256
water Water/River.generator.water follow wetness min 0.8
material terrain.layer0 Materials/Moss.generator.material
class boulder ref Nature/MossRock.generator.nature variants 6 priority 30 cell 16 radius 2.2 density 0.9 scale 0.7 1.6 tilt 0.8 sink 0.3
  slope 0.05 0.10 1.00 1.00
class oak ref Nature/OakTree.generator.nature variants 8 priority 20 cell 9 radius 3 density 0.8 scale 0.8 1.4 tilt 0.1 tint 0.1
  slope   0.00 0.00 0.15 0.35
  wetness 0.20 0.40 0.80 1.00
  adapt size wetness
class grass ref Nature/LowerGrass.generator.nature priority 5 cell 2.5 radius 0.4 tilt 1
  slope 0 0 0.3 0.5
)";

/// A 256 m bowl with a little ripple, and a stored wetness mask (wet in the low ground).
struct Valley {
    Terrain::TerrainHeightmap map{129, 129};
    Terrain::TerrainMasks masks;
    Terrain::TerrainFrame frame;
    WorldComposition composition;

    Valley() {
        for (u32 z = 0; z < 129; ++z) {
            for (u32 x = 0; x < 129; ++x) {
                // A polynomial (no libm), so the golden value below does not depend on the platform's sin/cos.
                const f32 fx = static_cast<f32>(x) - 40.0f, fz = static_cast<f32>(z) - 90.0f;
                const f32 ripple = static_cast<f32>((x * 7 + z * 13) % 16) * 0.05f;
                const f32 h = 4.0f + 0.0016f * fx * fx + 0.0011f * fz * fz - 0.0004f * fx * fz + ripple;
                map.setNormalized(x, z, std::clamp(h / 30.0f, 0.0f, 1.0f));
            }
        }
        frame.heightmap = &map;
        frame.sizeX = 256.0f;
        frame.sizeZ = 256.0f;
        frame.maxHeight = 30.0f;
        std::vector<u8> wet(33 * 33);
        for (u32 z = 0; z < 33; ++z) {
            for (u32 x = 0; x < 33; ++x) {
                f32 h = 0.0f;
                masks.sample(frame, "height", static_cast<f32>(x) * 8.0f, static_cast<f32>(z) * 8.0f, h);
                wet[z * 33 + x] = static_cast<u8>(std::clamp(1.0f - h, 0.0f, 1.0f) * 255.0f);
            }
        }
        REQUIRE(masks.set("wetness", 33, 33, wet.data()));
        std::string error;
        REQUIRE(WorldComposition::fromText(kValley, composition, &error));
        INFO(error);
    }

    WorldBuildRequest request() const {
        WorldBuildRequest r;
        r.composition = &composition;
        r.generatorVersion = Core::GENERATOR_VERSION;
        r.frame = frame;
        r.masks = &masks;
        return r;
    }
};

using Key = std::tuple<u32, i32, i32>;
std::set<Key> keysOf(const WorldPart& part) {
    std::set<Key> out;
    for (const WorldClassInstances& cls : part.classes) {
        for (const Scatter::Instance& i : cls.instances) out.insert({cls.salt, i.cellX, i.cellZ});
    }
    return out;
}

size_t countOf(const WorldPart& part) {
    size_t n = 0;
    for (const WorldClassInstances& cls : part.classes) n += cls.instances.size();
    return n;
}

}  // namespace

TEST_CASE("World: class salts are a stable hash of the id", "[world][determinism]") {
    // Pinned: the Rust twin (recipe_ref::content_hash folded to 32 bits) must give the same numbers.
    CHECK(classSalt("oak") == classSalt("oak"));
    CHECK(classSalt("oak") != classSalt("boulder"));
    CHECK(classSalt("") == 0xa8258331u);
    CHECK(classSalt("oak") == 0x85d158a1u);
    CHECK(classSalt("boulder") == 0x9cff765fu);
}

TEST_CASE("World: the text form parses, validates and round-trips", "[world]") {
    WorldComposition c;
    std::string error;
    REQUIRE(WorldComposition::fromText(kValley, c, &error));
    CHECK(c.seed == 1234);
    CHECK(c.terrain.ref == "Terrain/Alpine.generator.terrain");
    CHECK(c.terrain.sizeX == 256.0f);
    REQUIRE(c.water.size() == 1);
    CHECK(c.water[0].follow == "wetness");
    REQUIRE(c.materials.size() == 1);
    CHECK(c.materials[0].slot == "terrain.layer0");
    REQUIRE(c.classes.size() == 3);
    CHECK(c.classes[1].id == "oak");
    CHECK(c.classes[1].variants == 8);
    CHECK(c.classes[1].where.size() == 2);
    REQUIRE(c.classes[1].adapt.size() == 1);
    CHECK(c.classes[1].adapt[0].target == "size");
    CHECK(c.validate().empty());

    WorldComposition again;
    REQUIRE(WorldComposition::fromText(c.toText(), again, &error));
    CHECK(again.toText() == c.toText());

    SECTION("syntax errors name the line") {
        WorldComposition bad;
        CHECK_FALSE(WorldComposition::fromText("seed 1\n  slope 0 0 1 1\n", bad, &error));
        CHECK(error.rfind("line 2:", 0) == 0);
        CHECK_FALSE(WorldComposition::fromText("class oak ref A.generator.nature colour 3\n", bad, &error));
        CHECK(error.find("colour") != std::string::npos);
        CHECK_FALSE(WorldComposition::fromText("class oak ref X\n  slope 0.5 0.2 1 1\n", bad, &error));
        CHECK_FALSE(WorldComposition::fromText("class oak ref X\n  density 0 0 1 1\n", bad, &error));
        CHECK_FALSE(WorldComposition::fromText("override grow oak 1 2\n", bad, &error));
    }

    SECTION("validation lists every problem") {
        WorldComposition bad = c;
        bad.terrain.ref.clear();
        bad.classes.push_back(bad.classes[0]);  // duplicate id
        bad.classes[1].minScale = 2.0f;
        bad.overrides.push_back({OverrideOp::Pin, "oak", 0, 0, 0.0f, 0.0f, 99});
        bad.overrides.push_back({OverrideOp::Remove, "willow", 0, 0, 0.0f, 0.0f, 0});
        const auto errors = bad.validate();
        CHECK(errors.size() == 5);
    }
}

TEST_CASE("World: a build is a pure function and halves add up to the whole", "[world][determinism]") {
    const Valley valley;
    const WorldPart whole = build(valley.request());
    INFO((whole.errors.empty() ? std::string() : whole.errors[0]));
    REQUIRE(whole.ok);
    REQUIRE(whole.classes.size() == 3);
    // Placement order: priority descending, whatever the list order.
    CHECK(whole.classes[0].id == "boulder");
    CHECK(whole.classes[1].id == "oak");
    CHECK(whole.classes[2].id == "grass");
    for (const WorldClassInstances& cls : whole.classes) CHECK_FALSE(cls.instances.empty());
    CHECK(whole.total == countOf(whole));
    CHECK(whole.water.bodies.size() == 1);
    CHECK(whole.materials.size() == 1);

    const WorldPart again = build(valley.request());
    CHECK(instanceHash(again) == instanceHash(whole));
    CHECK(again.cacheKey == whole.cacheKey);

    WorldBuildRequest left = valley.request(), right = valley.request();
    left.x1 = 100.0f;
    left.z1 = 256.0f;
    right.x0 = 100.0f;
    right.x1 = 256.0f;
    right.z1 = 256.0f;
    const WorldPart a = build(left), b = build(right);
    REQUIRE(a.ok);
    REQUIRE(b.ok);
    std::set<Key> joined = keysOf(a);
    const std::set<Key> rightKeys = keysOf(b);
    for (const Key& k : rightKeys) CHECK(joined.insert(k).second);  // no instance in both halves
    CHECK(joined == keysOf(whole));
    CHECK(a.cacheKey != whole.cacheKey);

    // Golden value: changing it means every saved world moves; bump GENERATOR_VERSION instead.
    CHECK(instanceHash(whole) == 0x4b14b768a899d002ull);
}

TEST_CASE("World: reordering or adding classes does not move the others", "[world][determinism]") {
    Valley valley;
    const WorldPart before = build(valley.request());
    REQUIRE(before.ok);

    std::reverse(valley.composition.classes.begin(), valley.composition.classes.end());
    const WorldPart reordered = build(valley.request());
    REQUIRE(reordered.ok);
    CHECK(instanceHash(reordered) == instanceHash(before));

    // A new low-priority class placed on top of the list leaves boulders and oaks where they were.
    WorldClass moss;
    moss.id = "moss";
    moss.ref = "Nature/Moss.generator.nature";
    moss.priority = 1;
    moss.cell = 3.0f;
    moss.radius = 0.3f;
    valley.composition.classes.insert(valley.composition.classes.begin(), moss);
    const WorldPart added = build(valley.request());
    REQUIRE(added.ok);
    REQUIRE(added.classes.size() == 4);
    for (size_t i = 0; i < 2; ++i) {
        REQUIRE(added.classes[i].id == before.classes[i].id);
        CHECK(added.classes[i].instances.size() == before.classes[i].instances.size());
    }
    const auto boulders = [](const WorldPart& p) {
        std::set<std::pair<i32, i32>> out;
        for (const auto& i : p.classes[0].instances) out.insert({i.cellX, i.cellZ});
        return out;
    };
    CHECK(boulders(added) == boulders(before));
    CHECK(added.cacheKey != before.cacheKey);
}

TEST_CASE("World: builds on several threads match the single-threaded one", "[world][determinism]") {
    const Valley valley;
    const u64 expected = instanceHash(build(valley.request()));
    std::vector<u64> results(7, 0);
    std::vector<std::thread> threads;
    for (size_t t = 0; t < results.size(); ++t) {
        threads.emplace_back([&, t] { results[t] = instanceHash(build(valley.request())); });
    }
    for (std::thread& t : threads) t.join();
    for (const u64 r : results) CHECK(r == expected);
}

TEST_CASE("World: overrides remove, move and pin without touching other instances", "[world]") {
    Valley valley;
    const WorldPart before = build(valley.request());
    REQUIRE(before.ok);
    const WorldClassInstances& oaks = before.classes[1];
    REQUIRE(oaks.instances.size() >= 3);
    const Scatter::Instance gone = oaks.instances[0], moved = oaks.instances[1], pinned = oaks.instances[2];

    valley.composition.overrides.push_back({OverrideOp::Remove, "oak", gone.cellX, gone.cellZ, 0.0f, 0.0f, 0});
    valley.composition.overrides.push_back({OverrideOp::Move, "oak", moved.cellX, moved.cellZ, 10.0f, 20.0f, 0});
    valley.composition.overrides.push_back(
        {OverrideOp::Pin, "oak", pinned.cellX, pinned.cellZ, 0.0f, 0.0f, (pinned.variant + 1) % 8});
    REQUIRE(valley.composition.validate().empty());

    const WorldPart after = build(valley.request());
    REQUIRE(after.ok);
    CHECK(after.total + 1 == before.total);
    const auto find = [&](i32 cx, i32 cz) -> const Scatter::Instance* {
        for (const auto& i : after.classes[1].instances) {
            if (i.cellX == cx && i.cellZ == cz) return &i;
        }
        return nullptr;
    };
    CHECK(find(gone.cellX, gone.cellZ) == nullptr);
    const Scatter::Instance* m = find(moved.cellX, moved.cellZ);
    REQUIRE(m);
    CHECK(m->position.x == 10.0f);
    CHECK(m->position.z == 20.0f);
    CHECK(m->seed == moved.seed);
    const Scatter::Instance* p = find(pinned.cellX, pinned.cellZ);
    REQUIRE(p);
    CHECK(p->variant == (pinned.variant + 1) % 8);
    // Every other instance is untouched.
    CHECK(instanceHash(WorldPart{true, {}, {after.classes[0]}, {}, {}, {}, 0, false, 0}) ==
          instanceHash(WorldPart{true, {}, {before.classes[0]}, {}, {}, {}, 0, false, 0}));

    // The edit layer survives the text form.
    WorldComposition reparsed;
    REQUIRE(WorldComposition::fromText(valley.composition.toText(), reparsed));
    REQUIRE(reparsed.overrides.size() == 3);
    CHECK(reparsed.overrides[1].x == 10.0f);
}

TEST_CASE("World: references are checked all at once and nothing is built on failure", "[world]") {
    const Valley valley;
    WorldBuildRequest request = valley.request();
    request.resolve = [](const std::string& ref) {
        RecipeInfo info;
        info.found = ref != "Nature/OakTree.generator.nature";
        info.generatorVersion = 2;
        info.contentHash = classSalt(ref);
        if (ref.find(".generator.terrain") != std::string::npos) info.kind = RecipeKind::Terrain;
        if (ref.find(".generator.water") != std::string::npos) info.kind = RecipeKind::Water;
        if (ref.find(".generator.material") != std::string::npos) info.kind = RecipeKind::Texture;  // wrong kind
        if (ref.find(".generator.nature") != std::string::npos) info.kind = RecipeKind::Nature;
        return info;
    };
    const WorldPart part = build(request);
    CHECK_FALSE(part.ok);
    CHECK(part.classes.empty());
    REQUIRE(part.errors.size() == 2);
    CHECK(part.errors[0].find("Moss.generator.material") != std::string::npos);
    CHECK(part.errors[1].find("OakTree") != std::string::npos);

    SECTION("a newer world and a missing terrain are errors too") {
        WorldBuildRequest r = valley.request();
        r.generatorVersion = 3;
        r.frame.heightmap = nullptr;
        const WorldPart failed = build(r);
        CHECK_FALSE(failed.ok);
        CHECK(failed.errors.size() == 2);
    }

    SECTION("so are masks the terrain does not have, in a rule, an adapt or a river") {
        Valley other;
        other.composition.classes[2].where.push_back({"snow_depth", 0.0f, 0.0f, 0.5f, 1.0f});
        other.composition.classes[1].adapt.push_back({"tint", "moss", -1.0f});
        other.composition.water[0].follow = "flow";
        const WorldPart failed = build(other.request());
        CHECK_FALSE(failed.ok);
        CHECK(failed.errors.size() == 3);
    }
}

TEST_CASE("World: the instance budget stops the build and says so", "[world]") {
    const Valley valley;
    WorldBuildRequest request = valley.request();
    const WorldPart full = build(request);
    REQUIRE(full.ok);
    CHECK_FALSE(full.truncated);
    request.maxInstances = 300;
    const WorldPart cut = build(request);
    REQUIRE(cut.ok);
    CHECK(cut.truncated);
    CHECK(countOf(cut) == request.maxInstances);
    CHECK(cut.total == full.total);
    CHECK(full.total == countOf(full));
    // What is kept is the same every time (chunk order, as in Scatter on Terrain).
    CHECK(instanceHash(build(request)) == instanceHash(cut));
}

TEST_CASE("World: rivers, carving, lakes and the water_distance mask", "[world][water]") {
    Valley valley;
    valley.composition.water.clear();
    valley.composition.water.push_back({"Water/River.generator.water", "wetness", 0.85f, false, 0.0f, 1.5f});
    valley.composition.water.push_back({"Water/Lake.generator.water", "", 0.0f, true, 0.35f, 0.0f});
    WorldClass reeds;
    reeds.id = "reeds";
    reeds.ref = "Nature/Reeds.generator.nature";
    reeds.priority = 10;
    reeds.cell = 3.0f;
    reeds.radius = 0.5f;
    reeds.where.push_back({"water_distance", 0.0f, 0.0f, 0.05f, 0.1f});
    valley.composition.classes.push_back(reeds);
    REQUIRE(valley.composition.validate().empty());

    const WorldPart part = build(valley.request());
    INFO((part.errors.empty() ? std::string() : part.errors[0]));
    REQUIRE(part.ok);
    REQUIRE(part.water.bodies.size() == 2);
    const WaterBody& river = part.water.bodies[0];
    const WaterBody& lake = part.water.bodies[1];
    CHECK_FALSE(river.lake);
    CHECK(lake.lake);
    CHECK(river.cells > 0);
    CHECK(lake.cells > 0);
    CHECK(part.water.carved);

    // The river bed is 1.5 m lower than the baked ground, and its surface lies between the two.
    bool checked = false;
    for (size_t i = 0; i < river.coverage.size() && !checked; ++i) {
        if (!river.coverage[i]) continue;
        const f32 before = valley.map.heights()[i] * valley.frame.maxHeight;
        const f32 after = part.terrain.heights()[i] * valley.frame.maxHeight;
        if (before < 1.6f) continue;
        CHECK(after == Approx(before - 1.5f).margin(1e-3f));
        CHECK(river.surface[i] > after);
        CHECK(river.surface[i] < before);
        checked = true;
    }
    CHECK(checked);
    // Lake water stands at most at the level and above the ground.
    for (size_t i = 0; i < lake.coverage.size(); ++i) {
        if (!lake.coverage[i]) continue;
        CHECK(lake.surface[i] <= 0.35f * valley.frame.maxHeight + 1e-4f);
        CHECK(lake.surface[i] > part.terrain.heights()[i] * valley.frame.maxHeight);
    }
    // The distance mask is 0 on water and grows away from it.
    u8 farthest = 0;
    for (size_t i = 0; i < part.water.distance.size(); ++i) {
        if (river.coverage[i] || lake.coverage[i]) CHECK(part.water.distance[i] == 0);
        farthest = std::max(farthest, part.water.distance[i]);
    }
    CHECK(farthest > 128);

    // Reeds only grow at the bank.
    const WorldClassInstances* bank = nullptr;
    for (const auto& cls : part.classes) {
        if (cls.id == "reeds") bank = &cls;
    }
    REQUIRE(bank);
    REQUIRE_FALSE(bank->instances.empty());
    for (const InstanceContext& ctx : bank->context) CHECK(ctx.waterDistance < 0.1f);

    // Carving and lakes are computed on the whole terrain, so regions still add up.
    WorldBuildRequest left = valley.request(), right = valley.request();
    left.x1 = 128.0f;
    left.z1 = 256.0f;
    right.x0 = 128.0f;
    right.x1 = 256.0f;
    right.z1 = 256.0f;
    std::set<Key> joined = keysOf(build(left));
    for (const Key& k : keysOf(build(right))) joined.insert(k);
    CHECK(joined == keysOf(part));
}

TEST_CASE("World: adapt maps the ground context onto each instance", "[world][adapt]") {
    Valley plain;
    for (WorldClass& cls : plain.composition.classes) cls.adapt.clear();
    const WorldPart before = build(plain.request());
    REQUIRE(before.ok);

    Valley adapted;
    for (WorldClass& cls : adapted.composition.classes) cls.adapt.clear();
    WorldClass& oak = adapted.composition.classes[1];
    oak.adapt.push_back({"size", "wetness", 0.5f});
    oak.adapt.push_back({"snow", "height", -1.0f});
    oak.adapt.push_back({"tint", "neighbours", -1.0f});
    oak.adapt.push_back({"lean", "slope", 1.0f});
    const WorldPart after = build(adapted.request());
    INFO((after.errors.empty() ? std::string() : after.errors[0]));
    REQUIRE(after.ok);

    // Same instances (adapt never moves or removes), different looks.
    CHECK(keysOf(after) == keysOf(before));
    const WorldClassInstances& a = after.classes[1];
    const WorldClassInstances& b = before.classes[1];
    REQUIRE(a.id == "oak");
    REQUIRE(a.instances.size() == b.instances.size());
    REQUIRE(a.context.size() == a.instances.size());
    bool crowded = false;
    for (size_t i = 0; i < a.instances.size(); ++i) {
        const InstanceContext& ctx = a.context[i];
        CHECK(a.instances[i].scale == Approx(b.instances[i].scale * (1.0f + 0.5f * (2.0f * ctx.wetness - 1.0f))));
        CHECK(ctx.snowModifier == Approx(ctx.height));
        CHECK(a.instances[i].tint.x == Approx(b.instances[i].tint.x * (1.0f - 0.2f * ctx.neighbours)));
        crowded = crowded || ctx.neighbours > 0.0f;
    }
    CHECK(crowded);
    // The other classes are untouched.
    CHECK(instanceHash(WorldPart{true, {}, {after.classes[0]}, {}, {}, {}, 0, false, 0}) ==
          instanceHash(WorldPart{true, {}, {before.classes[0]}, {}, {}, {}, 0, false, 0}));

    // Crowding reads past the region's border, so halves still agree with the whole, context included.
    WorldBuildRequest left = adapted.request();
    left.x1 = 100.0f;
    left.z1 = 256.0f;
    const WorldPart half = build(left);
    REQUIRE(half.ok);
    for (size_t i = 0; i < half.classes[1].instances.size(); ++i) {
        const Scatter::Instance& h = half.classes[1].instances[i];
        for (size_t j = 0; j < a.instances.size(); ++j) {
            if (a.instances[j].cellX != h.cellX || a.instances[j].cellZ != h.cellZ) continue;
            CHECK(half.classes[1].context[i].neighbours == a.context[j].neighbours);
            CHECK(h.scale == a.instances[j].scale);
        }
    }

    WorldComposition reparsed;
    REQUIRE(WorldComposition::fromText(adapted.composition.toText(), reparsed));
    REQUIRE(reparsed.classes[1].adapt.size() == 4);
    CHECK(reparsed.classes[1].adapt[0].amount == 0.5f);
    CHECK(reparsed.classes[1].adapt[1].amount < 0.0f);
}
