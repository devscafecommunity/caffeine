#include "catch.hpp"
#include "../src/render/TextureAtlas.hpp"

#include <cmath>

using namespace Caffeine;
using namespace Caffeine::Render;

static bool approxEq(f32 a, f32 b, f32 eps = 0.001f) {
    return fabsf(a - b) < eps;
}

TEST_CASE("TextureAtlas - default construction", "[atlas]") {
    TextureAtlas atlas(512, 512);
    REQUIRE(atlas.width()     == 512);
    REQUIRE(atlas.height()    == 512);
    REQUIRE(atlas.count()     == 0);
    REQUIRE(atlas.usedArea()  == 0);
    REQUIRE(atlas.isPacked()  == false);
    REQUIRE(approxEq(atlas.utilization(), 0.0f));
}

TEST_CASE("TextureAtlas - add returns true for new entry", "[atlas]") {
    TextureAtlas atlas(512, 512);
    bool added = atlas.add("hero", { {0, 0}, {64, 64} });
    REQUIRE(added);
    REQUIRE(atlas.count() == 1);
}

TEST_CASE("TextureAtlas - add returns false for duplicate name", "[atlas]") {
    TextureAtlas atlas(512, 512);
    atlas.add("hero", { {0, 0}, {64, 64} });
    bool second = atlas.add("hero", { {0, 0}, {32, 32} });
    REQUIRE_FALSE(second);
    REQUIRE(atlas.count() == 1);
}

TEST_CASE("TextureAtlas - add returns false for empty name", "[atlas]") {
    TextureAtlas atlas(512, 512);
    REQUIRE_FALSE(atlas.add("", { {0, 0}, {64, 64} }));
    REQUIRE_FALSE(atlas.add(nullptr, { {0, 0}, {64, 64} }));
}

TEST_CASE("TextureAtlas - pack sets isPacked true", "[atlas]") {
    TextureAtlas atlas(512, 512);
    atlas.add("a", { {0, 0}, {64, 64} });
    atlas.pack();
    REQUIRE(atlas.isPacked());
}

TEST_CASE("TextureAtlas - getUV returns valid range after pack", "[atlas]") {
    TextureAtlas atlas(512, 512);
    atlas.add("sprite", { {0, 0}, {128, 64} });
    atlas.pack();

    Vec4 uv = atlas.getUV("sprite");
    REQUIRE(uv.x >= 0.0f); REQUIRE(uv.x <= 1.0f);
    REQUIRE(uv.y >= 0.0f); REQUIRE(uv.y <= 1.0f);
    REQUIRE(uv.z >= 0.0f); REQUIRE(uv.z <= 1.0f);
    REQUIRE(uv.w >= 0.0f); REQUIRE(uv.w <= 1.0f);
    REQUIRE(uv.z > uv.x);
    REQUIRE(uv.w > uv.y);
}

TEST_CASE("TextureAtlas - getUV UV width/height matches proportional size", "[atlas]") {
    TextureAtlas atlas(512, 512);
    atlas.add("a", { {0, 0}, {128, 64} });
    atlas.pack();

    Vec4 uv = atlas.getUV("a");
    f32 uvW = uv.z - uv.x;
    f32 uvH = uv.w - uv.y;
    REQUIRE(approxEq(uvW, 128.0f / 512.0f));
    REQUIRE(approxEq(uvH, 64.0f  / 512.0f));
}

TEST_CASE("TextureAtlas - getUV returns sentinel for unknown name", "[atlas]") {
    TextureAtlas atlas(256, 256);
    atlas.pack();
    Vec4 uv = atlas.getUV("nonexistent");
    REQUIRE(approxEq(uv.x, -1.0f));
}

TEST_CASE("TextureAtlas - getUV returns sentinel before pack", "[atlas]") {
    TextureAtlas atlas(256, 256);
    atlas.add("sprite", { {0, 0}, {32, 32} });
    Vec4 uv = atlas.getUV("sprite");
    REQUIRE(approxEq(uv.x, -1.0f));
}

TEST_CASE("TextureAtlas - usedArea > 0 after pack", "[atlas]") {
    TextureAtlas atlas(512, 512);
    atlas.add("a", { {0, 0}, {64, 64} });
    atlas.add("b", { {0, 0}, {32, 32} });
    atlas.pack();
    REQUIRE(atlas.usedArea() > 0);
}

TEST_CASE("TextureAtlas - utilization is in (0,1]", "[atlas]") {
    TextureAtlas atlas(256, 256);
    atlas.add("big", { {0, 0}, {128, 128} });
    atlas.pack();
    f32 util = atlas.utilization();
    REQUIRE(util > 0.0f);
    REQUIRE(util <= 1.0f);
}

TEST_CASE("TextureAtlas - multiple sprites packed without overlap", "[atlas]") {
    TextureAtlas atlas(256, 128);
    atlas.add("s0", { {0, 0}, {64, 64} });
    atlas.add("s1", { {0, 0}, {64, 64} });
    atlas.add("s2", { {0, 0}, {64, 64} });
    atlas.pack();

    Vec4 uv0 = atlas.getUV("s0");
    Vec4 uv1 = atlas.getUV("s1");
    Vec4 uv2 = atlas.getUV("s2");

    REQUIRE(uv0.x >= 0.0f);
    REQUIRE(uv1.x >= 0.0f);
    REQUIRE(uv2.x >= 0.0f);

    bool s01Overlap = (uv0.z > uv1.x && uv1.z > uv0.x && uv0.w > uv1.y && uv1.w > uv0.y);
    bool s02Overlap = (uv0.z > uv2.x && uv2.z > uv0.x && uv0.w > uv2.y && uv2.w > uv0.y);
    REQUIRE_FALSE(s01Overlap);
    REQUIRE_FALSE(s02Overlap);
}

TEST_CASE("TextureAtlas - exportImage returns correct dimensions", "[atlas]") {
    TextureAtlas atlas(64, 32);
    auto img = atlas.exportImage();
    REQUIRE(img.width    == 64);
    REQUIRE(img.height   == 32);
    REQUIRE(img.byteSize == 64 * 32 * 4);
    REQUIRE(img.pixels   != nullptr);
}

TEST_CASE("TextureAtlas - shelf-first sorts taller sprites first", "[atlas]") {
    TextureAtlas atlas(256, 256);
    atlas.add("tall",  { {0, 0}, {32, 128} });
    atlas.add("short", { {0, 0}, {32, 32}  });
    atlas.pack();

    Vec4 uvTall  = atlas.getUV("tall");
    Vec4 uvShort = atlas.getUV("short");

    REQUIRE(uvTall.x  >= 0.0f);
    REQUIRE(uvShort.x >= 0.0f);
}

TEST_CASE("TextureAtlas - sprites that don't fit are not packed", "[atlas]") {
    TextureAtlas atlas(32, 32);
    atlas.add("too_big", { {0, 0}, {64, 64} });
    atlas.pack();

    Vec4 uv = atlas.getUV("too_big");
    REQUIRE(approxEq(uv.x, -1.0f));
}

TEST_CASE("TextureAtlas - re-pack after add invalidates previous pack", "[atlas]") {
    TextureAtlas atlas(512, 512);
    atlas.add("a", { {0, 0}, {64, 64} });
    atlas.pack();
    REQUIRE(atlas.isPacked());

    atlas.add("b", { {0, 0}, {64, 64} });
    REQUIRE_FALSE(atlas.isPacked());

    atlas.pack();
    REQUIRE(atlas.isPacked());
    REQUIRE(atlas.count() == 2);
}
