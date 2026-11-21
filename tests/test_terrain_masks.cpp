#include "catch.hpp"

#include "scatter/Scatter.hpp"
#include "terrain/TerrainBiome.hpp"
#include "terrain/TerrainHeightmap.hpp"
#include "terrain/TerrainMasks.hpp"
#include "terrain/TerrainSerializer.hpp"
#include "terrain/TerrainSplatmap.hpp"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

using namespace Caffeine;
using namespace Caffeine::Terrain;

namespace {

/// A heightfield made from a function of (x, z) metres: 65 x 65 vertices over 64 m, 10 m of height range.
struct Field {
    TerrainHeightmap map{65, 65};
    TerrainFrame frame;
    Field(const std::function<f32(f32, f32)>& heightMetres) {
        for (u32 z = 0; z < 65; ++z) {
            for (u32 x = 0; x < 65; ++x) {
                map.setNormalized(x, z, std::clamp(heightMetres(static_cast<f32>(x), static_cast<f32>(z)) / 10.0f, 0.0f, 1.0f));
            }
        }
        frame.heightmap = &map;
        frame.sizeX = 64.0f;
        frame.sizeZ = 64.0f;
        frame.maxHeight = 10.0f;
    }
};

f32 maskAt(const TerrainMasks& masks, const TerrainFrame& frame, const char* name, f32 x, f32 z) {
    f32 v = -1.0f;
    REQUIRE(masks.sample(frame, name, x, z, v));
    return v;
}

}  // namespace

TEST_CASE("Terrain masks: the built-in masks are derived from the heightfield", "[terrain][masks]") {
    TerrainMasks masks;
    // A ramp rising 0.5 m per metre in x (about 26.6 degrees) up to 10 m at x = 20, then flat.
    Field ramp([](f32 x, f32) { return std::min(x * 0.5f, 10.0f); });
    CHECK(maskAt(masks, ramp.frame, "height", 10.0f, 5.0f) == Approx(0.5f).margin(0.01f));
    CHECK(maskAt(masks, ramp.frame, "height", 40.0f, 5.0f) == Approx(1.0f).margin(0.001f));
    const f32 angle = std::atan(0.5f) / 1.57079632679f;
    CHECK(maskAt(masks, ramp.frame, "slope", 10.0f, 30.0f) == Approx(angle).margin(0.01f));
    CHECK(maskAt(masks, ramp.frame, "slope", 50.0f, 30.0f) == Approx(0.0f).margin(0.01f));
    // A plane has no curvature: 0.5. A bowl is concave (> 0.5), a hill convex (< 0.5).
    CHECK(maskAt(masks, ramp.frame, "curvature", 10.0f, 30.0f) == Approx(0.5f).margin(0.01f));
    Field bowl([](f32 x, f32 z) { return 0.004f * ((x - 32.0f) * (x - 32.0f) + (z - 32.0f) * (z - 32.0f)); });
    Field hill([](f32 x, f32 z) { return 10.0f - 0.004f * ((x - 32.0f) * (x - 32.0f) + (z - 32.0f) * (z - 32.0f)); });
    // The Laplacian of 0.004 r^2 is 0.016 per metre: 0.5 + 0.5 * 0.016 / 0.125 = 0.564.
    CHECK(maskAt(masks, bowl.frame, "curvature", 32.0f, 32.0f) == Approx(0.564f).margin(0.01f));
    CHECK(maskAt(masks, hill.frame, "curvature", 32.0f, 32.0f) == Approx(0.436f).margin(0.01f));

    // The normal of the ramp: (-0.5, 1, 0) normalised.
    const Vec3 n = TerrainMasks::normalAt(ramp.frame, 10.0f, 30.0f);
    CHECK(n.x == Approx(-0.5f / std::sqrt(1.25f)).margin(0.01f));
    CHECK(n.y == Approx(1.0f / std::sqrt(1.25f)).margin(0.01f));
    CHECK(n.length() == Approx(1.0f).margin(1e-4f));
    // Outside the terrain reads the nearest edge instead of failing.
    CHECK(maskAt(masks, ramp.frame, "height", -500.0f, 5.0f) == Approx(0.0f).margin(0.001f));
    f32 unused = 0.0f;
    CHECK_FALSE(masks.sample(ramp.frame, "no_such_mask", 1.0f, 1.0f, unused));
}

