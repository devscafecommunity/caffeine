#include "catch.hpp"

#include "../src/caffeine/procedural/ProceduralNoise.hpp"
#include "../src/caffeine/scatter/Scatter.hpp"
#include "../src/caffeine/scatter/TerrainScatter.hpp"
#include "../src/caffeine/terrain/TerrainBiome.hpp"
#include "../src/caffeine/terrain/TerrainHeightmap.hpp"
#include "../src/caffeine/terrain/TerrainMasks.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

using namespace Caffeine;
using namespace Caffeine::Scatter;

namespace {

ScatterField hills() {
    ScatterField field;
    field.height = [](f32 x, f32 z) { return 3.0f * std::sin(x * 0.05f) + 2.0f * std::cos(z * 0.04f); };
    field.normal = [](f32 x, f32 z) {
        const f32 dx = 3.0f * 0.05f * std::cos(x * 0.05f);
        const f32 dz = -2.0f * 0.04f * std::sin(z * 0.04f);
        return Vec3(-dx, 1.0f, -dz).normalized();
    };
    return field;
}

std::vector<ScatterClass> forest() {
    ScatterClass rock;
    rock.id = 1;
    rock.priority = 30;
    rock.cellSize = 14.0f;
    rock.radius = 2.0f;
    rock.density = 0.6f;
    rock.sink = 0.2f;
    rock.tiltToNormal = 0.8f;
    ScatterClass tree;
    tree.id = 2;
    tree.priority = 20;
    tree.cellSize = 7.0f;
    tree.radius = 2.5f;
    tree.density = 0.9f;
    tree.variants = 4;
    ScatterClass grass;
    grass.id = 3;
    grass.priority = 5;
    grass.cellSize = 1.5f;
    grass.radius = 0.4f;
    grass.tiltToNormal = 1.0f;
    return {rock, tree, grass};
}

using Key = std::pair<std::pair<u32, i64>, i64>;
std::set<Key> keysOf(const std::vector<Instance>& list) {
    std::set<Key> out;
    for (const Instance& i : list) {
        out.insert({{i.classId, static_cast<i64>(std::llround(i.position.x * 1000.0f))},
                    static_cast<i64>(std::llround(i.position.z * 1000.0f))});
    }
    return out;
}

}  // namespace

TEST_CASE("Scatter: a chunk keeps only its own points and is a pure function", "[scatter][determinism]") {
    const auto classes = forest();
    const ScatterField field = hills();
    const auto a = scatterChunk(42, 2, -3, 48.0f, classes, field);
    const auto b = scatterChunk(42, 2, -3, 48.0f, classes, field);
    REQUIRE(a.size() > 50);
    REQUIRE(a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].position.x == b[i].position.x);
        CHECK(a[i].seed == b[i].seed);
        CHECK(a[i].position.x >= 2 * 48.0f);
        CHECK(a[i].position.x < 3 * 48.0f);
        CHECK(a[i].position.z >= -3 * 48.0f);
        CHECK(a[i].position.z < -2 * 48.0f);
    }
    // Another seed is another forest.
    CHECK(keysOf(scatterChunk(43, 2, -3, 48.0f, classes, field)) != keysOf(a));
}

TEST_CASE("Scatter: how the world is cut into chunks never changes the result", "[scatter][determinism]") {
    const auto classes = forest();
    const ScatterField field = hills();
    // One 96 m chunk against the four 48 m chunks that tile it.
    std::set<Key> whole = keysOf(scatterChunk(7, 0, 0, 96.0f, classes, field));
    std::set<Key> parts;
    for (i32 cz = 0; cz < 2; ++cz) {
        for (i32 cx = 0; cx < 2; ++cx) {
            const auto chunk = keysOf(scatterChunk(7, cx, cz, 48.0f, classes, field));
            for (const Key& key : chunk) CHECK(parts.insert(key).second);  // no duplicates on borders
        }
    }
    REQUIRE(whole.size() > 200);
    CHECK(parts == whole);
}

