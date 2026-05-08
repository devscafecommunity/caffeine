#include "catch.hpp"
#include "../src/render/Camera2D.hpp"
#include "../src/Caffeine.hpp"

#include <cmath>

using namespace Caffeine;
using namespace Caffeine::Render;
using namespace Caffeine::ECS;

static constexpr f32 kEps = 0.01f;

static bool approxEq(f32 a, f32 b, f32 eps = kEps) {
    return fabsf(a - b) < eps;
}

TEST_CASE("Camera2D - default construction", "[camera2d]") {
    Camera2D cam;
    REQUIRE(approxEq(cam.position().x, 0.0f));
    REQUIRE(approxEq(cam.position().y, 0.0f));
    REQUIRE(approxEq(cam.zoom(), 1.0f));
    REQUIRE(approxEq(cam.rotation(), 0.0f));
    REQUIRE(approxEq(cam.viewport().size.x, 1280.0f));
    REQUIRE(approxEq(cam.viewport().size.y, 720.0f));
}

TEST_CASE("Camera2D - setPosition / position", "[camera2d]") {
    Camera2D cam;
    cam.setPosition({ 100.0f, 200.0f });
    REQUIRE(approxEq(cam.position().x, 100.0f));
    REQUIRE(approxEq(cam.position().y, 200.0f));
}

TEST_CASE("Camera2D - setZoom / zoom", "[camera2d]") {
    Camera2D cam;
    cam.setZoom(2.0f);
    REQUIRE(approxEq(cam.zoom(), 2.0f));
}

TEST_CASE("Camera2D - setZoom clamps to positive", "[camera2d]") {
    Camera2D cam;
    cam.setZoom(-1.0f);
    REQUIRE(cam.zoom() > 0.0f);
}

TEST_CASE("Camera2D - setRotation / rotation", "[camera2d]") {
    Camera2D cam;
    cam.setRotation(45.0f);
    REQUIRE(approxEq(cam.rotation(), 45.0f));
}

TEST_CASE("Camera2D - size returns viewport divided by zoom", "[camera2d]") {
    Camera2D cam;
    cam.setZoom(2.0f);
    Vec2 s = cam.size();
    REQUIRE(approxEq(s.x, 640.0f));
    REQUIRE(approxEq(s.y, 360.0f));
}

TEST_CASE("Camera2D - viewProjectionMatrix does not crash", "[camera2d]") {
    Camera2D cam;
    Mat4 vp = cam.viewProjectionMatrix();
    (void)vp;
    SUCCEED();
}

TEST_CASE("Camera2D - dirty flag: VP matrix not recalculated when camera is still", "[camera2d]") {
    Camera2D cam;
    Mat4 first  = cam.viewProjectionMatrix();
    Mat4 second = cam.viewProjectionMatrix();
    for (int i = 0; i < 16; ++i) {
        REQUIRE(approxEq(first.data()[i], second.data()[i]));
    }
}

TEST_CASE("Camera2D - dirty flag: VP matrix recalculates after move", "[camera2d]") {
    Camera2D cam;
    Mat4 before = cam.viewProjectionMatrix();
    cam.setPosition({ 500.0f, 300.0f });
    Mat4 after = cam.viewProjectionMatrix();
    bool changed = false;
    for (int i = 0; i < 16; ++i) {
        if (!approxEq(before.data()[i], after.data()[i])) { changed = true; break; }
    }
    REQUIRE(changed);
}

TEST_CASE("Camera2D - worldToScreen / screenToWorld round-trip (no rotation, origin)", "[camera2d]") {
    Camera2D cam;
    Vec2 world  = { 0.0f, 0.0f };
    Vec2 screen = cam.worldToScreen(world);
    Vec2 back   = cam.screenToWorld(screen);
    REQUIRE(approxEq(back.x, world.x));
    REQUIRE(approxEq(back.y, world.y));
}

TEST_CASE("Camera2D - worldToScreen / screenToWorld round-trip (offset position)", "[camera2d]") {
    Camera2D cam;
    cam.setPosition({ 200.0f, 150.0f });
    Vec2 world  = { 300.0f, 400.0f };
    Vec2 screen = cam.worldToScreen(world);
    Vec2 back   = cam.screenToWorld(screen);
    REQUIRE(approxEq(back.x, world.x, 0.1f));
    REQUIRE(approxEq(back.y, world.y, 0.1f));
}

