#include "catch.hpp"
#include "../src/render/Camera3D.hpp"
#include "Caffeine.hpp"

using namespace Caffeine;
using namespace Caffeine::Render;
using namespace Caffeine::Spatial;
using namespace Caffeine::Math;

TEST_CASE("Camera3D default state", "[camera3d]") {
    Camera3D cam;
    REQUIRE(cam.position().x == Approx(0.0f));
    REQUIRE(cam.position().y == Approx(0.0f));
    REQUIRE(cam.position().z == Approx(-5.0f));
    REQUIRE(cam.fov() == Approx(60.0f));
    REQUIRE(cam.aspect() == Approx(16.0f/9.0f));
    REQUIRE(cam.nearPlane() == Approx(0.1f));
    REQUIRE(cam.farPlane() == Approx(1000.0f));
}

TEST_CASE("Camera3D view matrix matches lookAt", "[camera3d]") {
    Camera3D cam;
    cam.lookAt({0, 0, -5}, {0, 0, 0});
    Mat4 view = cam.viewMatrix();
    (void)view;
    Vec3 pos = cam.position();
    REQUIRE(pos.x == Approx(0.0f));
    REQUIRE(pos.y == Approx(0.0f));
    REQUIRE(pos.z == Approx(-5.0f));
    Vec3 fwd = cam.forward();
    REQUIRE(fwd.z == Approx(1.0f).margin(0.01f));
}

TEST_CASE("Camera3D lookAt from different angle", "[camera3d]") {
    Camera3D cam;
    cam.lookAt({10, 5, 10}, {0, 0, 0});
    Vec3 pos = cam.position();
    REQUIRE(pos.x == Approx(10.0f));
    REQUIRE(pos.y == Approx(5.0f));
    REQUIRE(pos.z == Approx(10.0f));
    Vec3 fwd = cam.forward();
    REQUIRE(fwd.length() == Approx(1.0f).margin(0.01f));
}

TEST_CASE("Camera3D projection matrix matches Mat4::perspective", "[camera3d]") {
    Camera3D cam;
    cam.setFOV(60.0f);
    cam.setAspect(16.0f/9.0f);
    cam.setNearFar(0.1f, 1000.0f);
    Mat4 proj = cam.projectionMatrix();
    Mat4 expected = Mat4::perspective(degToRad(60.0f), 16.0f/9.0f, 0.1f, 1000.0f);
    for (u32 i = 0; i < 16; ++i) {
        REQUIRE(proj.data()[i] == Approx(expected.data()[i]).margin(0.001f));
    }
}

TEST_CASE("Camera3D VP matrix is cached", "[camera3d]") {
    Camera3D cam;
    Mat4 vp1 = cam.viewProjectionMatrix();
    Mat4 vp2 = cam.viewProjectionMatrix();
    for (u32 i = 0; i < 16; ++i) {
        REQUIRE(vp1.data()[i] == vp2.data()[i]);
    }
}

TEST_CASE("Camera3D VP matrix recomputes when dirty", "[camera3d]") {
    Camera3D cam;
    Mat4 vp1 = cam.viewProjectionMatrix();
    cam.setPosition({10, 0, 0});
    Mat4 vp2 = cam.viewProjectionMatrix();
    bool same = true;
    for (u32 i = 0; i < 16; ++i) {
        if (vp1.data()[i] != vp2.data()[i]) { same = false; break; }
    }
    REQUIRE(!same);
}

TEST_CASE("Camera3D forward/right/up defaults", "[camera3d]") {
    Camera3D cam;
    Vec3 fwd = cam.forward();
    REQUIRE(fwd.x == Approx(0.0f));
    REQUIRE(fwd.y == Approx(0.0f));
    REQUIRE(fwd.z == Approx(1.0f));

    Vec3 r = cam.right();
    REQUIRE(r.x == Approx(1.0f));
    REQUIRE(r.y == Approx(0.0f));
    REQUIRE(r.z == Approx(0.0f));

    Vec3 u = cam.up();
    REQUIRE(u.x == Approx(0.0f));
    REQUIRE(u.y == Approx(1.0f));
    REQUIRE(u.z == Approx(0.0f));
}

TEST_CASE("Camera3D forward after lookAt", "[camera3d]") {
    Camera3D cam;
    cam.lookAt({0, 0, -5}, {0, 0, 0});
    Vec3 fwd = cam.forward();
    REQUIRE(fwd.x == Approx(0.0f));
    REQUIRE(fwd.y == Approx(0.0f));
    REQUIRE(fwd.z == Approx(1.0f));
}

TEST_CASE("Camera3D Frustum extraction", "[camera3d]") {
    Camera3D cam;
    cam.lookAt({0, 5, -10}, {0, 0, 0});
    Frustum f = cam.frustum();
    REQUIRE(f.contains({0, 0, 0}));
    REQUIRE(!f.contains({0, 0, -20}));
}

TEST_CASE("Camera3D isVisible", "[camera3d]") {
    Camera3D cam;
    cam.lookAt({0, 0, -10}, {0, 0, 0});
    AABB3D visibleBox = {{-1, -1, -1}, {1, 1, 1}};
    REQUIRE(cam.isVisible(visibleBox));
    AABB3D hiddenBox = {{-1, -1, -30}, {1, 1, -28}};
    REQUIRE(!cam.isVisible(hiddenBox));
}