TEST_CASE("Scatter: nothing overlaps inside a chunk or across its borders", "[scatter]") {
    const auto classes = forest();
    const ScatterField field = hills();
    std::vector<Instance> all;
    for (i32 cz = -1; cz <= 1; ++cz) {
        for (i32 cx = -1; cx <= 1; ++cx) {
            const auto chunk = scatterChunk(99, cx, cz, 40.0f, classes, field);
            all.insert(all.end(), chunk.begin(), chunk.end());
        }
    }
    auto radiusOf = [&](u32 id) {
        for (const auto& c : classes) {
            if (c.id == id) return c.radius;
        }
        return 0.0f;
    };
    REQUIRE(all.size() > 400);
    for (size_t i = 0; i < all.size(); ++i) {
        for (size_t j = i + 1; j < all.size(); ++j) {
            const f32 dx = all[i].position.x - all[j].position.x;
            const f32 dz = all[i].position.z - all[j].position.z;
            const f32 reach = radiusOf(all[i].classId) + radiusOf(all[j].classId);
            REQUIRE(dx * dx + dz * dz >= reach * reach);
        }
    }
}

TEST_CASE("Scatter: high priority classes are not changed by the lower ones", "[scatter]") {
    auto classes = forest();
    const ScatterField field = hills();
    const auto full = scatterChunk(5, 0, 0, 80.0f, classes, field);
    std::vector<ScatterClass> rocksOnly = {classes[0]};
    const auto rocks = scatterChunk(5, 0, 0, 80.0f, rocksOnly, field);
    std::set<Key> inFull;
    for (const Instance& i : full) {
        if (i.classId == 1) inFull.insert({{i.classId, std::llround(i.position.x * 1000.0f)}, std::llround(i.position.z * 1000.0f)});
    }
    CHECK(keysOf(rocks) == inFull);
    // Lower classes leave room for the higher ones: grass count drops when rocks and trees exist.
    std::vector<ScatterClass> grassOnly = {classes[2]};
    const size_t freeGrass = scatterChunk(5, 0, 0, 80.0f, grassOnly, field).size();
    size_t withOthers = 0;
    for (const Instance& i : full) withOthers += i.classId == 3 ? 1u : 0u;
    CHECK(withOthers < freeGrass);
}

TEST_CASE("Scatter: density masks, ground conforming and variation", "[scatter]") {
    ScatterClass tree;
    tree.id = 2;
    tree.cellSize = 4.0f;
    tree.radius = 0.1f;
    tree.density = 1.0f;
    tree.variants = 5;
    tree.sink = 0.5f;
    tree.minScale = 0.5f;
    tree.maxScale = 2.0f;
    ScatterField field = hills();
    const size_t full = scatterChunk(1, 0, 0, 200.0f, {tree}, field).size();
    field.density = [](u32, f32 x, f32) { return x < 100.0f ? 1.0f : 0.0f; };
    const auto half = scatterChunk(1, 0, 0, 200.0f, {tree}, field);
    CHECK(half.size() > full / 2 - full / 10);
    CHECK(half.size() < full / 2 + full / 10);
    std::set<u32> variants;
    f32 minScale = 9.0f;
    f32 maxScale = 0.0f;
    for (const Instance& i : half) {
        CHECK(i.position.x < 100.0f);
        CHECK(i.position.y == Approx(hills().height(i.position.x, i.position.z) - 0.5f * i.scale));
        CHECK(i.up.length() == Approx(1.0f).epsilon(1e-4));
        CHECK(i.up.y > 0.9f);
        variants.insert(i.variant);
        minScale = std::min(minScale, i.scale);
        maxScale = std::max(maxScale, i.scale);
    }
    CHECK(variants.size() == 5);
    CHECK(minScale >= 0.5f);
    CHECK(maxScale <= 2.0f);
    CHECK(maxScale - minScale > 1.0f);
}

