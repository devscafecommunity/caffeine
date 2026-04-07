#include "catch.hpp"
#include "../src/math/Vec2.hpp"
#include "../src/math/Vec3.hpp"
#include "../src/math/Vec4.hpp"
#include "../src/math/Mat4.hpp"
#include "../src/math/Math.hpp"

using namespace Caffeine;
using namespace Caffeine::Math;

TEST_CASE("Vec2 - Basic Operations", "[math][vec2]") {
    Vec2 v1(1.0f, 2.0f);
    Vec2 v2(3.0f, 4.0f);

    Vec2 sum = v1 + v2;
    REQUIRE(sum.x == 4.0f);
    REQUIRE(sum.y == 6.0f);

    Vec2 diff = v2 - v1;
    REQUIRE(diff.x == 2.0f);
    REQUIRE(diff.y == 2.0f);

    Vec2 scaled = v1 * 2.0f;
    REQUIRE(scaled.x == 2.0f);
    REQUIRE(scaled.y == 4.0f);
}

TEST_CASE("Vec2 - Dot Product", "[math][vec2]") {
    Vec2 v1(1.0f, 2.0f);
    Vec2 v2(3.0f, 4.0f);

    float dot = v1.dot(v2);
    REQUIRE(dot == 11.0f);
}

TEST_CASE("Vec2 - Length", "[math][vec2]") {
    Vec2 v(3.0f, 4.0f);
    float len = v.length();
    REQUIRE(len == 5.0f);

    float lenSq = v.lengthSquared();
    REQUIRE(lenSq == 25.0f);
}

TEST_CASE("Vec2 - Normalization", "[math][vec2]") {
    Vec2 v(3.0f, 4.0f);
    Vec2 normalized = v.normalized();

    float len = normalized.length();
    REQUIRE(len == Approx(1.0f).epsilon(0.001f));
}

TEST_CASE("Vec3 - Basic Operations", "[math][vec3]") {
    Vec3 v1(1.0f, 2.0f, 3.0f);
    Vec3 v2(4.0f, 5.0f, 6.0f);

    Vec3 sum = v1 + v2;
    REQUIRE(sum.x == 5.0f);
    REQUIRE(sum.y == 7.0f);
    REQUIRE(sum.z == 9.0f);
}

TEST_CASE("Vec3 - Cross Product", "[math][vec3]") {
    Vec3 v1(1.0f, 0.0f, 0.0f);
    Vec3 v2(0.0f, 1.0f, 0.0f);

    Vec3 cross = v1.cross(v2);
    REQUIRE(cross.x == 0.0f);
    REQUIRE(cross.y == 0.0f);
    REQUIRE(cross.z == 1.0f);
}

TEST_CASE("Vec4 - Basic Operations", "[math][vec4]") {
    Vec4 v1(1.0f, 2.0f, 3.0f, 4.0f);
    Vec4 v2(5.0f, 6.0f, 7.0f, 8.0f);

    Vec4 sum = v1 + v2;
    REQUIRE(sum.x == 6.0f);
    REQUIRE(sum.y == 8.0f);
    REQUIRE(sum.z == 10.0f);
    REQUIRE(sum.w == 12.0f);

    float dot = v1.dot(v2);
    REQUIRE(dot == 70.0f);
}

TEST_CASE("Mat4 - Identity", "[math][mat4]") {
    Mat4 identity = Mat4::identity();

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            if (i == j) {
                REQUIRE(identity(i, j) == 1.0f);
            } else {
                REQUIRE(identity(i, j) == 0.0f);
            }
        }
    }
}

TEST_CASE("Mat4 - Multiply", "[math][mat4]") {
    Mat4 a = Mat4::identity();
    Mat4 b = Mat4::identity();

    Mat4 result = a * b;

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            if (i == j) {
                REQUIRE(result(i, j) == 1.0f);
            } else {
                REQUIRE(result(i, j) == 0.0f);
            }
        }
    }
}

TEST_CASE("Mat4 - Translation", "[math][mat4]") {
    Mat4 translation = Mat4::translation(5.0f, 10.0f, 15.0f);

    REQUIRE(translation(0, 3) == 5.0f);
    REQUIRE(translation(1, 3) == 10.0f);
    REQUIRE(translation(2, 3) == 15.0f);
}

TEST_CASE("Mat4 - Scale", "[math][mat4]") {
    Mat4 scale = Mat4::scale(2.0f);

    REQUIRE(scale(0, 0) == 2.0f);
    REQUIRE(scale(1, 1) == 2.0f);
    REQUIRE(scale(2, 2) == 2.0f);
    REQUIRE(scale(3, 3) == 1.0f);
}

TEST_CASE("Mat4 - Transform Point", "[math][mat4]") {
    Mat4 translation = Mat4::translation(10.0f, 20.0f, 30.0f);
    Vec3 point(1.0f, 2.0f, 3.0f);

    Vec3 transformed = translation.transformPoint(point);
    REQUIRE(transformed.x == 11.0f);
    REQUIRE(transformed.y == 22.0f);
    REQUIRE(transformed.z == 33.0f);
}

TEST_CASE("Math - Lerp", "[math]") {
    float result = lerp(0.0f, 10.0f, 0.5f);
    REQUIRE(result == 5.0f);

    result = lerp(0.0f, 10.0f, 0.0f);
    REQUIRE(result == 0.0f);

    result = lerp(0.0f, 10.0f, 1.0f);
    REQUIRE(result == 10.0f);
}

TEST_CASE("Math - Clamp", "[math]") {
    float result = clamp(5.0f, 0.0f, 10.0f);
    REQUIRE(result == 5.0f);

    result = clamp(-5.0f, 0.0f, 10.0f);
    REQUIRE(result == 0.0f);

    result = clamp(15.0f, 0.0f, 10.0f);
    REQUIRE(result == 10.0f);
}

TEST_CASE("Math - Saturate", "[math]") {
    float result = saturate(0.5f);
    REQUIRE(result == 0.5f);

    result = saturate(-0.5f);
    REQUIRE(result == 0.0f);

    result = saturate(1.5f);
    REQUIRE(result == 1.0f);
}

TEST_CASE("Math - DegToRad", "[math]") {
    float result = degToRad(0.0f);
    REQUIRE(result == 0.0f);

    result = degToRad(90.0f);
    REQUIRE(result == Approx(PI / 2.0f).epsilon(0.001f));

    result = degToRad(180.0f);
    REQUIRE(result == Approx(PI).epsilon(0.001f));
}

TEST_CASE("Math - IsPowerOfTwo", "[math]") {
    REQUIRE(isPowerOfTwo(1) == true);
    REQUIRE(isPowerOfTwo(2) == true);
    REQUIRE(isPowerOfTwo(4) == true);
    REQUIRE(isPowerOfTwo(8) == true);
    REQUIRE(isPowerOfTwo(3) == false);
    REQUIRE(isPowerOfTwo(5) == false);
    REQUIRE(isPowerOfTwo(0) == false);
}

TEST_CASE("Math - NextPowerOfTwo", "[math]") {
    REQUIRE(nextPowerOfTwo(1) == 1);
    REQUIRE(nextPowerOfTwo(2) == 2);
    REQUIRE(nextPowerOfTwo(3) == 4);
    REQUIRE(nextPowerOfTwo(5) == 8);
    REQUIRE(nextPowerOfTwo(15) == 16);
    REQUIRE(nextPowerOfTwo(0) == 1);
}