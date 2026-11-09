#include "catch.hpp"

#ifdef CF_HAS_DRYAD

#include "../src/caffeine/script/DryadHost.hpp"
#include "../src/caffeine/ecs/Components.hpp"
#include "../src/caffeine/ecs/Components3D.hpp"
#include "../src/caffeine/ecs/MeshComponents.hpp"
#include "../src/caffeine/ecs/World.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
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

TEST_CASE("Dryad onUpdate moves the 3D Position3D component", "[dryad][3d]") {
    const std::string path = "dryad_host_move_3d.dyd";
    {
        std::ofstream out(path);
        out << "function onUpdate(entity, dt) {\n"
            << "  caffeine_set_position(entity, 1, 2, 3);\n"
            << "}\n";
    }

    ECS::World world;
    DryadHost host;
    host.setWorld(&world);
    ECS::Entity entity = world.create("Dryad3DEntity");
    entity.getOrAdd<ECS::Position3D>().position = Vec3(0, 0, 0);
    entity.getOrAdd<ECS::MeshFilterComponent>();

    REQUIRE(host.load(path));
    REQUIRE(host.call(path, "onUpdate", entity, 0.016f));

    auto* position = entity.get<ECS::Position3D>();
    REQUIRE(position != nullptr);
    REQUIRE(position->position.x == 1.0f);
    REQUIRE(position->position.y == 2.0f);
    REQUIRE(position->position.z == 3.0f);
    // 3D entities must not silently grow a 2D Transform.
    REQUIRE(entity.get<ECS::Transform>() == nullptr);

    // Translation is relative to the current 3D position.
    {
        std::ofstream out(path, std::ios::trunc);
        out << "function onUpdate(entity, dt) {\n"
            << "  caffeine_translate(entity, 1, 1, 1);\n"
            << "}\n";
    }
    REQUIRE(host.load(path));
    REQUIRE(host.call(path, "onUpdate", entity, 0.016f));
    REQUIRE(position->position.x == 2.0f);
    REQUIRE(position->position.y == 3.0f);
    REQUIRE(position->position.z == 4.0f);

    std::remove(path.c_str());
}

TEST_CASE("Dryad resolves project-relative paths and hot reloads", "[dryad][reload]") {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "caffeine_dryad_search_root";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    const std::filesystem::path script = root / "player.dyd";

    {
        std::ofstream out(script);
        out << "function onUpdate(entity, dt) {\n"
            << "  caffeine_set_position(entity, 1, 0, 0);\n"
            << "}\n";
    }

    ECS::World world;
    DryadHost host;
    host.setWorld(&world);
    host.setSearchRoot(root.string());
    ECS::Entity entity = world.create("ReloadEntity");

    REQUIRE(host.load("player.dyd"));
    REQUIRE(host.isLoaded("player.dyd"));
    REQUIRE(host.call("player.dyd", "onUpdate", entity, 0.016f));
    REQUIRE(entity.get<ECS::Transform>()->position.x == 1.0f);

    {
        std::ofstream out(script, std::ios::trunc);
        out << "function onUpdate(entity, dt) {\n"
            << "  caffeine_set_position(entity, 9, 0, 0);\n"
            << "}\n";
    }
    // Some filesystems have coarse timestamps; force a visible change.
    const auto future = std::filesystem::last_write_time(script, ec) +
                        std::chrono::seconds(2);
    std::filesystem::last_write_time(script, future, ec);

    REQUIRE(host.reloadIfChanged("player.dyd"));
    REQUIRE(host.call("player.dyd", "onUpdate", entity, 0.016f));
    REQUIRE(entity.get<ECS::Transform>()->position.x == 9.0f);

    std::filesystem::remove_all(root, ec);
}

TEST_CASE("Dryad reload is a no-op without file changes", "[dryad][reload]") {
    const std::string path = "dryad_host_no_reload.dyd";
    {
        std::ofstream out(path);
        out << "function onUpdate(entity, dt) { }\n";
    }

    ECS::World world;
    DryadHost host;
    host.setWorld(&world);
    REQUIRE(host.load(path));
    REQUIRE_FALSE(host.reloadIfChanged(path));
    std::remove(path.c_str());
}

#endif
