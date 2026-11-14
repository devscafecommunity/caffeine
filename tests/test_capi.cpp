#include "catch.hpp"
#include "caffeine/capi/EngineAPI.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

TEST_CASE("C API session creates, renames and selects entities", "[capi]") {
    CaffeineSession* session = caffeine_session_create();
    REQUIRE(session != nullptr);

    const CaffeineEntity a = caffeine_session_create_entity(session, "Player");
    const CaffeineEntity b = caffeine_session_create_entity(session, "Enemy");
    REQUIRE(a != CAFFEINE_ENTITY_NONE);
    REQUIRE(b != CAFFEINE_ENTITY_NONE);
    REQUIRE(caffeine_session_entity_count(session) == 2);

    char name[64] = {};
    REQUIRE(caffeine_session_entity_name(session, a, name, sizeof(name)) == CAFFEINE_OK);
    REQUIRE(std::strcmp(name, "Player") == 0);

    REQUIRE(caffeine_session_set_entity_name(session, a, "Hero") == CAFFEINE_OK);
    REQUIRE(caffeine_session_entity_name(session, a, name, sizeof(name)) == CAFFEINE_OK);
    REQUIRE(std::strcmp(name, "Hero") == 0);

    REQUIRE(caffeine_session_select_entity(session, b) == CAFFEINE_OK);
    REQUIRE(caffeine_session_selected_entity(session) == b);

    CaffeineVec3 position{};
    REQUIRE(caffeine_session_entity_position(session, a, &position) == CAFFEINE_NOT_FOUND);
    REQUIRE(caffeine_session_set_entity_position(session, a, {1.0f, 2.0f, 3.0f}) == CAFFEINE_OK);
    REQUIRE(caffeine_session_entity_position(session, a, &position) == CAFFEINE_OK);
    REQUIRE(position.x == Approx(1.0f));
    REQUIRE(position.y == Approx(2.0f));
    REQUIRE(position.z == Approx(3.0f));

    CaffeineVec3 rotation{};
    REQUIRE(caffeine_session_set_entity_rotation(session, a, {10.0f, 20.0f, 30.0f}) ==
            CAFFEINE_OK);
    REQUIRE(caffeine_session_entity_rotation(session, a, &rotation) == CAFFEINE_OK);
    REQUIRE(rotation.x == Approx(10.0f));
    REQUIRE(rotation.y == Approx(20.0f));
    REQUIRE(rotation.z == Approx(30.0f));

    CaffeineVec3 scale{};
    REQUIRE(caffeine_session_set_entity_scale(session, a, {2.0f, 3.0f, 4.0f}) == CAFFEINE_OK);
    REQUIRE(caffeine_session_entity_scale(session, a, &scale) == CAFFEINE_OK);
    REQUIRE(scale.x == Approx(2.0f));
    REQUIRE(scale.y == Approx(3.0f));
    REQUIRE(scale.z == Approx(4.0f));

    CaffeineCamera camera{1.0f, 0.25f, 12.0f, {1.0f, 2.0f, 3.0f}};
    REQUIRE(caffeine_session_set_camera(session, camera) == CAFFEINE_OK);
    CaffeineCamera cameraOut{};
    REQUIRE(caffeine_session_get_camera(session, &cameraOut) == CAFFEINE_OK);
    REQUIRE(cameraOut.yaw == Approx(1.0f));
    REQUIRE(cameraOut.pitch == Approx(0.25f));
    REQUIRE(cameraOut.distance == Approx(12.0f));
    REQUIRE(cameraOut.focus.x == Approx(1.0f));
    REQUIRE(cameraOut.focus.y == Approx(2.0f));
    REQUIRE(cameraOut.focus.z == Approx(3.0f));

    CaffeineEntityInfo info{};
    REQUIRE(caffeine_session_entity_at(session, 0, &info) == CAFFEINE_OK);
    REQUIRE(info.id != CAFFEINE_ENTITY_NONE);
    REQUIRE(caffeine_session_entity_at(session, 99, &info) == CAFFEINE_NOT_FOUND);

    REQUIRE(caffeine_session_destroy_entity(session, a) == CAFFEINE_OK);
    REQUIRE(caffeine_session_entity_count(session) == 1);
    REQUIRE(caffeine_session_entity_name(session, a, name, sizeof(name)) == CAFFEINE_NOT_FOUND);

    CaffeineFrameStats stats{};
    caffeine_session_tick(session, 0.016f);
    caffeine_session_frame_stats(session, &stats);
    REQUIRE(stats.frame_index == 1);
    REQUIRE(stats.entity_count == 1);
    REQUIRE(stats.delta_time == Approx(0.016f));

    caffeine_session_destroy(session);
}

