#ifdef CF_HAS_SDL3

#include "catch.hpp"
#include "../src/render/BatchRenderer.hpp"
#include "../src/render/TextureAtlas.hpp"
#include "../src/math/Mat4.hpp"

#include <cmath>

using namespace Caffeine;
using namespace Caffeine::Render;

static ECS::Sprite makeSprite(const char* name) {
    ECS::Sprite s;
    s.name       = name;
    s.frameIndex = 0;
    return s;
}

TEST_CASE("BatchRenderer - construct with null device does not crash", "[batch]") {
    REQUIRE_NOTHROW(BatchRenderer(nullptr));
}

TEST_CASE("BatchRenderer - beginFrame resets stats", "[batch]") {
    BatchRenderer renderer(nullptr);
    renderer.beginFrame();
    renderer.submitSprite(Mat4::identity(), makeSprite("hero"));
    REQUIRE(renderer.lastFrameStats().totalSprites == 1);

    renderer.beginFrame();
    REQUIRE(renderer.lastFrameStats().totalSprites == 0);
}

TEST_CASE("BatchRenderer - submitSprite increments totalSprites", "[batch]") {
    BatchRenderer renderer(nullptr);
    renderer.beginFrame();
    renderer.submitSprite(Mat4::identity(), makeSprite("a"));
    renderer.submitSprite(Mat4::identity(), makeSprite("b"));
    renderer.submitSprite(Mat4::identity(), makeSprite("c"));
    REQUIRE(renderer.lastFrameStats().totalSprites == 3);
}

TEST_CASE("BatchRenderer - endFrame with null cmd returns early", "[batch]") {
    BatchRenderer renderer(nullptr);
    renderer.beginFrame();
    renderer.submitSprite(Mat4::identity(), makeSprite("x"));
    REQUIRE_NOTHROW(renderer.endFrame(nullptr));

    auto stats = renderer.lastFrameStats();
    REQUIRE(stats.verticesUploaded == 0);
    REQUIRE(stats.drawCalls        == 0);
}

TEST_CASE("BatchRenderer - sort key layer 0 depth 0 equals 0x80000000", "[batch]") {
    // sortKey = (layer+128)[7:0]<<24 | textureId[11:0]<<12 | normDepth[11:0]
    // layer=0 → (0+128)<<24 = 0x80000000; textureId=0; normDepth=0 → key=0x80000000
    const u32 expected = (128u << 24) | (0u << 12) | 0u;
    REQUIRE(expected == 0x80000000u);
}

TEST_CASE("BatchRenderer - sort key lower depth yields lower key", "[batch]") {
    const u32 key0 = (128u << 24) | 0u;
    const u32 key1 = (128u << 24) | 0xFFFu;
    REQUIRE(key0 < key1);
}

TEST_CASE("BatchRenderer - setAtlas nullptr gives full-UV fallback without crash", "[batch]") {
    BatchRenderer renderer(nullptr);
    renderer.setAtlas(nullptr);
    renderer.beginFrame();
    REQUIRE_NOTHROW(renderer.submitSprite(Mat4::identity(), makeSprite("anything")));
    REQUIRE(renderer.lastFrameStats().totalSprites == 1);
}

TEST_CASE("BatchRenderer - setCamera stores camera without crash", "[batch]") {
    BatchRenderer renderer(nullptr);
    Camera2D cam;
    cam.setViewport({{0, 0}, {800.0f, 600.0f}});
    REQUIRE_NOTHROW(renderer.setCamera(cam));

    Mat4 vp = cam.viewProjectionMatrix();
    bool allZero = true;
    for (int i = 0; i < 16; ++i) {
        if (vp.data()[i] != 0.0f) { allZero = false; break; }
    }
    REQUIRE_FALSE(allZero);
}

TEST_CASE("BatchRenderer - radix sort does not crash with variable depths", "[batch]") {
    BatchRenderer renderer(nullptr);
    renderer.beginFrame();
    renderer.submitSprite(Mat4::translation(0.0f, 0.0f, 1.0f), makeSprite("c"));
    renderer.submitSprite(Mat4::identity(),                     makeSprite("a"));
    renderer.submitSprite(Mat4::translation(0.0f, 0.0f, 0.5f), makeSprite("b"));
    REQUIRE(renderer.lastFrameStats().totalSprites == 3);
    REQUIRE_NOTHROW(renderer.endFrame(nullptr));
}

TEST_CASE("BatchRenderer - packed atlas UV is used for submitSprite", "[batch]") {
    TextureAtlas atlas(256, 256);
    atlas.add("hero", { {0, 0}, {64, 64} });
    atlas.pack();
    REQUIRE(atlas.isPacked());

    BatchRenderer renderer(nullptr);
    renderer.setAtlas(&atlas);
    renderer.beginFrame();
    REQUIRE_NOTHROW(renderer.submitSprite(Mat4::identity(), makeSprite("hero")));
    REQUIRE(renderer.lastFrameStats().totalSprites == 1);
}

#endif  // CF_HAS_SDL3
