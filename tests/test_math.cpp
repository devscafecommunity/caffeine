#include "catch.hpp"
#include "../src/math/Vec2.hpp"
#include "../src/math/Vec3.hpp"
#include "../src/math/Vec4.hpp"
#include "../src/math/Mat4.hpp"
#include "../src/math/Math.hpp"
#include "../src/math/Quat.hpp"

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
    REQUIRE(len == Approx(1.0f).margin(0.001f));
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
    float result = Math::lerp(0.0f, 10.0f, 0.5f);
    REQUIRE(result == 5.0f);

    result = Math::lerp(0.0f, 10.0f, 0.0f);
    REQUIRE(result == 0.0f);

    result = Math::lerp(0.0f, 10.0f, 1.0f);
    REQUIRE(result == 10.0f);
}

TEST_CASE("Math - Clamp", "[math]") {
    float result = Math::clamp(5.0f, 0.0f, 10.0f);
    REQUIRE(result == 5.0f);

    result = Math::clamp(-5.0f, 0.0f, 10.0f);
    REQUIRE(result == 0.0f);

    result = Math::clamp(15.0f, 0.0f, 10.0f);
    REQUIRE(result == 10.0f);
}

TEST_CASE("Math - Saturate", "[math]") {
    float result = Math::saturate(0.5f);
    REQUIRE(result == 0.5f);

    result = Math::saturate(-0.5f);
    REQUIRE(result == 0.0f);

    result = Math::saturate(1.5f);
    REQUIRE(result == 1.0f);
}

TEST_CASE("Math - DegToRad", "[math]") {
    float result = Math::degToRad(0.0f);
    REQUIRE(result == 0.0f);

    result = Math::degToRad(90.0f);
    REQUIRE(result == Approx(PI / 2.0f).margin(0.001f));

    result = Math::degToRad(180.0f);
    REQUIRE(result == Approx(PI).margin(0.001f));
}

TEST_CASE("Math - IsPowerOfTwo", "[math]") {
    REQUIRE(Math::isPowerOfTwo(1) == true);
    REQUIRE(Math::isPowerOfTwo(2) == true);
    REQUIRE(Math::isPowerOfTwo(4) == true);
    REQUIRE(Math::isPowerOfTwo(8) == true);
    REQUIRE(Math::isPowerOfTwo(3) == false);
    REQUIRE(Math::isPowerOfTwo(5) == false);
    REQUIRE(Math::isPowerOfTwo(0) == false);
}

TEST_CASE("Math - NextPowerOfTwo", "[math]") {
    REQUIRE(Math::nextPowerOfTwo(1) == 1);
    REQUIRE(Math::nextPowerOfTwo(2) == 2);
    REQUIRE(Math::nextPowerOfTwo(3) == 4);
    REQUIRE(Math::nextPowerOfTwo(5) == 8);
    REQUIRE(Math::nextPowerOfTwo(15) == 16);
    REQUIRE(Math::nextPowerOfTwo(0) == 1);
}

TEST_CASE("Quat - Identity Values", "[math][quat]") {
    Quat identity = Quat::identity();
    REQUIRE(identity.x == 0.0f);
    REQUIRE(identity.y == 0.0f);
    REQUIRE(identity.z == 0.0f);
    REQUIRE(identity.w == 1.0f);
}

TEST_CASE("Quat - Default Constructor Equals Identity", "[math][quat]") {
    Quat q;
    Quat identity = Quat::identity();
    REQUIRE(q == identity);
}

TEST_CASE("Quat - FromAxisAngle Zero Degrees", "[math][quat]") {
    Quat q = Quat::fromAxisAngle(Vec3(0, 1, 0), 0.0f);
    Quat identity = Quat::identity();
    REQUIRE(q.x == Approx(identity.x).margin(0.001f));
    REQUIRE(q.y == Approx(identity.y).margin(0.001f));
    REQUIRE(q.z == Approx(identity.z).margin(0.001f));
    REQUIRE(q.w == Approx(identity.w).margin(0.001f));
}

TEST_CASE("Quat - FromAxisAngle 90 Degrees Y Rotates X to Z", "[math][quat]") {
    Quat q = Quat::fromAxisAngle(Vec3(0, 1, 0), PI / 2.0f);
    Vec3 rotated = q.rotate(Vec3(1, 0, 0));
    REQUIRE(rotated.x == Approx(0.0f).margin(0.001f));
    REQUIRE(rotated.y == Approx(0.0f).margin(0.001f));
    REQUIRE(rotated.z == Approx(-1.0f).margin(0.001f));
}

TEST_CASE("Quat - FromEuler Zero Returns Identity", "[math][quat]") {
    Quat q = Quat::fromEuler(0.0f, 0.0f, 0.0f);
    Quat identity = Quat::identity();
    REQUIRE(q.x == Approx(identity.x).margin(0.001f));
    REQUIRE(q.y == Approx(identity.y).margin(0.001f));
    REQUIRE(q.z == Approx(identity.z).margin(0.001f));
    REQUIRE(q.w == Approx(identity.w).margin(0.001f));
}