TEST_CASE("Terrain masks: stored masks are sampled bilinearly, clamped, and resampled over a rectangle",
          "[terrain][masks]") {
    TerrainMasks masks;
    Field flat([](f32, f32) { return 1.0f; });
    // A 3 x 3 mask: 0 at the left edge, 128 in the middle column, 255 at the right edge.
    const u8 data[9] = {0, 128, 255, 0, 128, 255, 0, 128, 255};
    REQUIRE(masks.set("wetness", 3, 3, data));
    CHECK(masks.has("wetness"));
    CHECK(masks.resolutionX("wetness") == 3u);
    CHECK(maskAt(masks, flat.frame, "wetness", 0.0f, 10.0f) == Approx(0.0f).margin(1e-4f));
    CHECK(maskAt(masks, flat.frame, "wetness", 32.0f, 10.0f) == Approx(128.0f / 255.0f).margin(1e-3f));
    CHECK(maskAt(masks, flat.frame, "wetness", 64.0f, 10.0f) == Approx(1.0f).margin(1e-4f));
    CHECK(maskAt(masks, flat.frame, "wetness", 16.0f, 10.0f) == Approx(64.0f / 255.0f).margin(1e-3f));  // between texels
    CHECK(maskAt(masks, flat.frame, "wetness", 500.0f, -500.0f) == Approx(1.0f).margin(1e-4f));      // clamped

    // A rectangle over the whole terrain at 5 x 2 texels matches the point samples at the texel centres.
    u8 grid[10];
    REQUIRE(masks.rect(flat.frame, "wetness", 0.0f, 0.0f, 64.0f, 64.0f, 5, 2, grid));
    for (u32 j = 0; j < 2; ++j) {
        for (u32 i = 0; i < 5; ++i) {
            const f32 x = 64.0f * (static_cast<f32>(i) + 0.5f) / 5.0f, z = 64.0f * (static_cast<f32>(j) + 0.5f) / 2.0f;
            CHECK(static_cast<int>(grid[j * 5 + i]) ==
                  static_cast<int>(std::lround(maskAt(masks, flat.frame, "wetness", x, z) * 255.0f)));
        }
    }
    // Built-ins can be read over a rectangle too.
    u8 heights[4];
    Field ramp([](f32 x, f32) { return x * 0.15f; });
    REQUIRE(masks.rect(ramp.frame, "height", 0.0f, 0.0f, 64.0f, 64.0f, 2, 2, heights));
    CHECK(heights[0] < heights[1]);

    // Limits and names.
    CHECK_FALSE(masks.set("height", 3, 3, data));      // a built-in name
    CHECK_FALSE(masks.set("has space", 3, 3, data));   // a bad name
    CHECK_FALSE(masks.set("", 3, 3, data));
    CHECK_FALSE(masks.set("tiny", 1, 3, data));        // a grid under 2 x 2
    CHECK_FALSE(masks.set("x", 3, 3, nullptr));
    for (int i = 1; i < 16; ++i) CHECK(masks.set("m" + std::to_string(i), 3, 3, data));
    CHECK_FALSE(masks.set("one_too_many", 3, 3, data));  // 16 stored (wetness + 15)
    CHECK(masks.set("wetness", 3, 3, data));              // replacing is not adding
    CHECK(masks.count() == TerrainMasks::kBuiltinCount + 16u);
    CHECK(masks.nameAt(0) == "height");
    CHECK(masks.nameAt(TerrainMasks::kBuiltinCount) == "wetness");
    CHECK(masks.remove("m3"));
    CHECK_FALSE(masks.has("m3"));
    CHECK(masks.set("one_more", 3, 3, data));
}