TEST_CASE("Scatter: chunks generated on several threads match the single-threaded result", "[scatter][determinism]") {
    const auto classes = forest();
    const ScatterField field = hills();
    constexpr int N = 16;
    auto run = [&](int threads) {
        std::vector<std::set<Key>> out(N);
        std::vector<std::thread> pool;
        for (int t = 0; t < threads; ++t) {
            pool.emplace_back([&, t] {
                for (int i = t; i < N; i += threads) {
                    out[static_cast<size_t>(i)] =
                        keysOf(scatterChunk(11, i % 4, i / 4, 32.0f, classes, field));
                }
            });
        }
        for (auto& th : pool) th.join();
        return out;
    };
    CHECK(run(1) == run(7));
}

TEST_CASE("Placement hash: version 1 is the legacy hash, version 2 the integer one", "[hash][determinism]") {
    using namespace Caffeine::Procedural;
    CHECK(placementHash(1, 3, -9, 1337, 7) == hash01(3, -9, 1337, 7));
    const f32 v2 = placementHash(2, 3, -9, 1337, 7);
    CHECK(v2 >= 0.0f);
    CHECK(v2 < 1.0f);
    CHECK(v2 == placementHash(2, 3, -9, 1337, 7));
    CHECK(v2 != placementHash(1, 3, -9, 1337, 7));
    // Different cells differ, and the values spread over the unit interval.
    f32 lo = 1.0f;
    f32 hi = 0.0f;
    for (int i = 0; i < 512; ++i) {
        const f32 h = placementHash(2, i, i * 3, 5, 1);
        lo = std::min(lo, h);
        hi = std::max(hi, h);
    }
    CHECK(lo < 0.05f);
    CHECK(hi > 0.95f);
}

// ---- scatter on a terrain: the core fed by the terrain's own height, normal and masks

namespace {

struct HillTerrain {
    Terrain::TerrainHeightmap map{129, 129};
    Terrain::TerrainFrame frame;
    Terrain::TerrainMasks masks;
    Terrain::BiomeTable biome;
    HillTerrain() {
        for (u32 z = 0; z < 129; ++z) {
            for (u32 x = 0; x < 129; ++x) {
                const f32 wx = static_cast<f32>(x) / 128.0f * 160.0f, wz = static_cast<f32>(z) / 128.0f * 160.0f;
                map.setNormalized(x, z, 0.4f + 0.15f * std::sin(wx * 0.05f) + 0.1f * std::cos(wz * 0.04f));
            }
        }
        frame = {&map, 160.0f, 160.0f, 50.0f};
        Terrain::BiomeTable::fromText("class 2 base 0.9\n  slope 0 0 0.2 0.5\n", biome);
    }
    TerrainScatterRequest request() const {
        TerrainScatterRequest r;
        r.seed = 11;
        r.frame = frame;
        r.masks = &masks;
        r.biome = &biome;
        ScatterClass tree;
        tree.id = 2;
        tree.priority = 20;
        tree.cellSize = 6.0f;
        tree.radius = 2.0f;
        tree.sink = 0.1f;
        ScatterClass grass;
        grass.id = 3;
        grass.priority = 5;
        grass.cellSize = 3.0f;
        grass.radius = 0.5f;
        r.classes = {tree, grass};
        return r;
    }
};

std::set<Key> keysOf(const TerrainScatterResult& result) { return keysOf(result.instances); }

}  // namespace

