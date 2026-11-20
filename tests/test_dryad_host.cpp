#include "catch.hpp"

#ifdef CF_HAS_DRYAD

#include "../src/caffeine/script/DryadHost.hpp"
#include "../src/caffeine/ecs/Components.hpp"
#include "../src/caffeine/ecs/Components3D.hpp"
#include "../src/caffeine/ecs/MeshComponents.hpp"
#include "../src/caffeine/ecs/World.hpp"
#include "../src/caffeine/physics/PhysicsComponents3D.hpp"
#include "../src/caffeine/input/InputManager.hpp"
#include "../src/caffeine/runtime/PlayClock.hpp"

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

TEST_CASE("Dryad reads and writes any reflected component", "[dryad][component]") {
    const std::string path = "dryad_host_components.dyd";
    {
        std::ofstream out(path);
        out << "function onUpdate(entity, dt) {\n"
            << "  caffeine_component_add(entity, \"Rigid Body 3D\");\n"
            << "  caffeine_component_set_number(entity, \"Rigid Body 3D\", \"Mass\", 7.5);\n"
            << "  caffeine_component_set_text(entity, \"Rigid Body 3D\", \"Body type\", \"Static\");\n"
            << "  caffeine_component_add(entity, \"Mesh Filter\");\n"
            << "  caffeine_component_set_text(entity, \"Mesh Filter\", \"Primitive\", \"Cone\");\n"
            << "  caffeine_component_add(entity, \"Position 3D\");\n"
            << "  caffeine_component_set_vec(entity, \"Position 3D\", \"Position\", 4, 5, 6);\n"
            << "  let mass = caffeine_component_get_number(entity, \"Rigid Body 3D\", \"Mass\");\n"
            << "  caffeine_component_set_number(entity, \"Position 3D\", \"Position\", mass * 2, 0);\n"
            << "  let kind = caffeine_component_get_text(entity, \"Mesh Filter\", \"Primitive\");\n"
            << "  caffeine_log(kind);\n"
            << "  let index = caffeine_component_get_number(entity, \"Mesh Filter\", \"Primitive\");\n"
            << "  if (index == 6) { caffeine_component_set_number(entity, \"Position 3D\", \"Position\", 9, 0); }\n"
            << "  caffeine_component_has(entity, \"Nonsense\");\n"
            << "}\n";
    }
    ECS::World world;
    DryadHost host;
    host.setWorld(&world);
    ECS::Entity entity = world.create("DryadThing");
    REQUIRE(host.load(path));
    REQUIRE(host.call(path, "onUpdate", entity, 0.016f));

    auto* rb = entity.get<Physics3D::RigidBody3D>();
    REQUIRE(rb != nullptr);
    REQUIRE(rb->mass == 7.5f);
    REQUIRE(rb->bodyType == Physics3D::BodyType3D::Static);
    auto* mesh = entity.get<ECS::MeshFilterComponent>();
    REQUIRE(mesh != nullptr);
    REQUIRE(mesh->primitive == ECS::MeshPrimitive::Cone);
    auto* pos = entity.get<ECS::Position3D>();
    REQUIRE(pos != nullptr);
    REQUIRE(pos->position.x == 9.0f);   // read back (enum index 6 = Cone), then written conditionally
    REQUIRE(pos->position.y == 5.0f);
    REQUIRE(pos->position.z == 6.0f);
    std::remove(path.c_str());
}

TEST_CASE("Dryad natives read input, time and the world", "[dryad][runtime]") {
    const std::string path = "dryad_host_runtime.dyd";
    {
        std::ofstream out(path);
        out << "function onUpdate(entity, dt) {\n"
            << "  let x = 0;\n"
            << "  if (caffeine_input_key_down(\"Space\")) { x = x + 1; }\n"
            << "  if (caffeine_input_key_down(\"Nope\")) { x = x + 100; }\n"
            << "  let spawned = caffeine_world_create(\"Spawned\");\n"
            << "  if (caffeine_world_is_alive(spawned)) { x = x + 10; }\n"
            << "  caffeine_world_destroy(spawned);\n"
            << "  if (!caffeine_world_is_alive(spawned)) { x = x + 20; }\n"
            << "  caffeine_set_position(entity, x, caffeine_world_distance(entity, entity), caffeine_time_scale());\n"
            << "}\n";
    }

    ECS::World world;
    Input::InputManager input;
    DryadHost host;
    host.setWorld(&world);
    host.setInput(&input);
    ECS::Entity entity = world.create("DryadRuntime");
    entity.getOrAdd<ECS::Position3D>().position = Vec3(0, 0, 0);
    input.injectKeyDown(Input::Key::Space);

    Runtime::PlayClock::instance().setTimeScale(1.0f);
    REQUIRE(host.load(path));
    std::string err;
    REQUIRE(host.call(path, "onUpdate", entity, 0.016f, &err));

    auto* pos = entity.get<ECS::Position3D>();
    REQUIRE(pos != nullptr);
    REQUIRE(pos->position.x == 31.0f);  // Space + alive + destroyed, unknown key ignored
    REQUIRE(pos->position.y == 0.0f);
    REQUIRE(pos->position.z == 1.0f);
    std::remove(path.c_str());
}