TEST_CASE("C API saves and reloads a scene", "[capi]") {
    const std::string path = "capi_roundtrip.caf";
    {
        CaffeineSession* session = caffeine_session_create();
        REQUIRE(session != nullptr);
        const CaffeineEntity entity = caffeine_session_create_entity(session, "Saved");
        REQUIRE(caffeine_session_set_entity_position(session, entity, {4.0f, 5.0f, 6.0f}) ==
                CAFFEINE_OK);
        REQUIRE(caffeine_session_is_dirty(session) == 1);
        REQUIRE(caffeine_session_save_scene(session, path.c_str()) == CAFFEINE_OK);
        REQUIRE(caffeine_session_is_dirty(session) == 0);
        caffeine_session_destroy(session);
    }
    {
        CaffeineSession* session = caffeine_session_create();
        REQUIRE(session != nullptr);
        REQUIRE(caffeine_session_load_scene(session, path.c_str()) == CAFFEINE_OK);
        REQUIRE(caffeine_session_entity_count(session) == 1);

        CaffeineEntityInfo info{};
        REQUIRE(caffeine_session_entity_at(session, 0, &info) == CAFFEINE_OK);
        REQUIRE(std::strcmp(info.name, "Saved") == 0);

        CaffeineVec3 position{};
        REQUIRE(caffeine_session_entity_position(session, info.id, &position) == CAFFEINE_OK);
        REQUIRE(position.x == Approx(4.0f));
        REQUIRE(position.y == Approx(5.0f));
        REQUIRE(position.z == Approx(6.0f));
        caffeine_session_destroy(session);
    }
    std::remove(path.c_str());
}

TEST_CASE("C API lists project assets", "[capi]") {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "caffeine_capi_assets_test";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "meshes", ec);
    {
        std::ofstream image(root / "rock.png", std::ios::binary);
        image << "not-a-real-png";
        std::ofstream mesh(root / "meshes" / "tree.obj", std::ios::binary);
        mesh << "v 0 0 0";
    }

    CaffeineSession* session = caffeine_session_create();
    REQUIRE(session != nullptr);
    REQUIRE(caffeine_session_set_asset_root(session, root.string().c_str()) == CAFFEINE_OK);
    REQUIRE(caffeine_session_asset_count(session) >= 2);

    bool foundImage = false;
    bool foundMesh = false;
    bool foundDirectory = false;
    for (uint32_t i = 0; i < caffeine_session_asset_count(session); ++i) {
        CaffeineAssetInfo info{};
        REQUIRE(caffeine_session_asset_at(session, i, &info) == CAFFEINE_OK);
        if (std::strcmp(info.name, "rock.png") == 0) {
            foundImage = std::strcmp(info.kind, "image") == 0 && info.size_bytes > 0;
        }
        if (std::strcmp(info.name, "tree.obj") == 0) {
            foundMesh = std::strcmp(info.kind, "mesh") == 0;
        }
        if (std::strcmp(info.name, "meshes") == 0) {
            foundDirectory = info.is_directory == 1 &&
                             std::strcmp(info.kind, "folder") == 0;
        }
    }
    REQUIRE(foundImage);
    REQUIRE(foundMesh);
    REQUIRE(foundDirectory);

    caffeine_session_destroy(session);
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("C API scene load reports missing files", "[capi]") {
    CaffeineSession* session = caffeine_session_create();
    REQUIRE(session != nullptr);
    REQUIRE(caffeine_session_load_scene(session, "/definitely/not/a/scene.caf") == CAFFEINE_ERROR);
    REQUIRE(std::string(caffeine_session_scene_path(session)).empty());
    caffeine_session_destroy(session);
}