TEST_CASE("Quat - FromEuler Pitch 90 Degrees", "[math][quat]") {
    Quat q = Quat::fromEuler(PI / 2.0f, 0.0f, 0.0f);
    Vec3 rotated = q.rotate(Vec3(0, 1, 0));
    REQUIRE(rotated.x == Approx(0.0f).margin(0.001f));
    REQUIRE(rotated.y == Approx(0.0f).margin(0.001f));
    REQUIRE(rotated.z == Approx(1.0f).margin(0.001f));
}

TEST_CASE("Quat - Quaternion Composition", "[math][quat]") {
    Quat q1 = Quat::fromAxisAngle(Vec3(0, 1, 0), PI / 2.0f);
    Quat q2 = Quat::fromAxisAngle(Vec3(0, 1, 0), PI / 2.0f);
    Quat result = q1 * q2;
    Vec3 rotated = result.rotate(Vec3(1, 0, 0));
    REQUIRE(rotated.x == Approx(-1.0f).margin(0.001f));
    REQUIRE(rotated.y == Approx(0.0f).margin(0.001f));
    REQUIRE(rotated.z == Approx(0.0f).margin(0.001f));
}

TEST_CASE("Quat - Quaternion Times Vec3", "[math][quat]") {
    Quat q = Quat::fromAxisAngle(Vec3(0, 1, 0), PI / 2.0f);
    Vec3 v(1, 0, 0);
    Vec3 rotated = q * v;
    REQUIRE(rotated.x == Approx(0.0f).margin(0.001f));
    REQUIRE(rotated.y == Approx(0.0f).margin(0.001f));
    REQUIRE(rotated.z == Approx(-1.0f).margin(0.001f));
}

TEST_CASE("Quat - Conjugate of Identity is Identity", "[math][quat]") {
    Quat identity = Quat::identity();
    Quat conj = identity.conjugate();
    REQUIRE(conj == identity);
}

TEST_CASE("Quat - Conjugate Negates XYZ", "[math][quat]") {
    Quat q(1, 0, 0, 0);
    Quat conj = q.conjugate();
    REQUIRE(conj.x == -1.0f);
    REQUIRE(conj.y == 0.0f);
    REQUIRE(conj.z == 0.0f);
    REQUIRE(conj.w == 0.0f);
}

TEST_CASE("Quat - Length of Identity is One", "[math][quat]") {
    Quat identity = Quat::identity();
    REQUIRE(identity.length() == Approx(1.0f).margin(0.001f));
}

TEST_CASE("Quat - Length of Normalized is One", "[math][quat]") {
    Quat q(2, 0, 0, 0);
    Quat normalized = q.normalized();
    REQUIRE(normalized.length() == Approx(1.0f).margin(0.001f));
}

TEST_CASE("Quat - Normalized 2,0,0,0 Returns 1,0,0,0", "[math][quat]") {
    Quat q(2, 0, 0, 0);
    Quat normalized = q.normalized();
    REQUIRE(normalized.x == Approx(1.0f).margin(0.001f));
    REQUIRE(normalized.y == Approx(0.0f).margin(0.001f));
    REQUIRE(normalized.z == Approx(0.0f).margin(0.001f));
    REQUIRE(normalized.w == Approx(0.0f).margin(0.001f));
}

TEST_CASE("Quat - Inverse of Identity is Identity", "[math][quat]") {
    Quat identity = Quat::identity();
    Quat inv = identity.inverse();
    REQUIRE(inv.x == Approx(identity.x).margin(0.001f));
    REQUIRE(inv.y == Approx(identity.y).margin(0.001f));
    REQUIRE(inv.z == Approx(identity.z).margin(0.001f));
    REQUIRE(inv.w == Approx(identity.w).margin(0.001f));
}

TEST_CASE("Quat - Q Times Q Inverse Equals Identity", "[math][quat]") {
    Quat q = Quat::fromAxisAngle(Vec3(1, 1, 1).normalized(), PI / 3.0f);
    Quat inv = q.inverse();
    Quat result = q * inv;
    Quat identity = Quat::identity();
    REQUIRE(result.x == Approx(identity.x).margin(0.001f));
    REQUIRE(result.y == Approx(identity.y).margin(0.001f));
    REQUIRE(result.z == Approx(identity.z).margin(0.001f));
    REQUIRE(result.w == Approx(identity.w).margin(0.001f));
}

TEST_CASE("Quat - SLERP of Same Quaternion Returns Same", "[math][quat]") {
    Quat q = Quat::fromAxisAngle(Vec3(0, 1, 0), PI / 4.0f);
    Quat result = Quat::slerp(q, q, 0.5f);
    REQUIRE(result.x == Approx(q.x).margin(0.001f));
    REQUIRE(result.y == Approx(q.y).margin(0.001f));
    REQUIRE(result.z == Approx(q.z).margin(0.001f));
    REQUIRE(result.w == Approx(q.w).margin(0.001f));
}

