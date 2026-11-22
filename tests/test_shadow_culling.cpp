#include "catch.hpp"

#include "../src/caffeine/math/Mat4.hpp"
#include "../src/caffeine/render/MicromeshGpu.hpp"
#include "../src/caffeine/render/ShadowCulling.hpp"

using namespace Caffeine;
using namespace Caffeine::Render;

// What a sun cascade leaves out: a caster wholly outside its sides is not submitted into it. The tests build a
// cascade the way the renderer does (an orthographic box looking down) and check which boxes are skipped.

namespace {

/// An orthographic cascade covering x, y in [-10, 10] and z in [-50, 50] of its own space.
std::array<Vec4, 6> cascade() {
    const Mat4 proj = Mat4::ortho(-10.0f, 10.0f, -10.0f, 10.0f, -50.0f, 50.0f);
    return planesFromViewProj(proj);
}

}  // namespace

TEST_CASE("Shadow culling: a box inside the cascade is kept", "[shadow]") {
    const auto planes = cascade();
    CHECK_FALSE(outsideCascadeSides(planes, Vec3(-1, -1, -1), Vec3(1, 1, 1)));
    CHECK_FALSE(outsideCascadeSides(planes, Vec3(-10, -10, -3), Vec3(10, 10, 3)));  // exactly the cascade
    // A huge box that spans the cascade (a terrain, a sea) is kept.
    CHECK_FALSE(outsideCascadeSides(planes, Vec3(-1000, -1000, -1000), Vec3(1000, 1000, 1000)));
}

TEST_CASE("Shadow culling: a box wholly beyond one side is skipped", "[shadow]") {
    const auto planes = cascade();
    CHECK(outsideCascadeSides(planes, Vec3(11, -1, -1), Vec3(14, 1, 1)));    // right
    CHECK(outsideCascadeSides(planes, Vec3(-14, -1, -1), Vec3(-11, 1, 1)));  // left
    CHECK(outsideCascadeSides(planes, Vec3(-1, 12, -1), Vec3(1, 15, 1)));    // above
    CHECK(outsideCascadeSides(planes, Vec3(-1, -15, -1), Vec3(1, -12, 1)));  // below
    // Far away along the light's direction but inside the sides: not the sides' business, so kept.
    CHECK_FALSE(outsideCascadeSides(planes, Vec3(-1, -1, 80), Vec3(1, 1, 90)));
}

TEST_CASE("Shadow culling: a box that reaches into the cascade is kept, even at a corner", "[shadow]") {
    const auto planes = cascade();
    CHECK_FALSE(outsideCascadeSides(planes, Vec3(9, -1, -1), Vec3(14, 1, 1)));   // crosses the right side
    CHECK_FALSE(outsideCascadeSides(planes, Vec3(9, 9, -1), Vec3(14, 14, 1)));   // the corner region
    CHECK_FALSE(outsideCascadeSides(planes, Vec3(10.0f, -1, -1), Vec3(12, 1, 1)));  // touching the side
    // Beyond a corner: outside both the right and the top side.
    CHECK(outsideCascadeSides(planes, Vec3(11, 11, -1), Vec3(14, 14, 1)));
}

TEST_CASE("Shadow culling: a rotated cascade culls along its own axes", "[shadow]") {
    // The same box turned 45 degrees about the vertical axis: its half-width of 10 is 14 along the world X axis
    // (10 / cos 45), so a box at x = 17 is outside and one at x = 8 is inside.
    const Mat4 view = Mat4::rotationY(0.7853982f);
    const Mat4 proj = Mat4::ortho(-10.0f, 10.0f, -10.0f, 10.0f, -50.0f, 50.0f);
    const auto planes = planesFromViewProj(proj * view);
    CHECK_FALSE(outsideCascadeSides(planes, Vec3(7.0f, -1, -1), Vec3(8.0f, 1, 1)));
    CHECK(outsideCascadeSides(planes, Vec3(17.0f, -1, -1), Vec3(18.0f, 1, 1)));
    CHECK_FALSE(outsideCascadeSides(planes, Vec3(12.0f, -1, -1), Vec3(13.0f, 1, 1)));
    // The world Y axis is the rotation's axis: a tall box on it stays, one far to the side along Z goes.
    CHECK_FALSE(outsideCascadeSides(planes, Vec3(-1, -5, -1), Vec3(1, 5, 1)));
    CHECK(outsideCascadeSides(planes, Vec3(-1, -1, 30), Vec3(1, 1, 33)));
}
