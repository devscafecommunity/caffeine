#include "catch.hpp"

#include "navigation/NavVolume.hpp"
#include "navigation/NavigationSystem.hpp"
#include "ecs/Components3D.hpp"
#include "ecs/World.hpp"

using namespace Caffeine;
using namespace Caffeine::ECS;
using namespace Caffeine::Navigation;

TEST_CASE("NavVolume walks around a blocked column", "[navigation]") {
    NavVolume volume;
    volume.resize(5, 5);
    volume.cellSize = 1.0f;
    for (int y = 0; y < 5; ++y) volume.setBlocked(2, y, true);
    volume.setBlocked(2, 4, false);

    std::vector<Vec3> path;
    REQUIRE(volume.findPath(Vec3(0.5f, 0.0f, 0.5f), Vec3(4.5f, 0.0f, 0.5f), path));
    REQUIRE(path.size() > 2);
    REQUIRE(path.front().x == Approx(0.5f));
    REQUIRE(path.back().x == Approx(4.5f));
    for (const Vec3& point : path) {
        i32 x = 0, z = 0;
        volume.worldToCell(point, x, z);
        REQUIRE_FALSE(volume.isBlocked(x, z));
    }
}

TEST_CASE("Nav agent follows a path on the XZ plane", "[navigation]") {
    World world;
    Entity ground = world.create();
    NavVolume volume;
    volume.resize(8, 4);
    volume.cellSize = 1.0f;
    world.add<NavVolume>(ground, volume);

    Entity agentEntity = world.create();
    world.add<Position3D>(agentEntity, Position3D{Vec3(0.5f, 0.0f, 0.5f)});
    NavAgent agent;
    agent.mode = NavMode::Scripted;
    agent.destination = Vec3(4.5f, 0.0f, 0.5f);
    agent.hasDestination = true;
    agent.speed = 10.0f;
    agent.arriveRadius = 0.2f;
    world.add<NavAgent>(agentEntity, agent);

    updateNavigation(world, 1.0f);
    const Position3D* position = world.get<Position3D>(agentEntity);
    REQUIRE(position != nullptr);
    REQUIRE(position->position.x > 0.5f);
}