TEST_CASE("Dryad math natives match Lua and fill the operator gaps", "[dryad][math]") {
    const std::string path = "dryad_math.dyd";
    {
        std::ofstream out(path);
        out << "function onUpdate(entity, dt) {\n"
            << "  let a = caffeine_math_clamp(15, 0, 10) + caffeine_math_min(3, 4) + caffeine_math_abs(-2);\n"
            << "  let b = caffeine_math_lerp(0, 10, 0.5) + caffeine_math_floor(2.9) + caffeine_math_round(2.5);\n"
            << "  let c = caffeine_math_sqrt(-4) + caffeine_math_pow(2, 3) + caffeine_math_max(1, 7);\n"
            << "  caffeine_set_position(entity, a, b, c);\n"
            << "}\n";
    }
    ECS::World world;
    DryadHost host;
    host.setWorld(&world);
    ECS::Entity entity = world.create("DryadMath");
    entity.getOrAdd<ECS::Position3D>().position = Vec3(0, 0, 0);
    REQUIRE(host.load(path));
    std::string err;
    REQUIRE(host.call(path, "onUpdate", entity, 0.016f, &err));
    auto* pos = entity.get<ECS::Position3D>();
    REQUIRE(pos != nullptr);
    REQUIRE(pos->position.x == 15.0f);  // 10 + 3 + 2
    REQUIRE(pos->position.y == 10.0f);  // 5 + 2 + 3
    REQUIRE(pos->position.z == 15.0f);  // 0 + 8 + 7
    std::remove(path.c_str());
}

TEST_CASE("Dryad properties override script globals", "[dryad][component]") {
    const std::string path = "dryad_props.dyd";
    {
        std::ofstream out(path);
        out << "let speed = 1;\n"
            << "function onUpdate(entity, dt) {\n  caffeine_set_position(entity, speed, 0, 0);\n}\n";
    }
    ECS::World world;
    DryadHost host;
    host.setWorld(&world);
    ECS::Entity entity = world.create("DryadProps");
    entity.getOrAdd<ECS::Position3D>().position = Vec3(0, 0, 0);
    REQUIRE(host.load(path));
    ScriptEngine::ExposedVar var;
    var.name = "speed";
    var.kind = ScriptEngine::ExposedVar::Kind::Number;
    var.number = 9.0;
    REQUIRE(host.setProperty(path, var));
    REQUIRE(host.call(path, "onUpdate", entity, 0.016f));
    REQUIRE(entity.get<ECS::Position3D>()->position.x == 9.0f);
    std::remove(path.c_str());
}

TEST_CASE("Dryad natives cover names, hierarchy and random", "[dryad][world]") {
    const std::string path = "dryad_world_api.dyd";
    {
        std::ofstream out(path);
        out << "function onUpdate(entity, dt) {\n"
            << "  let a = caffeine_world_create(\"A\");\n"
            << "  let b = caffeine_world_create(\"B\");\n"
            << "  caffeine_entity_set_name(a, \"Boss\");\n"
            << "  caffeine_entity_set_name(b, \"Minion\");\n"
            << "  let score = 0;\n"
            << "  if (caffeine_hierarchy_set_parent(b, a)) { score = score + 1; }\n"
            << "  if (!caffeine_hierarchy_set_parent(a, b)) { score = score + 10; }\n"
            << "  if (caffeine_hierarchy_child_count(a) == 1 && caffeine_entity_find(\"Minion\") == b) { score = score + 100; }\n"
            << "  caffeine_random_set_seed(7);\n  let r1 = caffeine_random_value();\n"
            << "  caffeine_random_set_seed(7);\n  if (r1 == caffeine_random_value()) { score = score + 1000; }\n"
            << "  caffeine_set_position(entity, score, 0, 0);\n}\n";
    }
    ECS::World world;
    DryadHost host;
    host.setWorld(&world);
    ECS::Entity entity = world.create("DryadWorld");
    entity.getOrAdd<ECS::Position3D>().position = Vec3(0, 0, 0);
    REQUIRE(host.load(path));
    std::string err;
    REQUIRE(host.call(path, "onUpdate", entity, 0.016f, &err));
    REQUIRE(entity.get<ECS::Position3D>()->position.x == 1111.0f);
    std::remove(path.c_str());
}

TEST_CASE("Dryad natives cover scenes, session and audio", "[dryad][scene][audio]") {
    const std::string path = "dryad_scene_api.dyd";
    {
        std::ofstream out(path);
        out << "function onUpdate(entity, dt) {\n"
            << "  let score = 0;\n"
            << "  caffeine_session_set(\"lives\", 3);\n"
            << "  if (caffeine_session_get(\"lives\") == 3) { score = score + 1; }\n"
            << "  if (!caffeine_session_has(\"nope\")) { score = score + 10; }\n"
            << "  if (caffeine_audio_play(\"x.wav\") == 0) { score = score + 100; }\n"
            << "  caffeine_scene_load(\"scenes/two.scene\");\n"
            << "  if (caffeine_scene_is_loading()) { score = score + 1000; }\n"
            << "  caffeine_set_position(entity, score, 0, 0);\n}\n";
    }
    ECS::World world;
    SceneRequests scenes;
    DryadHost host;
    host.setWorld(&world);
    host.setSceneRequests(&scenes);
    ECS::Entity entity = world.create("DryadScene");
    entity.getOrAdd<ECS::Position3D>().position = Vec3(0, 0, 0);
    REQUIRE(host.load(path));
    std::string err;
    REQUIRE(host.call(path, "onUpdate", entity, 0.016f, &err));
    REQUIRE(entity.get<ECS::Position3D>()->position.x == 1111.0f);
    REQUIRE(scenes.take() == "scenes/two.scene");
    SessionStore::instance().clear();
    std::remove(path.c_str());
}

#endif