TEST_CASE("Terrain masks: the terrain file keeps them, and a file without masks stays version 2",
          "[terrain][masks]") {
    TerrainHeightmap map(9, 9);
    map.fill(0.25f);
    TerrainSplatmap splat;
    splat.resize(9, 9);
    splat.fillLayer(0, 1.0f);
    TerrainMasks masks;
    std::vector<u8> wear(25);
    for (size_t i = 0; i < wear.size(); ++i) wear[i] = static_cast<u8>(i * 10);
    REQUIRE(masks.set("wear", 5, 5, wear.data()));
    REQUIRE(masks.set("flow", 5, 5, wear.data()));

    const auto dir = std::filesystem::temp_directory_path() / "caffeine_terrain_masks";
    std::filesystem::create_directories(dir);
    const auto withMasks = dir / "with.terrain";
    const auto without = dir / "without.terrain";
    REQUIRE(TerrainSerializer::save(withMasks, map, splat, true, &masks));
    REQUIRE(TerrainSerializer::save(without, map, splat, true));
    auto versionOf = [](const std::filesystem::path& path) {
        std::ifstream in(path, std::ios::binary);
        u32 header[2] = {};
        in.read(reinterpret_cast<char*>(header), sizeof(header));
        return header[1];
    };
    CHECK(versionOf(withMasks) == 3u);
    CHECK(versionOf(without) == 2u);  // older readers still open it

    TerrainHeightmap loadedMap;
    TerrainSplatmap loadedSplat;
    TerrainMasks loaded;
    bool hadSplat = false;
    REQUIRE(TerrainSerializer::load(withMasks, loadedMap, loadedSplat, &hadSplat, &loaded));
    CHECK(hadSplat);
    CHECK(loadedMap.resolutionX() == 9u);
    REQUIRE(loaded.stored().size() == 2u);
    CHECK(loaded.stored()[0].name == "wear");
    CHECK(loaded.stored()[1].name == "flow");
    CHECK(loaded.stored()[0].data == wear);
    CHECK(loaded.resolutionX("flow") == 5u);

    // A file without masks loads with an empty set (and clears what was there).
    TerrainMasks other;
    REQUIRE(other.set("stale", 3, 3, wear.data()));
    REQUIRE(TerrainSerializer::load(without, loadedMap, loadedSplat, &hadSplat, &other));
    CHECK(other.empty());

    // A truncated masks section is refused, never read out of range.
    std::filesystem::path cut = dir / "cut.terrain";
    {
        std::ifstream in(withMasks, std::ios::binary | std::ios::ate);
        const std::streamsize size = in.tellg();
        in.seekg(0);
        std::vector<char> bytes(static_cast<size_t>(size) - 7);
        in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        std::ofstream out(cut, std::ios::binary);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    TerrainMasks refused;
    CHECK_FALSE(TerrainSerializer::load(cut, loadedMap, loadedSplat, &hadSplat, &refused));
    std::filesystem::remove_all(dir);
}

TEST_CASE("Biome table: trapezoid terms, the text format and its errors", "[terrain][biome]") {
    BiomeTerm term{"slope", 0.1f, 0.2f, 0.5f, 0.7f};
    CHECK(term.weight(0.0f) == Approx(0.0f).margin(1e-6f));
    CHECK(term.weight(0.1f) == Approx(0.0f).margin(1e-6f));
    CHECK(term.weight(0.15f) == Approx(0.5f).margin(1e-5f));  // smoothstep midpoint
    CHECK(term.weight(0.3f) == Approx(1.0f).margin(1e-6f));
    CHECK(term.weight(0.6f) == Approx(0.5f).margin(1e-5f));
    CHECK(term.weight(0.9f) == Approx(0.0f).margin(1e-6f));
    // Equal edges are a step.
    BiomeTerm step{"height", 0.3f, 0.3f, 1.0f, 1.0f};
    CHECK(step.weight(0.29f) == 0.0f);
    CHECK(step.weight(0.31f) == 1.0f);

    const std::string text =
        "# forest\n"
        "class 2 base 0.9   # trees\n"
        "  slope 0 0 0.15 0.35\n"
        "  wetness 0 0 0.3 0.6\n"
        "class 3\n"
        "  height 0.1 0.2 1 1\n";
    BiomeTable table;
    std::string error;
    REQUIRE(BiomeTable::fromText(text, table, &error));
    REQUIRE(table.rules().size() == 2u);
    CHECK(table.find(2)->base == Approx(0.9f));
    CHECK(table.find(2)->terms.size() == 2u);
    CHECK(table.find(3)->base == Approx(1.0f));
    CHECK(table.find(9) == nullptr);
    // Writing and reading again gives the same table.
    BiomeTable again;
    REQUIRE(BiomeTable::fromText(table.toText(), again, &error));
    CHECK(again.toText() == table.toText());

    for (const char* bad : {"slope 0 0 1 1\n", "class x\n", "class 1 base\n", "class 1\n slope 0 0 1\n",
                            "class 1\n slope 0.5 0.2 1 1\n", "class 1\n bad name 0 0 1 1\n", "class 1 base 1 junk\n"}) {
        BiomeTable out;
        std::string why;
        INFO(bad);
        CHECK_FALSE(BiomeTable::fromText(bad, out, &why));
        CHECK_FALSE(why.empty());
    }
    // Setting a class twice replaces it; removing works.
    table.set(BiomeRule{2, 0.5f, {}});
    CHECK(table.rules().size() == 2u);
    CHECK(table.find(2)->terms.empty());
    CHECK(table.remove(3));
    CHECK_FALSE(table.remove(3));
}

TEST_CASE("Biome table: scatter places each class where its masks allow", "[terrain][biome][scatter]") {
    // A 200 m terrain: a plateau with a steep ridge across the middle and a wet hollow in one corner.
    TerrainHeightmap map(129, 129);
    for (u32 z = 0; z < 129; ++z) {
        for (u32 x = 0; x < 129; ++x) {
            const f32 wx = static_cast<f32>(x) / 128.0f * 200.0f;
            const f32 ridge = 0.5f / (1.0f + std::exp(-(wx - 100.0f) / 4.0f));  // a steep step at x = 100 m
            map.setNormalized(x, z, 0.3f + ridge);
        }
    }
    TerrainFrame frame{&map, 200.0f, 200.0f, 60.0f};
    TerrainMasks masks;
    std::vector<u8> wet(64 * 64, 0);
    for (u32 z = 0; z < 20; ++z) {
        for (u32 x = 0; x < 20; ++x) wet[z * 64 + x] = 255;  // the corner (0..60 m, 0..60 m)
    }
    REQUIRE(masks.set("wetness", 64, 64, wet.data()));

    BiomeTable table;
    REQUIRE(BiomeTable::fromText(
        "class 2 base 1\n  slope 0 0 0.12 0.3\n  wetness 0 0 0.2 0.5\n"   // trees: gentle and dry
        "class 1 base 1\n  slope 0.3 0.45 1 1\n"                            // rocks: steep
        "class 4 base 1\n  nothing_here 0 0 1 1\n",                         // a mask that does not exist
        table));
    CHECK(table.missingMasks(masks) == std::vector<std::string>{"nothing_here"});

    Scatter::ScatterField field;
    field.height = [&](f32 x, f32 z) {
        const f32 u = std::clamp(x / 200.0f, 0.0f, 1.0f), v = std::clamp(z / 200.0f, 0.0f, 1.0f);
        return map.sampleBilinear(u, v) * 60.0f;
    };
    field.normal = [&](f32 x, f32 z) { return TerrainMasks::normalAt(frame, x, z); };
    field.density = table.densityFunction(masks, frame);

    Scatter::ScatterClass rock, tree, ghost;
    rock.id = 1; rock.priority = 30; rock.cellSize = 6.0f; rock.radius = 1.5f;
    tree.id = 2; tree.priority = 20; tree.cellSize = 5.0f; tree.radius = 2.0f;
    ghost.id = 4; ghost.priority = 10; ghost.cellSize = 5.0f; ghost.radius = 1.0f;
    const std::vector<Scatter::ScatterClass> classes = {rock, tree, ghost};

    size_t trees = 0, rocks = 0, ghosts = 0;
    for (i32 cz = 0; cz < 4; ++cz) {
        for (i32 cx = 0; cx < 4; ++cx) {
            for (const Scatter::Instance& i : Scatter::scatterChunk(7, cx, cz, 50.0f, classes, field)) {
                if (i.position.x < 0.0f || i.position.x > 200.0f || i.position.z < 0.0f || i.position.z > 200.0f) continue;
                f32 slope = 0.0f, wetness = 0.0f;
                REQUIRE(masks.sample(frame, "slope", i.position.x, i.position.z, slope));
                REQUIRE(masks.sample(frame, "wetness", i.position.x, i.position.z, wetness));
                if (i.classId == 2) {
                    ++trees;
                    CHECK(slope < 0.3f);       // never where it is steep
                    CHECK(wetness < 0.5f);     // never in the wet hollow
                } else if (i.classId == 1) {
                    ++rocks;
                    CHECK(slope > 0.3f);       // only on the ridge
                } else if (i.classId == 4) {
                    ++ghosts;
                }
            }
        }
    }
    CHECK(trees > 40u);
    CHECK(rocks > 3u);
    CHECK(ghosts == 0u);  // an unknown mask places nothing
}
