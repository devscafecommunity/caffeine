#include "catch.hpp"

#ifdef CF_HAS_DRYAD

#include "../src/script/DryadHost.hpp"
#include "../src/ecs/World.hpp"
#include "../src/ecs/Components.hpp"

#include <cstdio>
#include <fstream>
#include <string>

using namespace Caffeine;
using namespace Caffeine::Script;

TEST_CASE("Dryad onUpdate sets entity position", "[dryad]") {
    const std::string path = "dryad_host_move.dyd";
    {
        std::ofstream out(path);
        out << "function onUpdate(entity, dt) {\n"
            << "  caffeine_set_position(entity, 3, 4, 5);\n"
            << "}\n";
    }

    ECS::World world;
    DryadHost host;
    host.setWorld(&world);
    ECS::Entity entity = world.create("DryadEntity");

    REQUIRE(host.load(path));
    REQUIRE(host.call(path, "onUpdate", entity, 0.016f));

    auto* transform = entity.get<ECS::Transform>();
    REQUIRE(transform != nullptr);
    REQUIRE(transform->position.x == 3.0f);
    REQUIRE(transform->position.y == 4.0f);
    REQUIRE(transform->position.z == 5.0f);

    std::remove(path.c_str());
}

#endif
