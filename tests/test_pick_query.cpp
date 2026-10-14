#include "catch.hpp"

#include "../src/ecs/World.hpp"
#include "../src/ecs/Components.hpp"
#include "../src/render/Camera2D.hpp"
#include "../src/scene/PickQuery.hpp"

#include <cmath>

using namespace Caffeine;
using namespace Caffeine::ECS;
using namespace Caffeine::Scene;

TEST_CASE("PickQuery - picks closest transform in world", "[pick]") {
    World world;
    Entity a = world.create("A");
    world.add<Transform>(a, Transform{{0.0f, 0.0f, 0.0f}, {0, 0, 0}, {1, 1, 1}});

    Entity b = world.create("B");
    world.add<Transform>(b, Transform{{5.0f, 0.0f, 0.0f}, {0, 0, 0}, {1, 1, 1}});

    Entity hit = pickEntityAtWorld(world, {0.1f, 0.1f});
    REQUIRE(hit.isValid());
    REQUIRE(hit.id() == a.id());

    hit = pickEntityAtWorld(world, {5.0f, 0.0f});
    REQUIRE(hit.isValid());
    REQUIRE(hit.id() == b.id());
}

TEST_CASE("PickQuery - screen to world uses PlayRenderContext", "[pick]") {
    PlayRenderContext::instance().clear();
    Render::Camera2D cam;
    cam.setViewport({{0, 0}, {800, 600}});
    cam.setPosition({10.0f, 20.0f});
    cam.setZoom(2.0f);
    PlayRenderContext::instance().set2D({{0, 0}, {800, 600}}, cam);

    const Vec2 w = PlayRenderContext::instance().screenToWorld({400, 300});
    REQUIRE(std::fabs(w.x - 10.0f) < 0.05f);
    REQUIRE(std::fabs(w.y - 20.0f) < 0.05f);
}