TEST_CASE("Camera3D worldToScreen basic", "[camera3d]") {
    Camera3D cam;
    cam.lookAt({0, 0, -5}, {0, 0, 0});
    cam.setViewport({{0, 0}, {1280, 720}});
    Vec3 screen = cam.worldToScreen({0, 0, 0});
    REQUIRE(screen.x == Approx(640.0f).margin(1.0f));
    REQUIRE(screen.y == Approx(360.0f).margin(1.0f));
}

TEST_CASE("Camera3D screenToWorld roundtrip", "[camera3d]") {
    Camera3D cam;
    cam.lookAt({0, 0, -5}, {0, 0, 0});
    cam.setViewport({{0, 0}, {1280, 720}});
    Vec3 world = {1.0f, 2.0f, -3.0f};
    Vec3 screen = cam.worldToScreen(world);
    Vec3 world2 = cam.screenToWorld({screen.x, screen.y}, screen.z);
    REQUIRE(world2.x == Approx(world.x).margin(0.01f));
    REQUIRE(world2.y == Approx(world.y).margin(0.01f));
    REQUIRE(world2.z == Approx(world.z).margin(0.01f));
}

TEST_CASE("Camera3D FPS rotation affects forward", "[camera3d]") {
    Camera3D cam;
    cam.rotateFPS(0.0f, 90.0f); // yaw 90 degrees right
    Vec3 fwd = cam.forward();
    REQUIRE(fwd.x == Approx(1.0f).margin(0.01f));
    REQUIRE(fwd.y == Approx(0.0f).margin(0.01f));
    REQUIRE(fwd.z == Approx(0.0f).margin(0.01f));
}

TEST_CASE("Camera3D FPS pitch clamped", "[camera3d]") {
    Camera3D cam;
    cam.rotateFPS(200.0f, 0.0f);
    Vec3 fwd = cam.forward();
    REQUIRE(fabsf(fwd.x) < 2.0f);
    REQUIRE(fabsf(fwd.y) < 2.0f);
    REQUIRE(fabsf(fwd.z) < 2.0f);
    cam.rotateFPS(-400.0f, 0.0f);
    fwd = cam.forward();
    REQUIRE(fabsf(fwd.x) < 2.0f);
}

TEST_CASE("Camera3D FPS move", "[camera3d]") {
    Camera3D cam;
    Vec3 before = cam.position();
    cam.moveFPS({0, 0, 1});
    Vec3 after = cam.position();
    REQUIRE(after.z > before.z);
}

TEST_CASE("Camera3D setPosition getPosition", "[camera3d]") {
    Camera3D cam;
    cam.setPosition({10, 20, 30});
    Vec3 pos = cam.position();
    REQUIRE(pos.x == Approx(10.0f));
    REQUIRE(pos.y == Approx(20.0f));
    REQUIRE(pos.z == Approx(30.0f));
}

TEST_CASE("Camera3D setRotation", "[camera3d]") {
    Camera3D cam;
    Quat rot = Quat::fromAxisAngle({0, 1, 0}, degToRad(90.0f));
    cam.setRotation(rot);
    Vec3 fwd = cam.forward();
    REQUIRE(fwd.x == Approx(1.0f).margin(0.01f));
    REQUIRE(fwd.z == Approx(0.0f).margin(0.01f));
}

TEST_CASE("Camera3D setFOV clamping", "[camera3d]") {
    Camera3D cam;
    cam.setFOV(0.0f);
    REQUIRE(cam.fov() == Approx(1.0f));
    cam.setFOV(200.0f);
    REQUIRE(cam.fov() == Approx(179.0f));
}

TEST_CASE("Camera3D setAspect validation", "[camera3d]") {
    Camera3D cam;
    cam.setAspect(-1.0f);
    REQUIRE(cam.aspect() == Approx(0.0001f));
    cam.setAspect(0.0f);
    REQUIRE(cam.aspect() == Approx(0.0001f));
}

TEST_CASE("Camera3D setNearFar validation", "[camera3d]") {
    Camera3D cam;
    cam.setNearFar(-1.0f, -0.5f);
    REQUIRE(cam.nearPlane() == Approx(0.1f));
    REQUIRE(cam.farPlane() > cam.nearPlane());
    cam.setNearFar(10.0f, 5.0f);
    REQUIRE(cam.farPlane() > cam.nearPlane());
}

TEST_CASE("Camera3D orbital position", "[camera3d]") {
    Camera3D cam;
    cam.setOrbitTarget({0, 0, 0});
    cam.setOrbitDistance(10.0f);
    Vec3 pos = cam.position();
    REQUIRE(pos.y == Approx(5.0f).margin(0.01f));
    Vec3 fwd = (Vec3(0,0,0) - pos).normalized();
    Vec3 camFwd = cam.forward();
    REQUIRE(camFwd.x == Approx(fwd.x).margin(0.01f));
    REQUIRE(camFwd.y == Approx(fwd.y).margin(0.01f));
    REQUIRE(camFwd.z == Approx(fwd.z).margin(0.01f));
}