TEST_CASE("Terrain scatter: a pure function of the seed, whatever the chunking, and halves add up to the whole",
          "[scatter][terrain]") {
    const HillTerrain terrain;
    const TerrainScatterResult whole = scatterTerrain(terrain.request());
    REQUIRE(whole.instances.size() > 300u);
    CHECK_FALSE(whole.truncated);
    CHECK(whole.total == whole.instances.size());

    // The same request again gives the same instances, in the same order.
    const TerrainScatterResult again = scatterTerrain(terrain.request());
    REQUIRE(again.instances.size() == whole.instances.size());
    for (size_t i = 0; i < whole.instances.size(); ++i) {
        CHECK(again.instances[i].position.x == whole.instances[i].position.x);
        CHECK(again.instances[i].seed == whole.instances[i].seed);
    }
    // Another chunk size: the same set.
    TerrainScatterRequest other = terrain.request();
    other.chunkSize = 40.0f;
    CHECK(keysOf(scatterTerrain(other)) == keysOf(whole));
    // Two halves of the region: exactly the whole.
    TerrainScatterRequest left = terrain.request(), right = terrain.request();
    left.x0 = 0.0f; left.z0 = 0.0f; left.x1 = 77.0f; left.z1 = 160.0f;
    right.x0 = 77.0f; right.z0 = 0.0f; right.x1 = 160.0f; right.z1 = 160.0f;
    const auto a = keysOf(scatterTerrain(left)), b = keysOf(scatterTerrain(right));
    std::set<Key> joined = a;
    joined.insert(b.begin(), b.end());
    CHECK(joined == keysOf(whole));
    CHECK(a.size() + b.size() == whole.instances.size());  // nothing counted twice
    // A different seed is a different forest.
    TerrainScatterRequest reseeded = terrain.request();
    reseeded.seed = 12;
    CHECK(keysOf(scatterTerrain(reseeded)) != keysOf(whole));
}

TEST_CASE("Terrain scatter: instances sit on the terrain, follow the biome table and stop at the limit",
          "[scatter][terrain]") {
    HillTerrain terrain;
    const TerrainScatterResult result = scatterTerrain(terrain.request());
    for (const Instance& i : result.instances) {
        REQUIRE(i.position.x >= 0.0f);
        REQUIRE(i.position.x <= 160.0f);
        REQUIRE(i.position.z >= 0.0f);
        REQUIRE(i.position.z <= 160.0f);
        const f32 u = i.position.x / 160.0f, v = i.position.z / 160.0f;
        const f32 ground = terrain.map.sampleBilinear(u, v) * 50.0f;
        // On the surface, sunk by `sink` metres per unit of scale for the trees.
        const f32 sink = i.classId == 2 ? 0.1f * i.scale : 0.0f;
        CHECK(i.position.y == Approx(ground - sink).margin(0.02f));
        CHECK(i.up.length() == Approx(1.0f).margin(1e-3f));
        CHECK(i.up.y > 0.0f);
    }
    // Trees (class 2) obey the table (only where it is not steep); grass (class 3) has no rule and goes everywhere.
    size_t trees = 0, grass = 0;
    for (const Instance& i : result.instances) {
        f32 slope = 0.0f;
        REQUIRE(terrain.masks.sample(terrain.frame, "slope", i.position.x, i.position.z, slope));
        if (i.classId == 2) {
            ++trees;
            CHECK(slope < 0.5f);
        } else {
            ++grass;
        }
    }
    CHECK(trees > 50u);
    CHECK(grass > trees);

    // A limit cuts the list and says so, while `total` still counts everything.
    TerrainScatterRequest limited = terrain.request();
    limited.maxInstances = 100;
    const TerrainScatterResult cut = scatterTerrain(limited);
    CHECK(cut.truncated);
    CHECK(cut.instances.size() == 100u);
    CHECK(cut.total == result.total);
    // The kept ones are the first of the full list.
    for (size_t i = 0; i < cut.instances.size(); ++i) CHECK(cut.instances[i].position.x == result.instances[i].position.x);

    // Nothing to scatter: no classes, an empty region.
    TerrainScatterRequest none = terrain.request();
    none.classes.clear();
    CHECK(scatterTerrain(none).instances.empty());
    TerrainScatterRequest outside = terrain.request();
    outside.x0 = 500.0f; outside.x1 = 600.0f; outside.z0 = 0.0f; outside.z1 = 10.0f;
    CHECK(scatterTerrain(outside).instances.empty());
}