TEST_CASE("Camera2D - worldToScreen / screenToWorld round-trip (zoom)", "[camera2d]") {
    Camera2D cam;
    cam.setZoom(2.0f);
    Vec2 world  = { 50.0f, -30.0f };
    Vec2 screen = cam.worldToScreen(world);
    Vec2 back   = cam.screenToWorld(screen);
    REQUIRE(approxEq(back.x, world.x, 0.1f));
    REQUIRE(approxEq(back.y, world.y, 0.1f));
}

TEST_CASE("Camera2D - worldToScreen / screenToWorld round-trip (rotation 30deg)", "[camera2d]") {
    Camera2D cam;
    cam.setRotation(30.0f);
    Vec2 world  = { 100.0f, 50.0f };
    Vec2 screen = cam.worldToScreen(world);
    Vec2 back   = cam.screenToWorld(screen);
    REQUIRE(approxEq(back.x, world.x, 0.1f));
    REQUIRE(approxEq(back.y, world.y, 0.1f));
}

TEST_CASE("Camera2D - world origin maps to screen center (no offset)", "[camera2d]") {
    Camera2D cam;
    Vec2 screen = cam.worldToScreen({ 0.0f, 0.0f });
    REQUIRE(approxEq(screen.x, 640.0f));
    REQUIRE(approxEq(screen.y, 360.0f));
}

TEST_CASE("Camera2D - isVisible returns true for overlapping rect", "[camera2d]") {
    Camera2D cam;
    Rect2D rect { { -100.0f, -100.0f }, { 200.0f, 200.0f } };
    REQUIRE(cam.isVisible(rect));
}

TEST_CASE("Camera2D - isVisible returns false for rect outside frustum", "[camera2d]") {
    Camera2D cam;
    Rect2D rect { { 2000.0f, 2000.0f }, { 100.0f, 100.0f } };
    REQUIRE_FALSE(cam.isVisible(rect));
}

TEST_CASE("Camera2D - shake produces non-zero offset after update", "[camera2d]") {
    Camera2D cam;
    cam.shake({ 10.0f, 10.0f }, 1.0f);

    bool offset_nonzero = false;
    for (int i = 0; i < 20; ++i) {
        cam.update(0.016, World{});
        Vec2 s = cam.worldToScreen({ 0.0f, 0.0f });
        if (!approxEq(s.x, 640.0f, 0.5f) || !approxEq(s.y, 360.0f, 0.5f)) {
            offset_nonzero = true;
            break;
        }
    }
    REQUIRE(offset_nonzero);
}

TEST_CASE("Camera2D - shake decays to zero after duration elapses", "[camera2d]") {
    Camera2D cam;
    cam.shake({ 10.0f, 10.0f }, 0.1f);
    cam.update(0.2, World{});
    Vec2 screen = cam.worldToScreen({ 0.0f, 0.0f });
    REQUIRE(approxEq(screen.x, 640.0f, kEps));
    REQUIRE(approxEq(screen.y, 360.0f, kEps));
}

TEST_CASE("Camera2D - setBounds clamps position", "[camera2d]") {
    Camera2D cam;
    cam.setViewport({ { 0.0f, 0.0f }, { 200.0f, 200.0f } });
    cam.setZoom(1.0f);
    cam.setBounds({ { 0.0f, 0.0f }, { 500.0f, 500.0f } });
    cam.setPosition({ -500.0f, -500.0f });
    REQUIRE(cam.position().x >= 0.0f);
    REQUIRE(cam.position().y >= 0.0f);
}

TEST_CASE("Camera2D - follow + update moves camera toward entity", "[camera2d]") {
    World world;
    Entity e = world.create("player");
    world.add<Position2D>(e, Position2D{ 1000.0f, 500.0f });

    Camera2D cam;
    cam.follow(e, 0.5f);

    f32 startX = cam.position().x;
    cam.update(0.016, world);

    REQUIRE(cam.position().x > startX);
}

TEST_CASE("Camera2D - stopFollowing stops camera movement", "[camera2d]") {
    World world;
    Entity e = world.create("player");
    world.add<Position2D>(e, Position2D{ 1000.0f, 500.0f });

    Camera2D cam;
    cam.follow(e, 0.5f);
    cam.stopFollowing();

    cam.update(0.016, world);
    REQUIRE(approxEq(cam.position().x, 0.0f));
    REQUIRE(approxEq(cam.position().y, 0.0f));
}