TEST_CASE("Quat - SLERP t=0 Returns First, t=1 Returns Second", "[math][quat]") {
    Quat a = Quat::fromAxisAngle(Vec3(0, 1, 0), 0.0f);
    Quat b = Quat::fromAxisAngle(Vec3(0, 1, 0), PI / 2.0f);
    
    Quat result0 = Quat::slerp(a, b, 0.0f);
    REQUIRE(result0.x == Approx(a.x).margin(0.001f));
    REQUIRE(result0.y == Approx(a.y).margin(0.001f));
    REQUIRE(result0.z == Approx(a.z).margin(0.001f));
    REQUIRE(result0.w == Approx(a.w).margin(0.001f));
    
    Quat result1 = Quat::slerp(a, b, 1.0f);
    REQUIRE(result1.x == Approx(b.x).margin(0.001f));
    REQUIRE(result1.y == Approx(b.y).margin(0.001f));
    REQUIRE(result1.z == Approx(b.z).margin(0.001f));
    REQUIRE(result1.w == Approx(b.w).margin(0.001f));
}

TEST_CASE("Quat - NLERP t=0 Returns First Normalized", "[math][quat]") {
    Quat a = Quat::fromAxisAngle(Vec3(0, 1, 0), 0.0f);
    Quat b = Quat::fromAxisAngle(Vec3(0, 1, 0), PI / 2.0f);
    
    Quat result = Quat::nlerp(a, b, 0.0f);
    Quat aNorm = a.normalized();
    REQUIRE(result.x == Approx(aNorm.x).margin(0.001f));
    REQUIRE(result.y == Approx(aNorm.y).margin(0.001f));
    REQUIRE(result.z == Approx(aNorm.z).margin(0.001f));
    REQUIRE(result.w == Approx(aNorm.w).margin(0.001f));
}

TEST_CASE("Quat - ToMatrix Identity Returns Identity Matrix", "[math][quat]") {
    Quat identity = Quat::identity();
    Mat4 m = identity.toMatrix();
    Mat4 identityMat = Mat4::identity();
    
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            REQUIRE(m(i, j) == Approx(identityMat(i, j)).margin(0.001f));
        }
    }
}

TEST_CASE("Quat - ToMatrix Matches Mat4::rotationY", "[math][quat]") {
    f32 angle = PI / 3.0f;
    Quat q = Quat::fromAxisAngle(Vec3(0, 1, 0), angle);
    Mat4 quatMat = q.toMatrix();
    Mat4 mat4Rot = Mat4::rotationY(angle);
    
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            REQUIRE(quatMat(i, j) == Approx(mat4Rot(i, j)).margin(0.001f));
        }
    }
}

TEST_CASE("Quat - ToEuler Identity Returns Zero", "[math][quat]") {
    Quat identity = Quat::identity();
    Vec3 euler = identity.toEuler();
    REQUIRE(euler.x == Approx(0.0f).margin(0.001f));
    REQUIRE(euler.y == Approx(0.0f).margin(0.001f));
    REQUIRE(euler.z == Approx(0.0f).margin(0.001f));
}

TEST_CASE("Quat - FromMatrix Identity Returns Identity Quat", "[math][quat]") {
    Mat4 identity = Mat4::identity();
    Quat q = Quat::fromMatrix(identity);
    Quat identityQuat = Quat::identity();
    REQUIRE(q.x == Approx(identityQuat.x).margin(0.001f));
    REQUIRE(q.y == Approx(identityQuat.y).margin(0.001f));
    REQUIRE(q.z == Approx(identityQuat.z).margin(0.001f));
    REQUIRE(q.w == Approx(identityQuat.w).margin(0.001f));
}

TEST_CASE("Quat - LookAt Forward Z Up Y Returns Identity", "[math][quat]") {
    Vec3 forward(0, 0, 1);
    Vec3 up(0, 1, 0);
    Quat q = Quat::lookAt(forward, up);
    Vec3 testVec = q.rotate(Vec3(1, 0, 0));
    REQUIRE(testVec.length() == Approx(1.0f).margin(0.001f));
}

TEST_CASE("Quat - FromEuler ToEuler Roundtrip", "[math][quat]") {
    f32 pitch = PI / 6.0f;
    f32 yaw = PI / 4.0f;
    f32 roll = PI / 3.0f;
    
    Quat q = Quat::fromEuler(pitch, yaw, roll);
    Vec3 euler = q.toEuler();
    
    REQUIRE(euler.x == Approx(pitch).margin(0.001f));
    REQUIRE(euler.y == Approx(yaw).margin(0.001f));
    REQUIRE(euler.z == Approx(roll).margin(0.001f));
}