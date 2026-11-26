#include "catch.hpp"

#include "../src/caffeine/debug/Profiler.hpp"
#include "../src/caffeine/debug/SystemMetrics.hpp"
#include "../src/caffeine/math/Quat.hpp"
#include "caffeine/capi/EngineAPI.h"
#include "../src/caffeine/animation/Rig2DAsset.hpp"
#include <stb/stb_image_write.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

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
    CaffeineCamera highCamera{1.0f, -1.55f, 200.0f, {0.0f, 10000.0f, 0.0f}};
    REQUIRE(caffeine_session_set_camera(session, highCamera) == CAFFEINE_OK);
    REQUIRE(caffeine_session_get_camera(session, &cameraOut) == CAFFEINE_OK);
    CHECK(cameraOut.focus.y == Approx(10000.0f));

    CaffeineViewportSettings viewport{};
    REQUIRE(caffeine_session_get_viewport_settings(session, &viewport) == CAFFEINE_OK);
    CHECK(viewport.show_grid == 1);
    CHECK(viewport.anti_aliasing == 1);
    CHECK(viewport.render_scale > 1.0f);
    CaffeineViewportSettings custom{0, 2, 9.0f};
    REQUIRE(caffeine_session_set_viewport_settings(session, custom) == CAFFEINE_OK);
    REQUIRE(caffeine_session_get_viewport_settings(session, &viewport) == CAFFEINE_OK);
    CHECK(viewport.show_grid == 0);
    CHECK(viewport.anti_aliasing == 2);
    CHECK(viewport.render_scale == Approx(2.0f));

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

    // With no project loaded the asset root is also where relative asset paths resolve: a generator that
    // writes "meshes/tree.obj" there and applies it by that path must find it again.
    char resolved[512] = {};
    REQUIRE(caffeine_session_resolve_path(session, "meshes/tree.obj", resolved, sizeof(resolved)) ==
            CAFFEINE_OK);
    REQUIRE(std::filesystem::equivalent(resolved, root / "meshes" / "tree.obj"));

    caffeine_session_destroy(session);
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("C API takes the folder with project.caffeine as the project root", "[capi]") {
    // The layout Doppio creates: project.caffeine, Scenes/ (capital S) and Assets/Raw/.
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "caffeine_capi_project_root_test";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "Scenes", ec);
    std::filesystem::create_directories(root / "Assets" / "Raw" / "Models", ec);
    {
        std::ofstream manifest(root / "project.caffeine");
        manifest << "{}";
        std::ofstream mesh(root / "Assets" / "Raw" / "Models" / "tree.obj");
        mesh << "v 0 0 0";
    }
    const std::string scene = (root / "Scenes" / "main.caf").string();

    CaffeineSession* session = caffeine_session_create();
    REQUIRE(session != nullptr);
    REQUIRE(caffeine_session_save_scene(session, scene.c_str()) == CAFFEINE_OK);
    REQUIRE(caffeine_session_load_scene(session, scene.c_str()) == CAFFEINE_OK);

    // The asset root is found whatever the case of "Assets/Raw"...
    char buffer[512] = {};
    REQUIRE(caffeine_session_asset_path(session, "", buffer, sizeof(buffer)) == CAFFEINE_OK);
    CHECK(std::filesystem::equivalent(buffer, root / "Assets" / "Raw"));
    // ...and a project path resolves from the manifest's folder, not from Scenes/.
    REQUIRE(caffeine_session_asset_project_path(session, "Models/tree.obj", buffer,
                                                sizeof(buffer)) == CAFFEINE_OK);
    CHECK(std::string(buffer) == "Assets/Raw/Models/tree.obj");
    REQUIRE(caffeine_session_resolve_path(session, buffer, buffer, sizeof(buffer)) == CAFFEINE_OK);
    CHECK(std::filesystem::equivalent(buffer, root / "Assets" / "Raw" / "Models" / "tree.obj"));

    caffeine_session_destroy(session);
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("C API hierarchy parent links", "[capi]") {
    CaffeineSession* session = caffeine_session_create();
    REQUIRE(session != nullptr);
    const CaffeineEntity root = caffeine_session_create_entity(session, "Root");
    const CaffeineEntity child = caffeine_session_create_entity(session, "Child");
    const CaffeineEntity grandchild = caffeine_session_create_entity(session, "Grandchild");
    REQUIRE(root != CAFFEINE_ENTITY_NONE);
    REQUIRE(child != CAFFEINE_ENTITY_NONE);
    REQUIRE(grandchild != CAFFEINE_ENTITY_NONE);

    CaffeineEntity parent = 1234;
    REQUIRE(caffeine_session_entity_parent(session, child, &parent) == CAFFEINE_OK);
    CHECK(parent == CAFFEINE_ENTITY_NONE);

    REQUIRE(caffeine_session_set_entity_parent(session, child, root) == CAFFEINE_OK);
    REQUIRE(caffeine_session_set_entity_parent(session, grandchild, child) == CAFFEINE_OK);
    REQUIRE(caffeine_session_entity_parent(session, child, &parent) == CAFFEINE_OK);
    CHECK(parent == root);
    REQUIRE(caffeine_session_entity_parent(session, grandchild, &parent) == CAFFEINE_OK);
    CHECK(parent == child);

    // Cycles are rejected.
    CHECK(caffeine_session_set_entity_parent(session, root, grandchild) ==
          CAFFEINE_INVALID_ARGUMENT);
    CHECK(caffeine_session_set_entity_parent(session, child, child) ==
          CAFFEINE_INVALID_ARGUMENT);

    REQUIRE(caffeine_session_set_entity_parent(session, child, CAFFEINE_ENTITY_NONE) ==
            CAFFEINE_OK);
    REQUIRE(caffeine_session_entity_parent(session, child, &parent) == CAFFEINE_OK);
    CHECK(parent == CAFFEINE_ENTITY_NONE);

    caffeine_session_destroy(session);
}

TEST_CASE("C API hierarchy composes parent transforms", "[capi]") {
    CaffeineSession* session = caffeine_session_create();
    REQUIRE(session != nullptr);
    const CaffeineEntity root = caffeine_session_create_entity(session, "Group");
    const CaffeineEntity child = caffeine_session_create_entity(session, "Child");
    const CaffeineEntity grandchild = caffeine_session_create_entity(session, "Grandchild");
    REQUIRE(root != CAFFEINE_ENTITY_NONE);
    REQUIRE(child != CAFFEINE_ENTITY_NONE);
    REQUIRE(grandchild != CAFFEINE_ENTITY_NONE);

    REQUIRE(caffeine_session_set_entity_position(session, root, {10.0f, 0.0f, 0.0f}) ==
            CAFFEINE_OK);
    REQUIRE(caffeine_session_set_entity_position(session, child, {5.0f, 0.0f, 0.0f}) ==
            CAFFEINE_OK);
    REQUIRE(caffeine_session_set_entity_position(session, grandchild, {2.0f, 0.0f, 0.0f}) ==
            CAFFEINE_OK);
    REQUIRE(caffeine_session_set_entity_parent(session, child, root) == CAFFEINE_OK);
    REQUIRE(caffeine_session_set_entity_parent(session, grandchild, child) == CAFFEINE_OK);

    CaffeineVec3 world{};
    REQUIRE(caffeine_session_entity_world_position(session, root, &world) == CAFFEINE_OK);
    CHECK(world.x == Approx(10.0f));
    REQUIRE(caffeine_session_entity_world_position(session, child, &world) == CAFFEINE_OK);
    CHECK(world.x == Approx(15.0f));
    REQUIRE(caffeine_session_entity_world_position(session, grandchild, &world) == CAFFEINE_OK);
    CHECK(world.x == Approx(17.0f));

    // Setting a world position derives the local transform from the group.
    REQUIRE(caffeine_session_set_entity_world_position(session, child, {20.0f, 0.0f, 0.0f}) ==
            CAFFEINE_OK);
    CaffeineVec3 local{};
    REQUIRE(caffeine_session_entity_position(session, child, &local) == CAFFEINE_OK);
    CHECK(local.x == Approx(10.0f));
    REQUIRE(caffeine_session_entity_world_position(session, child, &world) == CAFFEINE_OK);
    CHECK(world.x == Approx(20.0f));
    REQUIRE(caffeine_session_set_entity_position(session, child, {5.0f, 0.0f, 0.0f}) ==
            CAFFEINE_OK);

    // Rotation and scale of the group are inherited too.
    REQUIRE(caffeine_session_set_entity_rotation(session, root, {0.0f, 90.0f, 0.0f}) ==
            CAFFEINE_OK);
    REQUIRE(caffeine_session_entity_world_position(session, child, &world) == CAFFEINE_OK);
    CHECK(world.x == Approx(10.0f));
    CHECK(world.z == Approx(-5.0f));

    REQUIRE(caffeine_session_set_entity_rotation(session, root, {0.0f, 0.0f, 0.0f}) ==
            CAFFEINE_OK);
    REQUIRE(caffeine_session_set_entity_scale(session, root, {2.0f, 2.0f, 2.0f}) ==
            CAFFEINE_OK);
    REQUIRE(caffeine_session_entity_world_position(session, grandchild, &world) ==
            CAFFEINE_OK);
    CHECK(world.x == Approx(24.0f));
    REQUIRE(caffeine_session_set_entity_world_position(
                session, grandchild, {24.0f, 0.0f, 0.0f}) == CAFFEINE_OK);
    REQUIRE(caffeine_session_entity_position(session, grandchild, &local) == CAFFEINE_OK);
    CHECK(local.x == Approx(2.0f));

    // Unparenting drops the inherited offset but keeps descendants linked.
    REQUIRE(caffeine_session_set_entity_parent(session, child, CAFFEINE_ENTITY_NONE) ==
            CAFFEINE_OK);
    REQUIRE(caffeine_session_entity_world_position(session, child, &world) == CAFFEINE_OK);
    CHECK(world.x == Approx(5.0f));
    REQUIRE(caffeine_session_entity_world_position(session, grandchild, &world) ==
            CAFFEINE_OK);
    CHECK(world.x == Approx(7.0f));

    REQUIRE(caffeine_session_entity_world_position(session, CAFFEINE_ENTITY_NONE, &world) ==
            CAFFEINE_NOT_FOUND);
    CHECK(caffeine_session_entity_world_position(session, child, nullptr) ==
          CAFFEINE_INVALID_ARGUMENT);

    caffeine_session_destroy(session);
}

TEST_CASE("C API material assets save, load and assign", "[capi]") {
    const auto dir = std::filesystem::temp_directory_path() / "caffeine-capi-material";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto path = dir / "terrain.mat";

    CaffeineMaterial material{};
    REQUIRE(caffeine_material_default(&material) == CAFFEINE_OK);
    REQUIRE(std::strcmp(material.name, "New Material") == 0);
    REQUIRE(material.roughness == Approx(0.5f));
    REQUIRE(material.ior == Approx(1.5f));
    material.albedo[0] = 0.2f;
    material.albedo[1] = 0.8f;
    material.metallic = 0.7f;
    material.normal_strength = 1.5f;
    material.normal_directx = 1;
    std::strncpy(material.albedo_map, "textures/sand.png", sizeof(material.albedo_map) - 1);
    REQUIRE(caffeine_material_save(path.string().c_str(), &material) == CAFFEINE_OK);

    CaffeineMaterial loaded{};
    REQUIRE(caffeine_material_load(path.string().c_str(), &loaded) == CAFFEINE_OK);
    CHECK(loaded.albedo[1] == Approx(0.8f));
    CHECK(loaded.metallic == Approx(0.7f));
    CHECK(loaded.normal_strength == Approx(1.5f));
    CHECK(loaded.normal_directx == 1);
    CHECK(std::strcmp(loaded.albedo_map, "textures/sand.png") == 0);

    REQUIRE(caffeine_material_preset_count() > 8);
    CaffeineMaterialPresetInfo preset{};
    REQUIRE(caffeine_material_preset_at(0, &preset, &loaded) == CAFFEINE_OK);
    CHECK(std::strlen(preset.category) > 0);
    CHECK(std::strlen(preset.name) > 0);
    CHECK(caffeine_material_preset_at(9999, &preset, &loaded) == CAFFEINE_NOT_FOUND);

    CaffeineSession* session = caffeine_session_create();
    REQUIRE(session != nullptr);
    const CaffeineEntity entity = caffeine_session_create_entity(session, "Ground");
    REQUIRE(entity != CAFFEINE_ENTITY_NONE);
    uint32_t filterType = 0;
    for (uint32_t index = 0; index < caffeine_component_type_count(); ++index) {
        CaffeineComponentTypeInfo info{};
        REQUIRE(caffeine_component_type_at(index, &info) == CAFFEINE_OK);
        if (std::strcmp(info.name, "Mesh Filter") == 0) filterType = info.id;
    }
    REQUIRE(filterType != 0);
    REQUIRE(caffeine_session_add_component(session, entity, filterType) == CAFFEINE_OK);
    REQUIRE(caffeine_session_apply_material(session, entity, "materials/terrain.mat") ==
            CAFFEINE_OK);
    char assigned[512] = {};
    REQUIRE(caffeine_session_entity_material(session, entity, assigned, sizeof(assigned)) ==
            CAFFEINE_OK);
    CHECK(std::strcmp(assigned, "materials/terrain.mat") == 0);

    const CaffeineEntity empty = caffeine_session_create_entity(session, "No Mesh");
    REQUIRE(empty != CAFFEINE_ENTITY_NONE);
    REQUIRE(caffeine_session_apply_material(session, empty, "materials/auto.mat") ==
            CAFFEINE_OK);
    char autoAssigned[512] = {};
    REQUIRE(caffeine_session_entity_material(session, empty, autoAssigned,
                                             sizeof(autoAssigned)) == CAFFEINE_OK);
    CHECK(std::strcmp(autoAssigned, "materials/auto.mat") == 0);
    caffeine_session_destroy(session);
    std::filesystem::remove_all(dir);
}

TEST_CASE("C API texture maps generate and pack", "[capi]") {
    const auto dir = std::filesystem::temp_directory_path() / "caffeine-capi-maps";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto heightPath = dir / "height.ppm";
    {
        std::ofstream out(heightPath, std::ios::binary);
        out << "P6\n2 2\n255\n";
        const unsigned char pixels[12] = {
            0, 0, 0, 64, 64, 64,
            128, 128, 128, 192, 192, 192,
        };
        out.write(reinterpret_cast<const char*>(pixels), sizeof(pixels));
    }
    const auto normalPath = dir / "normal.png";
    REQUIRE(caffeine_texture_generate_map(
                heightPath.string().c_str(), normalPath.string().c_str(), CAFFEINE_TEXTURE_NORMAL,
                1.0f, 0, 0, 0.0f, 1.0f, 0, 2) == CAFFEINE_OK);
    CHECK(std::filesystem::exists(normalPath));
    CHECK(std::filesystem::file_size(normalPath) > 0);

    const auto aoPath = dir / "ao.png";
    REQUIRE(caffeine_texture_generate_map(heightPath.string().c_str(), aoPath.string().c_str(),
                                          CAFFEINE_TEXTURE_AO, 0.8f, 0, 0, 0.0f, 1.0f, 0, 2) ==
            CAFFEINE_OK);
    const auto ormPath = dir / "orm.png";
    REQUIRE(caffeine_texture_pack_maps(aoPath.string().c_str(), nullptr, nullptr,
                                       ormPath.string().c_str(), 0) == CAFFEINE_OK);
    CHECK(std::filesystem::exists(ormPath));
    CHECK(caffeine_texture_pack_maps(nullptr, nullptr, nullptr, ormPath.string().c_str(), 0) ==
          CAFFEINE_INVALID_ARGUMENT);

    const auto generatedPath = dir / "generated.png";
    const uint8_t generated[16] = {
        255, 0, 0, 255, 0, 255, 0, 255,
        0, 0, 255, 255, 255, 255, 255, 255,
    };
    REQUIRE(caffeine_texture_save_rgba(generatedPath.string().c_str(), 2, 2, generated,
                                       sizeof(generated)) == CAFFEINE_OK);
    CHECK(std::filesystem::exists(generatedPath));
    CHECK(caffeine_texture_save_rgba(generatedPath.string().c_str(), 2, 2, generated, 4) ==
          CAFFEINE_INVALID_ARGUMENT);

    const auto generated16Path = dir / "generated16.png";
    const uint16_t generated16[16] = {
        65535, 0,     0,     65535, 0,     65535, 0,     65535,
        0,     0,     65535, 65535, 65535, 65535, 65535, 65535,
    };
    REQUIRE(caffeine_texture_save_rgba16(generated16Path.string().c_str(), 2, 2, generated16,
                                          sizeof(generated16)) == CAFFEINE_OK);
    CHECK(std::filesystem::exists(generated16Path));
    CHECK(caffeine_texture_save_rgba16(generated16Path.string().c_str(), 2, 2, generated16, 8) ==
          CAFFEINE_INVALID_ARGUMENT);

    uint32_t loadedWidth = 0;
    uint32_t loadedHeight = 0;
    uint8_t* loadedPixels = nullptr;
    size_t loadedBytes = 0;
    REQUIRE(caffeine_texture_load_rgba(generated16Path.string().c_str(), &loadedWidth,
                                       &loadedHeight, &loadedPixels, &loadedBytes) == CAFFEINE_OK);
    REQUIRE(loadedWidth == 2);
    REQUIRE(loadedHeight == 2);
    REQUIRE(loadedBytes == sizeof(generated));
    REQUIRE(loadedPixels != nullptr);
    CHECK(loadedPixels[0] > 240);
    CHECK(loadedPixels[1] < 16);
    caffeine_session_free(loadedPixels);

    uint32_t thumbWidth = 0;
    uint32_t thumbHeight = 0;
    uint8_t* thumbPixels = nullptr;
    size_t thumbBytes = 0;
    REQUIRE(caffeine_texture_load_thumbnail(generatedPath.string().c_str(), 16, &thumbWidth,
                                            &thumbHeight, &thumbPixels, &thumbBytes) ==
            CAFFEINE_OK);
    CHECK(thumbWidth <= 16);
    CHECK(thumbHeight <= 16);
    CHECK(thumbBytes == static_cast<size_t>(thumbWidth) * thumbHeight * 4);
    REQUIRE(thumbPixels != nullptr);
    caffeine_session_free(thumbPixels);
    std::filesystem::remove_all(dir);
}

TEST_CASE("C API bakes scene AO through Photon", "[capi]") {
    CaffeineSession* session = caffeine_session_create();
    REQUIRE(session != nullptr);
    const CaffeineEntity entity = caffeine_session_create_entity(session, "AO Target");
    REQUIRE(entity != CAFFEINE_ENTITY_NONE);

    CaffeineAOBakeResult result{};
    CHECK(caffeine_session_bake_ao(session, CAFFEINE_ENTITY_NONE, 64, 4, 2.0f, nullptr,
                                   &result) == CAFFEINE_NOT_FOUND);
    REQUIRE(caffeine_session_bake_ao(session, entity, 64, 4, 2.0f, nullptr, &result) ==
            CAFFEINE_OK);
    CHECK(result.ok == 0);
    CHECK(std::strlen(result.error) > 0);
    CHECK(caffeine_session_bake_ao(session, entity, 64, 4, 2.0f, nullptr, nullptr) ==
          CAFFEINE_INVALID_ARGUMENT);
    caffeine_session_destroy(session);
}

TEST_CASE("C API reports its version", "[capi]") {
    CHECK(caffeine_capi_version() == CAFFEINE_CAPI_VERSION);
    CHECK(caffeine_capi_version() >= 2);
}

TEST_CASE("C API sessions simulate by default and an editor can pause them", "[capi][simulation]") {
    CHECK(caffeine_capi_version() >= 21);
    CHECK(caffeine_session_simulating(nullptr) == 0);
    caffeine_session_set_simulating(nullptr, 1);  // nothing to set: must not crash

    CaffeineSession* session = caffeine_session_create();
    REQUIRE(session != nullptr);
    // The runtime and every existing caller simulate unless they ask otherwise.
    CHECK(caffeine_session_simulating(session) == 1);
    caffeine_session_set_simulating(session, 0);
    CHECK(caffeine_session_simulating(session) == 0);
    caffeine_session_set_simulating(session, 7);  // any non-zero is on
    CHECK(caffeine_session_simulating(session) == 1);
    caffeine_session_destroy(session);
}

namespace {
uint32_t typeIdByName(const char* wanted) {
    const uint32_t count = caffeine_component_type_count();
    for (uint32_t i = 0; i < count; ++i) {
        CaffeineComponentTypeInfo info{};
        if (caffeine_component_type_at(i, &info) == CAFFEINE_OK && std::strcmp(info.name, wanted) == 0) return info.id;
    }
    return UINT32_MAX;
}
}  // namespace

TEST_CASE("C API scene views look through the scene's cameras", "[capi][views]") {
    CHECK(caffeine_capi_version() >= 23);
    uint32_t entity = 0;
    float fov = 0.0f;
    CHECK(caffeine_session_scene_camera(nullptr, CAFFEINE_VIEW_GAME, &entity, &fov) == CAFFEINE_INVALID_ARGUMENT);

    CaffeineSession* session = caffeine_session_create();
    REQUIRE(session != nullptr);
    CHECK(caffeine_session_scene_camera(session, 5, &entity, &fov) == CAFFEINE_INVALID_ARGUMENT);
    // No camera: there is no scene view.
    CHECK(caffeine_session_scene_camera(session, CAFFEINE_VIEW_GAME, &entity, &fov) == CAFFEINE_ERROR);

    const uint32_t cameraType = typeIdByName("Camera 3D");
    REQUIRE(cameraType != UINT32_MAX);
    const CaffeineEntity first = caffeine_session_create_entity(session, "Camera A");
    const CaffeineEntity second = caffeine_session_create_entity(session, "Camera B");
    REQUIRE(caffeine_session_add_component(session, first, cameraType) == CAFFEINE_OK);
    REQUIRE(caffeine_session_add_component(session, second, cameraType) == CAFFEINE_OK);

    // With no active camera the first one is the game's; the preview follows unless a camera is chosen.
    REQUIRE(caffeine_session_scene_camera(session, CAFFEINE_VIEW_GAME, &entity, &fov) == CAFFEINE_OK);
    CHECK(entity == first);
    CHECK(fov > 1.0f);
    REQUIRE(caffeine_session_scene_camera(session, CAFFEINE_VIEW_CAMERA, &entity, nullptr) == CAFFEINE_OK);
    CHECK(entity == first);
    CHECK(caffeine_session_set_preview_camera(session, second) == CAFFEINE_OK);
    REQUIRE(caffeine_session_scene_camera(session, CAFFEINE_VIEW_CAMERA, &entity, nullptr) == CAFFEINE_OK);
    CHECK(entity == second);
    REQUIRE(caffeine_session_scene_camera(session, CAFFEINE_VIEW_GAME, &entity, nullptr) == CAFFEINE_OK);
    CHECK(entity == first);
    // A chosen camera that no longer exists falls back to the others.
    CHECK(caffeine_session_set_preview_camera(session, 99999) == CAFFEINE_OK);
    REQUIRE(caffeine_session_scene_camera(session, CAFFEINE_VIEW_CAMERA, &entity, nullptr) == CAFFEINE_OK);
    CHECK(entity == first);
    caffeine_session_destroy(session);
}

TEST_CASE("C API game clock follows the game's ticks, once, whichever view draws", "[capi][game][simulation]") {
    CaffeineSession* session = caffeine_session_create();
    REQUIRE(session != nullptr);
    const uint32_t cameraType = typeIdByName("Camera 3D");
    REQUIRE(cameraType != UINT32_MAX);
    const CaffeineEntity camera = caffeine_session_create_entity(session, "Camera");
    REQUIRE(caffeine_session_add_component(session, camera, cameraType) == CAFFEINE_OK);

    const uint32_t w = 64, h = 48;
    std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4u);
    auto clock = [&]() {
        CaffeineWeatherState state{};
        REQUIRE(caffeine_session_weather_state(session, &state) == CAFFEINE_OK);
        return state.clock_s;
    };
    auto drawEditor = [&]() { REQUIRE(caffeine_session_render(session, w, h, pixels.data(), pixels.size()) == CAFFEINE_OK); };
    auto drawGame = [&]() {
        REQUIRE(caffeine_session_view_render_begin(session, CAFFEINE_VIEW_GAME, w, h) == CAFFEINE_OK);
        for (int i = 0; i < 400; ++i) {
            if (caffeine_session_view_render_poll(session, CAFFEINE_VIEW_GAME, pixels.data(), pixels.size()) ==
                CAFFEINE_RENDER_READY) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        FAIL("the game view did not finish");
    };

    // Stopped: nothing steps the clock.
    caffeine_session_set_simulating(session, 0);
    drawEditor();
    const float stopped = clock();
    drawEditor();
    CHECK(clock() == stopped);

    // Playing: the clock moves by the game's ticks, no more, no less.
    caffeine_session_set_simulating(session, 1);
    REQUIRE(caffeine_session_game_start(session) == CAFFEINE_OK);
    drawEditor();
    CHECK(clock() == stopped);  // no tick yet, nothing to show
    REQUIRE(caffeine_session_game_tick(session, 0.05f) == CAFFEINE_OK);
    drawEditor();  // the editor viewport takes the step...
    CHECK(clock() == Approx(stopped + 0.05f).margin(1e-4));
    drawGame();  // ...and the game view, in the same tick, takes none
    drawEditor();
    CHECK(clock() == Approx(stopped + 0.05f).margin(1e-4));
    // With only the game view on screen it takes the step instead.
    REQUIRE(caffeine_session_game_tick(session, 0.05f) == CAFFEINE_OK);
    drawGame();
    CHECK(clock() == Approx(stopped + 0.10f).margin(1e-4));
    // Ticks nobody saw add up, up to a quarter of a second.
    for (int i = 0; i < 20; ++i) REQUIRE(caffeine_session_game_tick(session, 0.05f) == CAFFEINE_OK);
    drawEditor();
    CHECK(clock() == Approx(stopped + 0.10f + 0.25f).margin(1e-3));
    // Paused (not simulating): pictures stand still again.
    caffeine_session_set_simulating(session, 0);
    drawEditor();
    CHECK(clock() == Approx(stopped + 0.35f).margin(1e-3));
    caffeine_session_game_stop(session);
    caffeine_session_destroy(session);
}

TEST_CASE("C API runs the game: start, tick, keys and stop", "[capi][game]") {
    CAFFEINE_GAME_CHECK_NULLS:
    CHECK(caffeine_session_game_running(nullptr) == 0);
    CHECK(caffeine_session_game_tick(nullptr, 0.016f) == CAFFEINE_INVALID_ARGUMENT);
    CaffeineGameState state{};
    CHECK(caffeine_session_game_state(nullptr, &state) == CAFFEINE_INVALID_ARGUMENT);
    caffeine_session_game_key(nullptr, 4, 1);  // must not crash
    caffeine_session_game_stop(nullptr);

    CaffeineSession* session = caffeine_session_create();
    REQUIRE(session != nullptr);
    CHECK(caffeine_session_game_running(session) == 0);
    CHECK(caffeine_session_game_tick(session, 0.016f) == CAFFEINE_INVALID_ARGUMENT);  // not started

    REQUIRE(caffeine_session_game_start(session) == CAFFEINE_OK);
    CHECK(caffeine_session_game_running(session) == 1);
    for (int i = 0; i < 5; ++i) REQUIRE(caffeine_session_game_tick(session, 1.0f / 60.0f) == CAFFEINE_OK);
    caffeine_session_game_key(session, 4, 1);
    caffeine_session_game_key(session, 4, 0);
    caffeine_session_game_mouse(session, 10.0f, 20.0f, 1, 1);
    caffeine_session_game_release_keys(session);
    REQUIRE(caffeine_session_game_state(session, &state) == CAFFEINE_OK);
    CHECK(state.running == 1);
    CHECK(state.frames == 5);
    CHECK(state.time_s == Approx(5.0f / 60.0f).epsilon(0.01));

    // The editor's own tick leaves the scene to the game.
    const uint64_t before = state.frames;
    caffeine_session_tick(session, 0.5f);
    caffeine_session_game_state(session, &state);
    CHECK(state.frames == before);

    caffeine_session_game_stop(session);
    CHECK(caffeine_session_game_running(session) == 0);
    CHECK(caffeine_session_game_tick(session, 0.016f) == CAFFEINE_INVALID_ARGUMENT);
    // Starting again begins a fresh run.
    REQUIRE(caffeine_session_game_start(session) == CAFFEINE_OK);
    caffeine_session_game_state(session, &state);
    CHECK(state.frames == 0);
    caffeine_session_destroy(session);
}

TEST_CASE("C API reports Photon's voxel stats and takes a rebuild request", "[capi][photon]") {
    CHECK(caffeine_capi_version() >= 17);
    CHECK(caffeine_session_photon_stats(nullptr, nullptr) == CAFFEINE_INVALID_ARGUMENT);
    CHECK(caffeine_session_photon_rebuild(nullptr) == CAFFEINE_INVALID_ARGUMENT);
    CaffeineSession* session = caffeine_session_create();
    REQUIRE(session != nullptr);
    CaffeinePhotonStats stats{};
    stats.objects = 99;
    // No frame was rendered: every counter is zero and nothing is over budget.
    CHECK(caffeine_session_photon_stats(session, &stats) == CAFFEINE_OK);
    CHECK(stats.objects == 0);
    CHECK(stats.triangles == 0);
    CHECK(stats.over_budget == 0);
    CHECK(caffeine_session_photon_rebuild(session) == CAFFEINE_OK);
    caffeine_session_destroy(session);
}

TEST_CASE("C API reports the scope profiler and switches GPU timing", "[capi][profiler]") {
    CHECK(caffeine_capi_version() >= 18);
    uint32_t count = 0;
    CHECK(caffeine_profiler_report(nullptr, 0, nullptr) == CAFFEINE_INVALID_ARGUMENT);
    CHECK(caffeine_profiler_report(nullptr, 4, &count) == CAFFEINE_INVALID_ARGUMENT);
    CHECK(caffeine_profiler_gpu_timing(nullptr) == CAFFEINE_INVALID_ARGUMENT);

    CHECK(caffeine_profiler_reset() == CAFFEINE_OK);
    CHECK(caffeine_profiler_report(nullptr, 0, &count) == CAFFEINE_OK);
    CHECK(count == 0);
    {
        CF_PROFILE_SCOPE("capi_outer");
        for (int i = 0; i < 3; ++i) {
            CF_PROFILE_SCOPE("capi_inner");
        }
    }
    // The count is reported even when the buffer is too small, and the scopes come outer first.
    CHECK(caffeine_profiler_report(nullptr, 0, &count) == CAFFEINE_OK);
    REQUIRE(count == 2);
    CaffeineProfileScope scopes[4]{};
    CHECK(caffeine_profiler_report(scopes, 1, &count) == CAFFEINE_OK);
    CHECK(count == 2);
    CHECK(std::strcmp(scopes[0].name, "capi_outer") == 0);
    CHECK(scopes[0].depth == 0);
    CHECK(scopes[1].calls == 0);  // not copied: capacity 1
    CHECK(caffeine_profiler_report(scopes, 4, &count) == CAFFEINE_OK);
    CHECK(std::strcmp(scopes[1].name, "capi_inner") == 0);
    CHECK(scopes[1].depth == 1);
    CHECK(scopes[1].calls == 3);
    CHECK(scopes[0].calls == 1);
    CHECK(scopes[0].avg_ms >= scopes[1].avg_ms);
    CHECK(scopes[0].max_ms >= scopes[0].last_ms);

    // The recent figures cover the latest calls only: a slow start leaves them, the cumulative average keeps it.
    CHECK(caffeine_profiler_reset() == CAFFEINE_OK);
    for (int i = 0; i < 10; ++i) {
        CF_PROFILE_SCOPE("capi_window");
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    for (int i = 0; i < 130; ++i) {
        CF_PROFILE_SCOPE("capi_window");
    }
    CaffeineProfileScope window[2]{};
    REQUIRE(caffeine_profiler_report(window, 2, &count) == CAFFEINE_OK);
    REQUIRE(count == 1);
    CHECK(window[0].calls == 140);
    CHECK(window[0].max_ms >= 3.0f);                          // the slow calls are in the whole history
    CHECK(window[0].recent_max_ms < 1.0f);                    // and gone from the last 120
    CHECK(window[0].recent_avg_ms < window[0].avg_ms * 0.5f);
    CHECK(window[0].recent_avg_ms >= 0.0f);

    int32_t on = 1;
    CHECK(caffeine_profiler_gpu_timing(&on) == CAFFEINE_OK);
    CHECK(on == 0);
    CHECK(caffeine_profiler_set_gpu_timing(1) == CAFFEINE_OK);
    CHECK(caffeine_profiler_gpu_timing(&on) == CAFFEINE_OK);
    CHECK(on == 1);
    CHECK(caffeine_profiler_set_gpu_timing(0) == CAFFEINE_OK);
    CHECK(caffeine_profiler_reset() == CAFFEINE_OK);
}

TEST_CASE("C API reports the machine's CPU cores, memory and GPU", "[capi][profiler]") {
    CHECK(caffeine_capi_version() >= 19);
    CHECK(caffeine_system_metrics(nullptr) == CAFFEINE_INVALID_ARGUMENT);
    CaffeineSystemMetrics first{};
    CHECK(caffeine_system_metrics(&first) == CAFFEINE_OK);
    // Burn a little CPU so the second sample has something to measure.
    volatile double sink = 0.0;
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(150);
    while (std::chrono::steady_clock::now() < until) sink = sink + 1.0;
    CaffeineSystemMetrics m{};
    REQUIRE(caffeine_system_metrics(&m) == CAFFEINE_OK);
#ifdef __linux__
    CHECK(m.cpu_core_count >= 1);
    CHECK(m.cpu_core_count <= CAFFEINE_MAX_CPU_CORES);
    for (uint32_t i = 0; i < m.cpu_core_count; ++i) {
        CHECK(m.cpu_core_percent[i] >= 0.0f);
        CHECK(m.cpu_core_percent[i] <= 100.0f);
    }
    CHECK(m.cpu_total_percent >= 0.0f);
    CHECK(m.cpu_total_percent <= 100.0f);
    CHECK(m.process_cpu_percent > 0.0f);  // this thread was spinning
    CHECK(m.ram_total_mib > 0.0f);
    CHECK(m.ram_used_mib > 0.0f);
    CHECK(m.ram_used_mib <= m.ram_total_mib);
    CHECK(m.ram_process_mib > 0.0f);
    CHECK(m.ram_process_mib <= m.ram_total_mib);
#endif
    // Unavailable values are negative, never garbage.
    CHECK((m.gpu_busy_percent < 0.0f || m.gpu_busy_percent <= 100.0f));
    CHECK((m.vram_total_mib < 0.0f || m.vram_used_mib <= m.vram_total_mib));
    // Cores beyond the count stay zero.
    for (uint32_t i = m.cpu_core_count; i < CAFFEINE_MAX_CPU_CORES; ++i) CHECK(m.cpu_core_percent[i] == 0.0f);
    (void)first;
}

TEST_CASE("C API exposes component schemas and values", "[capi]") {
    CaffeineSession* session = caffeine_session_create();
    REQUIRE(session != nullptr);
    const CaffeineEntity entity = caffeine_session_create_entity(session, "Thing");
    REQUIRE(entity != CAFFEINE_ENTITY_NONE);
    REQUIRE(caffeine_session_set_entity_position(session, entity, {0.0f, 0.0f, 0.0f}) ==
            CAFFEINE_OK);

    uint32_t transformType = 0;
    for (uint32_t i = 0; i < caffeine_component_type_count(); ++i) {
        CaffeineComponentTypeInfo info{};
        REQUIRE(caffeine_component_type_at(i, &info) == CAFFEINE_OK);
        if (std::strcmp(info.name, "Transform") == 0) transformType = info.id;
    }
    REQUIRE(transformType != 0);

    uint32_t positionField = UINT32_MAX;
    for (uint32_t i = 0; i < 3; ++i) {
        CaffeineFieldInfo info{};
        REQUIRE(caffeine_component_type_field_at(transformType, i, &info) == CAFFEINE_OK);
        if (std::strcmp(info.name, "Position") == 0) positionField = i;
    }
    REQUIRE(positionField != UINT32_MAX);

    bool foundTransform = false;
    uint32_t transformIndex = 0;
    for (uint32_t i = 0; i < caffeine_session_entity_component_count(session, entity); ++i) {
        CaffeineComponentInfo info{};
        REQUIRE(caffeine_session_entity_component_at(session, entity, i, &info) ==
                CAFFEINE_OK);
        if (info.type_id == transformType) {
            foundTransform = true;
            transformIndex = i;
        }
    }
    REQUIRE(foundTransform);

    CaffeineFieldValue value{};
    REQUIRE(caffeine_session_entity_component_field_get(session, entity, transformIndex,
                                                        positionField, &value) ==
            CAFFEINE_OK);
    REQUIRE(value.kind == CAFFEINE_FIELD_VEC3);
    value.vec[0] = 4.0f;
    value.vec[1] = 5.0f;
    value.vec[2] = 6.0f;
    REQUIRE(caffeine_session_entity_component_field_set(session, entity, transformIndex,
                                                        positionField, &value) ==
            CAFFEINE_OK);
    CaffeineFieldValue readback{};
    REQUIRE(caffeine_session_entity_component_field_get(session, entity, transformIndex,
                                                        positionField, &readback) ==
            CAFFEINE_OK);
    CHECK(readback.vec[0] == 4.0f);
    CHECK(readback.vec[1] == 5.0f);
    CHECK(readback.vec[2] == 6.0f);

    uint32_t cameraType = 0;
    for (uint32_t i = 0; i < caffeine_component_type_count(); ++i) {
        CaffeineComponentTypeInfo info{};
        REQUIRE(caffeine_component_type_at(i, &info) == CAFFEINE_OK);
        if (std::strcmp(info.name, "Camera 3D") == 0) cameraType = info.id;
    }
    REQUIRE(cameraType != 0);
    const uint32_t before = caffeine_session_entity_component_count(session, entity);
    REQUIRE(caffeine_session_add_component(session, entity, cameraType) == CAFFEINE_OK);
    CHECK(caffeine_session_entity_component_count(session, entity) == before + 1);
    REQUIRE(caffeine_session_remove_component(session, entity, cameraType) == CAFFEINE_OK);
    CHECK(caffeine_session_entity_component_count(session, entity) == before);

    uint32_t lightType = 0;
    for (uint32_t i = 0; i < caffeine_component_type_count(); ++i) {
        CaffeineComponentTypeInfo info{};
        REQUIRE(caffeine_component_type_at(i, &info) == CAFFEINE_OK);
        if (std::strcmp(info.name, "Light") == 0) lightType = info.id;
    }
    REQUIRE(lightType != 0);
    REQUIRE(caffeine_session_add_component(session, entity, lightType) == CAFFEINE_OK);
    for (uint32_t i = 0; i < caffeine_session_entity_component_count(session, entity); ++i) {
        CaffeineComponentInfo info{};
        REQUIRE(caffeine_session_entity_component_at(session, entity, i, &info) ==
                CAFFEINE_OK);
        if (info.type_id == lightType) {
            CaffeineFieldValue value{};
            REQUIRE(caffeine_session_entity_component_field_get(session, entity, i, 0,
                                                                &value) ==
                    CAFFEINE_OK);
            CHECK(value.kind == CAFFEINE_FIELD_COLOR);
        }
    }

    caffeine_session_destroy(session);
}

TEST_CASE("C API snapshots restore the scene", "[capi]") {
    CaffeineSession* session = caffeine_session_create();
    REQUIRE(session != nullptr);

    const CaffeineEntity first = caffeine_session_create_entity(session, "First");
    const CaffeineEntity second = caffeine_session_create_entity(session, "Second");
    REQUIRE(first != CAFFEINE_ENTITY_NONE);
    REQUIRE(second != CAFFEINE_ENTITY_NONE);
    REQUIRE(caffeine_session_set_entity_position(session, first, {1.0f, 2.0f, 3.0f}) ==
            CAFFEINE_OK);

    uint8_t* snapshot = nullptr;
    size_t snapshotSize = 0;
    int dirty = 0;
    REQUIRE(caffeine_session_snapshot(session, &snapshot, &snapshotSize, &dirty) ==
            CAFFEINE_OK);
    REQUIRE(snapshot != nullptr);
    REQUIRE(snapshotSize > 0);

    const CaffeineEntity third = caffeine_session_create_entity(session, "Third");
    REQUIRE(third != CAFFEINE_ENTITY_NONE);
    REQUIRE(caffeine_session_destroy_entity(session, second) == CAFFEINE_OK);

    REQUIRE(caffeine_session_restore(session, snapshot, snapshotSize, dirty) == CAFFEINE_OK);
    caffeine_session_free(snapshot);
    REQUIRE(caffeine_session_entity_count(session) == 2);

    bool foundFirst = false;
    bool foundSecond = false;
    for (uint32_t i = 0; i < caffeine_session_entity_count(session); ++i) {
        CaffeineEntityInfo info{};
        REQUIRE(caffeine_session_entity_at(session, i, &info) == CAFFEINE_OK);
        if (std::strcmp(info.name, "First") == 0) {
            foundFirst = true;
            CaffeineVec3 position{};
            REQUIRE(caffeine_session_entity_position(session, info.id, &position) ==
                    CAFFEINE_OK);
            CHECK(position.x == 1.0f);
            CHECK(position.y == 2.0f);
            CHECK(position.z == 3.0f);
        }
        if (std::strcmp(info.name, "Second") == 0) {
            foundSecond = true;
        }
    }
    CHECK(foundFirst);
    CHECK(foundSecond);

    caffeine_session_destroy(session);
}

TEST_CASE("C API scene load reports missing files", "[capi]") {
    CaffeineSession* session = caffeine_session_create();
    REQUIRE(session != nullptr);
    REQUIRE(caffeine_session_load_scene(session, "/definitely/not/a/scene.caf") == CAFFEINE_ERROR);
    REQUIRE(std::string(caffeine_session_scene_path(session)).empty());
    caffeine_session_destroy(session);
}

TEST_CASE("C API runs a Dryad preview on an entity", "[capi]") {
    if (!caffeine_dryad_available()) {
        WARN("Dryad interpreter is not part of this build");
        return;
    }
    const std::filesystem::path script =
        std::filesystem::temp_directory_path() / "caffeine_capi_preview.dyd";
    {
        std::ofstream out(script);
        out << "function onUpdate(entity, dt) {\n"
               "  caffeine_translate(entity, 0.0, dt, 0.0);\n"
               "}\n";
    }

    CaffeineSession* session = caffeine_session_create();
    REQUIRE(session != nullptr);
    const CaffeineEntity entity = caffeine_session_create_entity(session, "DryadPreview");
    REQUIRE(entity != CAFFEINE_ENTITY_NONE);

    REQUIRE(caffeine_session_run_dryad(session, entity, script.string().c_str(), 0.5f) ==
            CAFFEINE_OK);
    CaffeineVec3 position{};
    REQUIRE(caffeine_session_entity_position(session, entity, &position) == CAFFEINE_OK);
    REQUIRE(position.y == Approx(0.5f));

    REQUIRE(caffeine_session_run_dryad(session, entity, script.string().c_str(), 0.25f) ==
            CAFFEINE_OK);
    REQUIRE(caffeine_session_entity_position(session, entity, &position) == CAFFEINE_OK);
    REQUIRE(position.y == Approx(0.75f));

    std::error_code ec;
    std::filesystem::remove(script, ec);
    caffeine_session_destroy(session);
}

TEST_CASE("C API scale and rotation reach entities that carry both Transform and the split 3D components",
          "[capi]") {
    CaffeineSession* session = caffeine_session_create();
    REQUIRE(session != nullptr);
    const CaffeineEntity entity = caffeine_session_create_entity(session, "Legacy cube");
    REQUIRE(caffeine_session_set_entity_position(session, entity, {1.0f, 2.0f, 3.0f}) == CAFFEINE_OK);
    // Older scenes keep the legacy Transform next to Position/Rotation/Scale 3D (ids 13, 14, 15).
    for (uint32_t type : {13u, 14u, 15u}) {
        REQUIRE(caffeine_session_add_component(session, entity, type) == CAFFEINE_OK);
    }
    REQUIRE(caffeine_session_set_entity_scale(session, entity, {2.0f, 3.0f, 4.0f}) == CAFFEINE_OK);
    REQUIRE(caffeine_session_set_entity_rotation(session, entity, {0.0f, 90.0f, 0.0f}) == CAFFEINE_OK);

    // The renderer builds the world matrix from the split components, so they must hold the edit.
    auto componentField = [&](const char* typeName, CaffeineFieldValue* out) {
        const uint32_t count = caffeine_session_entity_component_count(session, entity);
        for (uint32_t i = 0; i < count; ++i) {
            CaffeineComponentInfo info{};
            REQUIRE(caffeine_session_entity_component_at(session, entity, i, &info) == CAFFEINE_OK);
            if (std::strcmp(info.name, typeName) == 0) {
                return caffeine_session_entity_component_field_get(session, entity, i, 0, out) ==
                       CAFFEINE_OK;
            }
        }
        return false;
    };
    CaffeineFieldValue value{};
    REQUIRE(componentField("Scale 3D", &value));
    CHECK(value.vec[0] == Approx(2.0f));
    CHECK(value.vec[1] == Approx(3.0f));
    CHECK(value.vec[2] == Approx(4.0f));

    CaffeineVec3 read{};
    REQUIRE(caffeine_session_entity_scale(session, entity, &read) == CAFFEINE_OK);
    CHECK(read.y == Approx(3.0f));
    REQUIRE(caffeine_session_entity_rotation(session, entity, &read) == CAFFEINE_OK);
    CHECK(read.y == Approx(90.0f).margin(0.01));
    caffeine_session_destroy(session);
}

TEST_CASE("C API authors a water surface with explicit waves", "[capi][water]") {
    CaffeineSession* session = caffeine_session_create();
    REQUIRE(session != nullptr);
    CaffeineEntity entity = caffeine_session_create_entity(session, "Lake");
    CaffeineWater water{};
    water.enabled = 1;
    water.size_x = 40.0f;
    water.size_z = 40.0f;
    water.depth = 30.0f;
    water.resolution = 64;
    water.time_scale = 1.0f;
    water.albedo[3] = 0.8f;
    water.roughness = 0.05f;
    water.wave_count = 2;
    water.waves[0] = {1.0f, 0.0f, 12.0f, 0.3f, 0.2f, 0.0f};
    water.waves[1] = {0.0f, 1.0f, 5.0f, 0.1f, 0.2f, 1.0f};
    REQUIRE(caffeine_session_set_water(session, entity, &water) == CAFFEINE_OK);

    CaffeineWater back{};
    REQUIRE(caffeine_session_water(session, entity, &back) == CAFFEINE_OK);
    CHECK(back.wave_count == 2);
    CHECK(back.waves[0].wavelength == Approx(12.0f));
    CHECK(back.size_x == Approx(40.0f));

    // The session tick builds the surface, so the height is sampled from the waves.
    for (int i = 0; i < 4; ++i) caffeine_session_tick(session, 0.016f);
    float height = 99.0f;
    REQUIRE(caffeine_session_water_height(session, entity, 3.0f, 2.0f, &height) == CAFFEINE_OK);
    CHECK(std::abs(height) <= 0.41f);
    CHECK(caffeine_session_set_water(session, 9999u, &water) == CAFFEINE_NOT_FOUND);
    caffeine_session_destroy(session);
}

TEST_CASE("C API builds a terrain from generated heights and splat weights", "[capi][terrain]") {
    CaffeineSession* session = caffeine_session_create();
    REQUIRE(session != nullptr);
    CaffeineEntity entity = caffeine_session_create_entity(session, "Hills");
    CaffeineTerrain terrain{};
    terrain.resolution_x = 65;
    terrain.resolution_z = 65;
    terrain.size_x = 200.0f;
    terrain.size_z = 100.0f;
    terrain.max_height = 50.0f;
    terrain.cast_shadows = 1;
    std::vector<float> heights(65 * 65);
    std::vector<uint8_t> splat(65 * 65 * 4, 0);
    for (int z = 0; z < 65; ++z) {
        for (int x = 0; x < 65; ++x) {
            heights[z * 65 + x] = static_cast<float>(x) / 64.0f;  // a ramp along X
            uint8_t* p = &splat[(z * 65 + x) * 4];
            p[x < 32 ? 0 : 1] = 255;
        }
    }
    REQUIRE(caffeine_session_set_terrain(session, entity, &terrain, heights.data(), splat.data()) ==
            CAFFEINE_OK);
    CaffeineTerrain back{};
    REQUIRE(caffeine_session_terrain(session, entity, &back) == CAFFEINE_OK);
    CHECK(back.resolution_x == 65);
    CHECK(back.size_x == Approx(200.0f));
    float h = -1.0f;
    REQUIRE(caffeine_session_terrain_height(session, entity, 100.0f, 50.0f, &h) == CAFFEINE_OK);
    CHECK(h == Approx(25.0f).margin(0.5f));
    REQUIRE(caffeine_session_terrain_height(session, entity, 200.0f, 10.0f, &h) == CAFFEINE_OK);
    CHECK(h == Approx(50.0f).margin(0.5f));
    CHECK(caffeine_session_set_terrain(session, entity, &terrain, nullptr, nullptr) ==
          CAFFEINE_INVALID_ARGUMENT);
    CHECK(caffeine_session_set_terrain(session, 9999u, &terrain, heights.data(), nullptr) ==
          CAFFEINE_NOT_FOUND);
    caffeine_session_destroy(session);
}

TEST_CASE("C API terrain masks: built-ins from the heightfield, stored masks, the normal, a rectangle",
          "[capi][terrain][masks]") {
    CaffeineSession* session = caffeine_session_create();
    REQUIRE(session != nullptr);
    CaffeineEntity entity = caffeine_session_create_entity(session, "Hills");
    CaffeineEntity bare = caffeine_session_create_entity(session, "No terrain");
    CaffeineTerrain terrain{};
    terrain.resolution_x = 65;
    terrain.resolution_z = 65;
    terrain.size_x = 64.0f;
    terrain.size_z = 64.0f;
    terrain.max_height = 20.0f;
    std::vector<float> heights(65 * 65);
    for (int z = 0; z < 65; ++z) {
        for (int x = 0; x < 65; ++x) heights[z * 65 + x] = std::min(1.0f, static_cast<float>(x) / 64.0f);  // 0.3125 m/m
    }
    REQUIRE(caffeine_session_set_terrain(session, entity, &terrain, heights.data(), nullptr) == CAFFEINE_OK);

    // The three built-ins come first.
    uint32_t count = 0;
    REQUIRE(caffeine_session_terrain_mask_count(session, entity, &count) == CAFFEINE_OK);
    CHECK(count == 3u);
    CaffeineTerrainMaskInfo info{};
    REQUIRE(caffeine_session_terrain_mask_at(session, entity, 1, &info) == CAFFEINE_OK);
    CHECK(std::strcmp(info.name, "slope") == 0);
    CHECK(info.builtin == 1);
    CHECK(info.resolution_x == 0u);
    CHECK(caffeine_session_terrain_mask_at(session, entity, 3, &info) == CAFFEINE_NOT_FOUND);

    float value = -1.0f;
    REQUIRE(caffeine_session_terrain_mask(session, entity, "height", 32.0f, 10.0f, &value) == CAFFEINE_OK);
    CHECK(value == Approx(0.5f).margin(0.01f));
    REQUIRE(caffeine_session_terrain_mask(session, entity, "slope", 32.0f, 10.0f, &value) == CAFFEINE_OK);
    CHECK(value == Approx(std::atan(20.0f / 64.0f) / 1.57079632679f).margin(0.01f));
    float normal[3] = {};
    REQUIRE(caffeine_session_terrain_normal(session, entity, 32.0f, 10.0f, normal) == CAFFEINE_OK);
    const float slope = 20.0f / 64.0f;
    CHECK(normal[0] == Approx(-slope / std::sqrt(1.0f + slope * slope)).margin(0.01f));
    CHECK(normal[1] == Approx(1.0f / std::sqrt(1.0f + slope * slope)).margin(0.01f));

    // A stored mask: set, listed after the built-ins, read, saved with its name, removed.
    const std::vector<uint8_t> wet = {0, 255, 0, 255};  // 2 x 2 (z rows): dry on the left, wet on the right
    REQUIRE(caffeine_session_set_terrain_mask(session, entity, "wetness", 2, 2, wet.data()) == CAFFEINE_OK);
    REQUIRE(caffeine_session_terrain_mask_count(session, entity, &count) == CAFFEINE_OK);
    CHECK(count == 4u);
    REQUIRE(caffeine_session_terrain_mask_at(session, entity, 3, &info) == CAFFEINE_OK);
    CHECK(std::strcmp(info.name, "wetness") == 0);
    CHECK(info.builtin == 0);
    CHECK(info.resolution_x == 2u);
    REQUIRE(caffeine_session_terrain_mask(session, entity, "wetness", 32.0f, 10.0f, &value) == CAFFEINE_OK);
    CHECK(value == Approx(0.5f).margin(0.01f));
    uint8_t grid[4 * 2] = {};
    REQUIRE(caffeine_session_terrain_mask_rect(session, entity, "wetness", 0.0f, 0.0f, 64.0f, 64.0f, 4, 2, grid) ==
            CAFFEINE_OK);
    CHECK(grid[0] < grid[3]);  // left to right
    CHECK(grid[3] > 200);
    CHECK(grid[0] < 60);

    // Errors: unknown mask, bad names, built-in names, no terrain, a missing entity.
    CHECK(caffeine_session_terrain_mask(session, entity, "nope", 1.0f, 1.0f, &value) == CAFFEINE_NOT_FOUND);
    CHECK(caffeine_session_set_terrain_mask(session, entity, "slope", 2, 2, wet.data()) == CAFFEINE_INVALID_ARGUMENT);
    CHECK(caffeine_session_set_terrain_mask(session, entity, "bad name", 2, 2, wet.data()) == CAFFEINE_INVALID_ARGUMENT);
    CHECK(caffeine_session_set_terrain_mask(session, entity, "tiny", 1, 2, wet.data()) == CAFFEINE_INVALID_ARGUMENT);
    CHECK(caffeine_session_terrain_mask(session, bare, "height", 1.0f, 1.0f, &value) == CAFFEINE_NOT_FOUND);
    CHECK(caffeine_session_terrain_mask(session, 9999u, "height", 1.0f, 1.0f, &value) == CAFFEINE_NOT_FOUND);
    CHECK(caffeine_session_terrain_mask(session, entity, nullptr, 1.0f, 1.0f, &value) == CAFFEINE_INVALID_ARGUMENT);
    REQUIRE(caffeine_session_set_terrain_mask(session, entity, "wetness", 0, 0, nullptr) == CAFFEINE_OK);
    CHECK(caffeine_session_terrain_mask(session, entity, "wetness", 1.0f, 1.0f, &value) == CAFFEINE_NOT_FOUND);
    CHECK(caffeine_session_set_terrain_mask(session, entity, "wetness", 0, 0, nullptr) == CAFFEINE_NOT_FOUND);
    caffeine_session_destroy(session);
}

TEST_CASE("C API scatter on a terrain: placement by the terrain, the biome table, orientation, errors",
          "[capi][terrain][scatter]") {
    CaffeineSession* session = caffeine_session_create();
    REQUIRE(session != nullptr);
    CaffeineEntity entity = caffeine_session_create_entity(session, "Hills");
    CaffeineEntity bare = caffeine_session_create_entity(session, "No terrain");
    REQUIRE(caffeine_session_set_entity_position(session, entity, CaffeineVec3{100.0f, 5.0f, -50.0f}) == CAFFEINE_OK);
    CaffeineTerrain terrain{};
    terrain.resolution_x = 65;
    terrain.resolution_z = 65;
    terrain.size_x = 120.0f;
    terrain.size_z = 120.0f;
    terrain.max_height = 20.0f;
    std::vector<float> heights(65 * 65);
    for (int z = 0; z < 65; ++z) {
        for (int x = 0; x < 65; ++x) heights[z * 65 + x] = 0.3f + 0.2f * std::sin(static_cast<float>(x) * 0.12f);
    }
    REQUIRE(caffeine_session_set_terrain(session, entity, &terrain, heights.data(), nullptr) == CAFFEINE_OK);
    // The right half of the terrain (x > 60 m) is wet.
    std::vector<uint8_t> wet(64 * 2);
    for (int z = 0; z < 2; ++z) {
        for (int x = 0; x < 64; ++x) wet[z * 64 + x] = x >= 32 ? 255 : 0;
    }
    REQUIRE(caffeine_session_set_terrain_mask(session, entity, "wetness", 64, 2, wet.data()) == CAFFEINE_OK);

    CaffeineScatterClass tree{};
    tree.id = 2; tree.priority = 20; tree.cell_size = 6.0f; tree.radius = 2.0f; tree.density = 0.9f;
    tree.min_scale = 0.8f; tree.max_scale = 1.3f; tree.tilt_to_normal = 0.5f; tree.sink = 0.1f; tree.tint_jitter = 0.05f;
    tree.variants = 3;
    CaffeineScatterClass grass = tree;
    grass.id = 3; grass.priority = 5; grass.cell_size = 3.0f; grass.radius = 0.5f; grass.tilt_to_normal = 1.0f; grass.sink = 0.0f;
    const CaffeineScatterClass classes[2] = {tree, grass};
    const char* biome = "class 2\n  wetness 0 0 0.3 0.6\n";  // trees stay out of the wet half

    uint32_t total = 0;
    REQUIRE(caffeine_session_scatter_terrain(session, entity, 7, classes, 2, biome, nullptr, nullptr, 0, &total) ==
            CAFFEINE_OK);
    REQUIRE(total > 200u);
    std::vector<CaffeineScatterInstance> list(total);
    uint32_t again = 0;
    REQUIRE(caffeine_session_scatter_terrain(session, entity, 7, classes, 2, biome, nullptr, list.data(),
                                             static_cast<uint32_t>(list.size()), &again) == CAFFEINE_OK);
    CHECK(again == total);

    size_t trees = 0, grasses = 0;
    for (const CaffeineScatterInstance& i : list) {
        // Scene space: inside the terrain, which is centred on its entity at (100, 5, -50).
        CHECK(i.position[0] >= 100.0f - 60.0f - 0.01f);
        CHECK(i.position[0] <= 100.0f + 60.0f + 0.01f);
        CHECK(i.position[2] >= -50.0f - 60.0f - 0.01f);
        CHECK(i.position[2] <= -50.0f + 60.0f + 0.01f);
        // On the surface (the entity's height added, the tree's sink taken off).
        float ground = 0.0f;
        REQUIRE(caffeine_session_terrain_height(session, entity, i.position[0] - 100.0f + 60.0f,
                                                i.position[2] + 50.0f + 60.0f, &ground) == CAFFEINE_OK);
        const float sink = i.class_id == 2 ? 0.1f * i.scale : 0.0f;
        CHECK(i.position[1] == Approx(5.0f + ground - sink).margin(0.05f));
        CHECK(i.scale >= 0.79f);
        CHECK(i.scale <= 1.31f);
        CHECK(i.variant < 3u);
        // The Euler angles carry +Y onto `up`.
        const Caffeine::Quat q = Caffeine::Quat::fromEuler(i.rotation_degrees[0] * 0.017453292f,
                                                            i.rotation_degrees[1] * 0.017453292f,
                                                            i.rotation_degrees[2] * 0.017453292f);
        const Caffeine::Vec3 axis = q.rotate(Caffeine::Vec3(0.0f, 1.0f, 0.0f));
        CHECK(axis.x == Approx(i.up[0]).margin(2e-3f));
        CHECK(axis.y == Approx(i.up[1]).margin(2e-3f));
        CHECK(axis.z == Approx(i.up[2]).margin(2e-3f));
        if (i.class_id == 2) {
            ++trees;
            CHECK(i.position[0] < 100.0f + 5.0f);  // x < 60 m from the corner, with the mask's soft edge: the dry half
        } else {
            ++grasses;
        }
    }
    CHECK(trees > 20u);
    CHECK(grasses > trees);

    // A smaller buffer is filled and the total still says how many there are. A region restricts the scatter.
    std::vector<CaffeineScatterInstance> few(10);
    uint32_t t2 = 0;
    REQUIRE(caffeine_session_scatter_terrain(session, entity, 7, classes, 2, biome, nullptr, few.data(), 10, &t2) ==
            CAFFEINE_OK);
    CHECK(t2 == total);
    CHECK(few[3].position[0] == list[3].position[0]);
    const float region[4] = {0.0f, 0.0f, 30.0f, 120.0f};
    uint32_t part = 0;
    REQUIRE(caffeine_session_scatter_terrain(session, entity, 7, classes, 2, biome, region, nullptr, 0, &part) ==
            CAFFEINE_OK);
    CHECK(part > 0u);
    CHECK(part < total / 2);
    // Another seed, another forest.
    std::vector<CaffeineScatterInstance> reseeded(total + 100);
    uint32_t t3 = 0;
    REQUIRE(caffeine_session_scatter_terrain(session, entity, 8, classes, 2, biome, nullptr, reseeded.data(),
                                             static_cast<uint32_t>(reseeded.size()), &t3) == CAFFEINE_OK);
    CHECK(reseeded[0].position[0] != list[0].position[0]);

    // Errors.
    uint32_t n = 0;
    CHECK(caffeine_session_scatter_terrain(session, entity, 7, classes, 2, "class x\n", nullptr, nullptr, 0, &n) ==
          CAFFEINE_INVALID_ARGUMENT);
    CaffeineScatterClass broken = tree;
    broken.radius = 0.0f;
    CHECK(caffeine_session_scatter_terrain(session, entity, 7, &broken, 1, nullptr, nullptr, nullptr, 0, &n) ==
          CAFFEINE_INVALID_ARGUMENT);
    CHECK(caffeine_session_scatter_terrain(session, entity, 7, classes, 0, nullptr, nullptr, nullptr, 0, &n) ==
          CAFFEINE_INVALID_ARGUMENT);
    CHECK(caffeine_session_scatter_terrain(session, bare, 7, classes, 2, nullptr, nullptr, nullptr, 0, &n) ==
          CAFFEINE_NOT_FOUND);
    char why[128] = {};
    CHECK(caffeine_biome_check("class 2\n  slope 0 0 0.2 0.5\n", why, sizeof(why)) == CAFFEINE_OK);
    CHECK(caffeine_biome_check("class 2\n  slope 0.5 0.2 1 1\n", why, sizeof(why)) == CAFFEINE_INVALID_ARGUMENT);
    CHECK(std::strstr(why, "line 2") != nullptr);
    caffeine_session_destroy(session);
}

TEST_CASE("CPU load is computed the same way from any platform's counters", "[profiler][metrics]") {
    using namespace Caffeine::Debug;
    CpuSnapshot a;
    CpuSnapshot b;
    a.cores = {{100, 1000}, {500, 1000}, {0, 0}};
    b.cores = {{150, 1100}, {500, 1200}, {0, 0}};  // core 0: 50 busy of 100, core 1: idle, core 2: no time passed
    a.all = {600, 2000};
    b.all = {650, 2300};
    a.processSeconds = 10.0;
    b.processSeconds = 11.5;  // 1.5 s of CPU in 1 s of wall time: one and a half cores

    MachineMetrics m;
    cpuLoadFromSnapshots(a, b, 1.0, m);
    REQUIRE(m.cpuCoreCount == 3);
    CHECK(m.cpuCorePercent[0] == Approx(50.0f));
    CHECK(m.cpuCorePercent[1] == Approx(0.0f));
    CHECK(m.cpuCorePercent[2] == Approx(0.0f));
    CHECK(m.cpuTotalPercent == Approx(100.0f * 50.0f / 300.0f));
    CHECK(m.processCpuPercent == Approx(150.0f));

    // A core that came or went: the count is reported, the percentages have no baseline and stay at zero.
    CpuSnapshot fewer = a;
    fewer.cores.pop_back();
    MachineMetrics changed;
    cpuLoadFromSnapshots(fewer, b, 1.0, changed);
    CHECK(changed.cpuCoreCount == 3);
    CHECK(changed.cpuCorePercent[0] == 0.0f);

    // A counter that went backwards counts as no time; the others still count.
    CpuSnapshot wrapped = b;
    wrapped.cores[0] = {10, 50};
    MachineMetrics back;
    cpuLoadFromSnapshots(a, wrapped, 1.0, back);
    CHECK(back.cpuCorePercent[0] == 0.0f);
    CHECK(back.cpuCorePercent[1] == Approx(0.0f));

    // Busy cannot exceed total, whatever the counters say.
    CpuSnapshot odd;
    odd.cores = {{0, 0}};
    CpuSnapshot odd2;
    odd2.cores = {{500, 100}};
    MachineMetrics clamped;
    cpuLoadFromSnapshots(odd, odd2, 1.0, clamped);
    CHECK(clamped.cpuCorePercent[0] == Approx(100.0f));

    // More cores than the report holds: the count is capped and nothing is written past the array.
    CpuSnapshot big;
    CpuSnapshot big2;
    big.cores.assign(100, {0, 0});
    big2.cores.assign(100, {50, 100});
    MachineMetrics many;
    cpuLoadFromSnapshots(big, big2, 1.0, many);
    CHECK(many.cpuCoreCount == kMaxCpuCores);
    CHECK(many.cpuCorePercent[kMaxCpuCores - 1] == Approx(50.0f));

    // No elapsed time: the process figure stays at zero instead of dividing by it.
    MachineMetrics none;
    cpuLoadFromSnapshots(a, b, 0.0, none);
    CHECK(none.processCpuPercent == 0.0f);
}

TEST_CASE("C API reads and writes a 2D rig and its PhaseShift parameters", "[capi][animation]") {
    CaffeineSession* session = caffeine_session_create();
    REQUIRE(session != nullptr);
    const CaffeineEntity entity = caffeine_session_create_entity(session, "Hero");

    // One root bone, one parameter that rotates it, two vertices.
    Caffeine::Animation::Rig2D rig;
    Caffeine::Animation::Bone2D root;
    root.name = "root";
    rig.skeleton.bones = {root};
    Caffeine::Animation::Parameter2D bend;
    bend.name = "Bend";
    bend.minValue = 0.0f;
    bend.maxValue = 1.0f;
    rig.parameters = {bend};
    Caffeine::Animation::ParamBinding2D turn;
    turn.parameter = 0;
    turn.bone = 0;
    turn.keys = {{0.0f, 0.0f}, {1.0f, 1.0f}};
    rig.bindings = {turn};
    Caffeine::Animation::Vertex2D v;
    v.position = Caffeine::Vec2(1.0f, 0.0f);
    v.influences = {{0, 1.0f}};
    rig.vertices = {v, v};
    const auto rigPath = std::filesystem::temp_directory_path() / "caffeine_capi_hero.rig2d";
    REQUIRE(Caffeine::Animation::saveRig2D(rigPath, rig));

    // Before a rig is attached the entity reports nothing loaded.
    CaffeineRig2DCounts none{};
    REQUIRE(caffeine_session_rig2d_counts(session, entity, &none) == CAFFEINE_OK);
    CHECK(none.loaded == 0);

    REQUIRE(caffeine_session_rig2d_attach(session, entity, rigPath.string().c_str()) == CAFFEINE_OK);
    CaffeineRig2DCounts counts{};
    REQUIRE(caffeine_session_rig2d_counts(session, entity, &counts) == CAFFEINE_OK);
    CHECK(counts.loaded == 1);
    CHECK(counts.bones == 1);
    CHECK(counts.vertices == 2);
    CHECK(counts.parameters == 1);
    CHECK(counts.bindings == 1);

    CaffeineRig2DParameter param{};
    REQUIRE(caffeine_session_rig2d_parameter(session, entity, 0, &param) == CAFFEINE_OK);
    CHECK(std::strcmp(param.name, "Bend") == 0);
    CHECK(param.maximum == Approx(1.0f));

    // Setting a rig parameter writes the PhaseShift float with the same name, clamped to the range.
    REQUIRE(caffeine_session_rig2d_set_parameter(session, entity, 0, 4.0f) == CAFFEINE_OK);
    CaffeinePhaseShiftCounts ps{};
    REQUIRE(caffeine_session_phase_shift_counts(session, entity, &ps) == CAFFEINE_OK);
    CHECK(ps.parameters == 1);
    CaffeinePhaseShiftParameter psParam{};
    REQUIRE(caffeine_session_phase_shift_parameter(session, entity, 0, &psParam) == CAFFEINE_OK);
    CHECK(std::strcmp(psParam.name, "Bend") == 0);
    CHECK(psParam.kind == 1u);  // float
    CHECK(psParam.value == Approx(1.0f));

    REQUIRE(caffeine_session_phase_shift_set_smoothing(session, entity, 0, 0.2f) == CAFFEINE_OK);
    REQUIRE(caffeine_session_phase_shift_parameter(session, entity, 0, &psParam) == CAFFEINE_OK);
    CHECK(psParam.smoothing == Approx(0.2f));

    // A tick evaluates the rig, so the parameter now reports the value the rig used.
    caffeine_session_tick(session, 0.016f);
    REQUIRE(caffeine_session_rig2d_parameter(session, entity, 0, &param) == CAFFEINE_OK);
    CHECK(param.value == Approx(1.0f));

    CHECK(caffeine_session_rig2d_bone(session, entity, 9, nullptr) == CAFFEINE_INVALID_ARGUMENT);
    CHECK(caffeine_session_rig2d_counts(session, 9999u, &counts) == CAFFEINE_NOT_FOUND);
    CHECK(caffeine_session_phase_shift_counts(session, 9999u, &ps) == CAFFEINE_NOT_FOUND);
    CHECK(caffeine_session_rig2d_attach(session, entity, nullptr) == CAFFEINE_INVALID_ARGUMENT);
    std::filesystem::remove(rigPath);
    caffeine_session_destroy(session);
}

#ifdef CF_HAS_SDL3
TEST_CASE("C API draws an attached 2D rig on the GPU over the scene", "[capi][gpu][animation]") {
    CaffeineSession* session = caffeine_session_create();
    REQUIRE(session != nullptr);
    const CaffeineEntity entity = caffeine_session_create_entity(session, "Hero");

    const uint32_t w = 128;
    const uint32_t h = 128;
    const size_t bytes = static_cast<size_t>(w) * h * 4u;
    std::vector<uint8_t> before(bytes), after(bytes);
    REQUIRE(caffeine_session_render(session, w, h, before.data(), bytes) == CAFFEINE_OK);

    // A large white quad, so it covers a good part of the view wherever the camera looks.
    Caffeine::Animation::Rig2D rig;
    Caffeine::Animation::Bone2D root;
    root.name = "root";
    rig.skeleton.bones = {root};
    Caffeine::Animation::buildGridMesh2D(rig, 1, 1);
    // Rest vertices live in skeleton space, so the size is in the vertices, not the bone's rest scale.
    for (auto& v : rig.vertices) v.position *= 400.0f;
    Caffeine::Animation::bindVerticesToBone2D(rig, 0);
    const auto rigPath = std::filesystem::temp_directory_path() / "caffeine_gpu_quad.rig2d";
    REQUIRE(Caffeine::Animation::saveRig2D(rigPath, rig));
    REQUIRE(caffeine_session_rig2d_attach(session, entity, rigPath.string().c_str()) == CAFFEINE_OK);
    caffeine_session_tick(session, 0.016f);  // poses the rig (AnimationSystem)

    REQUIRE(caffeine_session_render(session, w, h, after.data(), bytes) == CAFFEINE_OK);

    size_t changed = 0;
    size_t whitish = 0;
    for (size_t i = 0; i < bytes; i += 4) {
        if (before[i] != after[i] || before[i + 1] != after[i + 1] || before[i + 2] != after[i + 2]) {
            ++changed;
            if (after[i] > 200 && after[i + 1] > 200 && after[i + 2] > 200) ++whitish;
        }
    }
    INFO("pixels changed by the rig: " << changed << ", of them near white: " << whitish);
    CHECK(changed > 50);
    CHECK(whitish > 50);
    std::filesystem::remove(rigPath);
    caffeine_session_destroy(session);
}
#endif

#ifdef CF_HAS_SDL3
static uint32_t componentTypeId(const char* name) {
    for (uint32_t i = 0; i < caffeine_component_type_count(); ++i) {
        CaffeineComponentTypeInfo info{};
        if (caffeine_component_type_at(i, &info) == CAFFEINE_OK && std::strcmp(info.name, name) == 0) return info.id;
    }
    return 0;
}

static uint32_t componentFieldIndex(uint32_t type, const char* name) {
    CaffeineComponentTypeInfo info{};
    for (uint32_t i = 0; i < caffeine_component_type_count(); ++i) {
        if (caffeine_component_type_at(i, &info) == CAFFEINE_OK && info.id == type) break;
    }
    for (uint32_t f = 0; f < 16; ++f) {
        CaffeineFieldInfo field{};
        if (caffeine_component_type_field_at(type, f, &field) != CAFFEINE_OK) break;
        if (std::strcmp(field.name, name) == 0) return f;
    }
    return UINT32_MAX;
}

static uint32_t componentIndexOf(CaffeineSession* session, CaffeineEntity entity, uint32_t type) {
    const uint32_t count = caffeine_session_entity_component_count(session, entity);
    for (uint32_t i = 0; i < count; ++i) {
        CaffeineComponentInfo info{};
        if (caffeine_session_entity_component_at(session, entity, i, &info) == CAFFEINE_OK && info.type_id == type) {
            return i;
        }
    }
    return UINT32_MAX;
}

TEST_CASE("C API draws a textured sprite on the GPU over the scene", "[capi][gpu][sprite]") {
    const uint32_t w = 128;
    const uint32_t h = 128;
    const size_t bytes = static_cast<size_t>(w) * h * 4u;

    // An 8x8 solid red image: any red pixel in the frame came from the sprite's texture.
    std::vector<uint8_t> redPixels(8 * 8 * 4);
    for (size_t i = 0; i < redPixels.size(); i += 4) {
        redPixels[i] = 255;
        redPixels[i + 1] = 0;
        redPixels[i + 2] = 0;
        redPixels[i + 3] = 255;
    }
    const auto imagePath = std::filesystem::temp_directory_path() / "caffeine_gpu_red.png";
    REQUIRE(stbi_write_png(imagePath.string().c_str(), 8, 8, 4, redPixels.data(), 8 * 4) != 0);

    CaffeineSession* session = caffeine_session_create();
    REQUIRE(session != nullptr);
    const CaffeineEntity entity = caffeine_session_create_entity(session, "Sprite");
    REQUIRE(caffeine_session_set_entity_position(session, entity, {0.0f, 0.0f, 0.0f}) == CAFFEINE_OK);

    std::vector<uint8_t> before(bytes), after(bytes);
    REQUIRE(caffeine_session_render(session, w, h, before.data(), bytes) == CAFFEINE_OK);

    // The sprite component: its Name is the image path, and the entity's Transform scales the quad up.
    const uint32_t spriteType = componentTypeId("Sprite");
    REQUIRE(spriteType != 0);
    REQUIRE(caffeine_session_add_component(session, entity, spriteType) == CAFFEINE_OK);
    const uint32_t spriteIndex = componentIndexOf(session, entity, spriteType);
    REQUIRE(spriteIndex != UINT32_MAX);
    const uint32_t nameField = componentFieldIndex(spriteType, "Name");
    REQUIRE(nameField != UINT32_MAX);
    CaffeineFieldValue name{};
    name.kind = CAFFEINE_FIELD_STRING;
    std::strncpy(name.text, imagePath.string().c_str(), sizeof(name.text) - 1);
    REQUIRE(caffeine_session_entity_component_field_set(session, entity, spriteIndex, nameField, &name) == CAFFEINE_OK);

    const uint32_t transformType = componentTypeId("Transform");
    REQUIRE(transformType != 0);
    const uint32_t transformIndex = componentIndexOf(session, entity, transformType);
    const uint32_t scaleField = componentFieldIndex(transformType, "Scale");
    REQUIRE(scaleField != UINT32_MAX);
    CaffeineFieldValue scale{};
    scale.kind = CAFFEINE_FIELD_VEC3;
    scale.vec[0] = 400.0f;
    scale.vec[1] = 400.0f;
    scale.vec[2] = 1.0f;
    REQUIRE(caffeine_session_entity_component_field_set(session, entity, transformIndex, scaleField, &scale) == CAFFEINE_OK);

    REQUIRE(caffeine_session_render(session, w, h, after.data(), bytes) == CAFFEINE_OK);

    size_t changed = 0;
    size_t red = 0;
    for (size_t i = 0; i < bytes; i += 4) {
        if (before[i] != after[i] || before[i + 1] != after[i + 1] || before[i + 2] != after[i + 2]) {
            ++changed;
            if (after[i] > 200 && after[i + 1] < 60 && after[i + 2] < 60) ++red;
        }
    }
    INFO("pixels changed by the sprite: " << changed << ", of them red: " << red);
    CHECK(changed > 50);
    CHECK(red > 50);
    std::filesystem::remove(imagePath);
    caffeine_session_destroy(session);
}
#endif

TEST_CASE("C API animation advances only while the scene simulates", "[capi][animation]") {
    CaffeineSession* session = caffeine_session_create();
    REQUIRE(session != nullptr);
    const CaffeineEntity entity = caffeine_session_create_entity(session, "Smoother");

    Caffeine::Animation::Rig2D rig;
    Caffeine::Animation::Bone2D root;
    root.name = "root";
    rig.skeleton.bones = {root};
    Caffeine::Animation::Parameter2D bend;
    bend.name = "Bend";
    bend.minValue = 0.0f;
    bend.maxValue = 1.0f;
    rig.parameters = {bend};
    const auto rigPath = std::filesystem::temp_directory_path() / "caffeine_capi_smooth.rig2d";
    REQUIRE(Caffeine::Animation::saveRig2D(rigPath, rig));
    REQUIRE(caffeine_session_rig2d_attach(session, entity, rigPath.string().c_str()) == CAFFEINE_OK);

    // Creates the PhaseShift float, smooths it, then asks for 1.0: the spring starts at 0.
    REQUIRE(caffeine_session_rig2d_set_parameter(session, entity, 0, 0.0f) == CAFFEINE_OK);
    REQUIRE(caffeine_session_phase_shift_set_smoothing(session, entity, 0, 0.5f) == CAFFEINE_OK);
    REQUIRE(caffeine_session_rig2d_set_parameter(session, entity, 0, 1.0f) == CAFFEINE_OK);

    CaffeineRig2DParameter param{};
    caffeine_session_set_simulating(session, 0);
    for (int i = 0; i < 120; ++i) caffeine_session_tick(session, 1.0f / 60.0f);
    REQUIRE(caffeine_session_rig2d_parameter(session, entity, 0, &param) == CAFFEINE_OK);
    CHECK(param.value == Approx(0.0f));

    caffeine_session_set_simulating(session, 1);
    for (int i = 0; i < 120; ++i) caffeine_session_tick(session, 1.0f / 60.0f);
    REQUIRE(caffeine_session_rig2d_parameter(session, entity, 0, &param) == CAFFEINE_OK);
    CHECK(param.value > 0.9f);

    std::filesystem::remove(rigPath);
    caffeine_session_destroy(session);
}

TEST_CASE("C API adds a PhaseShift and its parameters from the editor", "[capi][animation]") {
    CaffeineSession* session = caffeine_session_create();
    REQUIRE(session != nullptr);
    const CaffeineEntity entity = caffeine_session_create_entity(session, "Hero");

    CaffeinePhaseShiftCounts counts{};
    REQUIRE(caffeine_session_phase_shift_counts(session, entity, &counts) == CAFFEINE_NOT_FOUND);
    CHECK(caffeine_session_phase_shift_add_parameter(session, entity, "speed", 1) == CAFFEINE_NOT_FOUND);

    REQUIRE(caffeine_session_phase_shift_add(session, entity) == CAFFEINE_OK);
    REQUIRE(caffeine_session_phase_shift_add(session, entity) == CAFFEINE_OK);  // no-op when present
    REQUIRE(caffeine_session_phase_shift_counts(session, entity, &counts) == CAFFEINE_OK);
    CHECK(counts.parameters == 0);

    REQUIRE(caffeine_session_phase_shift_add_parameter(session, entity, "speed", 1) == CAFFEINE_OK);
    REQUIRE(caffeine_session_phase_shift_add_parameter(session, entity, "grounded", 0) == CAFFEINE_OK);
    // Duplicate names, unknown kinds and empty or over-long names are refused.
    CHECK(caffeine_session_phase_shift_add_parameter(session, entity, "speed", 1) == CAFFEINE_INVALID_ARGUMENT);
    CHECK(caffeine_session_phase_shift_add_parameter(session, entity, "bad", 9) == CAFFEINE_INVALID_ARGUMENT);
    CHECK(caffeine_session_phase_shift_add_parameter(session, entity, "", 1) == CAFFEINE_INVALID_ARGUMENT);
    const std::string longName(40, 'x');
    CHECK(caffeine_session_phase_shift_add_parameter(session, entity, longName.c_str(), 1) == CAFFEINE_INVALID_ARGUMENT);

    REQUIRE(caffeine_session_phase_shift_counts(session, entity, &counts) == CAFFEINE_OK);
    CHECK(counts.parameters == 2);
    CaffeinePhaseShiftParameter first{};
    REQUIRE(caffeine_session_phase_shift_parameter(session, entity, 0, &first) == CAFFEINE_OK);
    CHECK(std::strcmp(first.name, "speed") == 0);
    CHECK(first.kind == 1u);

    REQUIRE(caffeine_session_phase_shift_remove_parameter(session, entity, 0) == CAFFEINE_OK);
    REQUIRE(caffeine_session_phase_shift_counts(session, entity, &counts) == CAFFEINE_OK);
    CHECK(counts.parameters == 1);
    REQUIRE(caffeine_session_phase_shift_parameter(session, entity, 0, &first) == CAFFEINE_OK);
    CHECK(std::strcmp(first.name, "grounded") == 0);
    CHECK(caffeine_session_phase_shift_remove_parameter(session, entity, 5) == CAFFEINE_NOT_FOUND);
    caffeine_session_destroy(session);
}

TEST_CASE("C ABI asset references decide what a path means", "[capi][assets][references]") {
    namespace fs = std::filesystem;
    const fs::path base = fs::temp_directory_path() / "caffeine-capi-references";
    std::error_code ec;
    fs::remove_all(base, ec);
    const fs::path assets = base / "project" / "Assets";
    const fs::path outside = base / "downloads";
    fs::create_directories(assets / "Textures", ec);
    fs::create_directories(outside, ec);
    { std::ofstream(assets / "Textures" / "brick.png", std::ios::binary) << "brick"; }
    { std::ofstream(outside / "rock.png", std::ios::binary) << "rock"; }

    CaffeineSession* session = caffeine_session_create();
    REQUIRE(session != nullptr);
    REQUIRE(caffeine_session_set_asset_root(session, assets.string().c_str()) == CAFFEINE_OK);

    CaffeineAssetReference ref{};
    REQUIRE(caffeine_session_asset_reference(session, "Textures/brick.png", 0, &ref) == CAFFEINE_OK);
    CHECK(ref.status == CAFFEINE_REF_OK);
    CHECK(std::string(ref.asset_path) == "Textures/brick.png");
    CHECK(std::string(ref.kind) == "image");
    CHECK(ref.imported == 0);

    REQUIRE(caffeine_session_asset_reference(session, "nothing.png", 0, &ref) == CAFFEINE_OK);
    CHECK(ref.status == CAFFEINE_REF_MISSING);
    REQUIRE(caffeine_session_asset_reference(session, "", 0, &ref) == CAFFEINE_OK);
    CHECK(ref.status == CAFFEINE_REF_EMPTY);
    REQUIRE(caffeine_session_asset_reference(session, "../downloads/rock.png", 0, &ref) == CAFFEINE_OK);
    CHECK(ref.status == CAFFEINE_REF_MISSING);  // a relative path never climbs out of the project

    const std::string rock = (outside / "rock.png").string();
    REQUIRE(caffeine_session_asset_reference(session, rock.c_str(), 0, &ref) == CAFFEINE_OK);
    CHECK(ref.status == CAFFEINE_REF_OUTSIDE_PROJECT);
    CHECK_FALSE(fs::exists(assets / "Textures" / "rock.png"));  // looking does not import

    REQUIRE(caffeine_session_asset_reference(session, rock.c_str(), 1, &ref) == CAFFEINE_OK);
    CHECK(ref.status == CAFFEINE_REF_OK);
    CHECK(ref.imported == 1);
    CHECK(std::string(ref.asset_path) == "Textures/rock.png");
    CHECK(fs::exists(assets / "Textures" / "rock.png"));
    // The import shows up in the project's asset list.
    bool listed = false;
    for (uint32_t i = 0; i < caffeine_session_asset_count(session); ++i) {
        CaffeineAssetInfo info{};
        REQUIRE(caffeine_session_asset_at(session, i, &info) == CAFFEINE_OK);
        if (std::string(info.relative_path) == "Textures/rock.png") listed = true;
    }
    CHECK(listed);

    CHECK(caffeine_session_asset_reference(nullptr, "x", 0, &ref) == CAFFEINE_INVALID_ARGUMENT);
    CHECK(caffeine_session_asset_reference(session, "x", 0, nullptr) == CAFFEINE_INVALID_ARGUMENT);
    caffeine_session_destroy(session);
    fs::remove_all(base, ec);
}

TEST_CASE("C API terrain strokes: stamps by metre, a local rebuild, exact undo and redo", "[capi][terrain][edit]") {
    CaffeineSession* session = caffeine_session_create();
    REQUIRE(session != nullptr);
    CaffeineEntity entity = caffeine_session_create_entity(session, "Ground");
    CaffeineTerrain terrain{};
    terrain.resolution_x = 257;
    terrain.resolution_z = 257;
    terrain.size_x = 512.0f;
    terrain.size_z = 512.0f;
    terrain.max_height = 80.0f;
    std::vector<float> heights(257 * 257, 0.5f);
    REQUIRE(caffeine_session_set_terrain(session, entity, &terrain, heights.data(), nullptr) == CAFFEINE_OK);

    auto heightAt = [&](float x, float z) {
        float h = 0.0f;
        REQUIRE(caffeine_session_terrain_height(session, entity, x + 256.0f, z + 256.0f, &h) == CAFFEINE_OK);
        return h;
    };
    const float before = heightAt(0.0f, 0.0f);
    CHECK(before == Approx(40.0f).margin(0.01f));

    CaffeineTerrainBrush brush{};
    brush.mode = CAFFEINE_BRUSH_RAISE;
    brush.radius_m = 10.0f;
    brush.strength = 2.0f;  // metres per stamp
    brush.spacing_m = 5.0f;
    brush.falloff = CAFFEINE_FALLOFF_SMOOTH;
    REQUIRE(caffeine_session_terrain_stroke_begin(session, entity, &brush, nullptr, 0) == CAFFEINE_OK);
    uint32_t stamps = 0;
    REQUIRE(caffeine_session_terrain_stroke_point(session, 0.0, 0.0, 0.0, 1.0f, &stamps) == CAFFEINE_OK);
    CHECK(stamps == 1);
    REQUIRE(caffeine_session_terrain_stroke_point(session, 50.0, 0.0, 0.0, 1.0f, &stamps) == CAFFEINE_OK);
    CHECK(stamps == 10);  // 50 m at one stamp per 5 m
    // The terrain moved at once, only around the path.
    CHECK(heightAt(0.0f, 0.0f) > before + 1.5f);
    CHECK(heightAt(25.0f, 0.0f) > before + 1.5f);
    CHECK(heightAt(0.0f, 100.0f) == Approx(before).margin(0.001f));

    CaffeineTerrainStrokeResult result{};
    REQUIRE(caffeine_session_terrain_stroke_end(session, &result) == CAFFEINE_OK);
    CHECK(result.record != 0);
    CHECK(result.stamps == 11);
    CHECK(result.tiles_changed >= 1);
    CHECK(result.tiles_changed < 16);  // a part of the 16 tiles
    CHECK(result.has_region == 1);
    CHECK(result.bytes < 257u * 257u * sizeof(float));
    const float after = heightAt(25.0f, 0.0f);

    // Undo puts every tile back exactly; redo brings the stroke back.
    REQUIRE(caffeine_session_terrain_stroke_revert(session, result.record) == CAFFEINE_OK);
    CHECK(heightAt(25.0f, 0.0f) == Approx(before).margin(1e-4f));
    CHECK(heightAt(0.0f, 0.0f) == Approx(before).margin(1e-4f));
    REQUIRE(caffeine_session_terrain_stroke_reapply(session, result.record) == CAFFEINE_OK);
    CHECK(heightAt(25.0f, 0.0f) == Approx(after).margin(1e-4f));

    // Errors: no open stroke, an unknown record, a missing entity, a bad brush.
    CHECK(caffeine_session_terrain_stroke_point(session, 0, 0, 0, 1.0f, nullptr) == CAFFEINE_NOT_FOUND);
    CHECK(caffeine_session_terrain_stroke_end(session, nullptr) == CAFFEINE_NOT_FOUND);
    CHECK(caffeine_session_terrain_stroke_revert(session, 9999u) == CAFFEINE_NOT_FOUND);
    CHECK(caffeine_session_terrain_stroke_begin(session, 9999u, &brush, nullptr, 0) == CAFFEINE_NOT_FOUND);
    CHECK(caffeine_session_terrain_stroke_begin(session, entity, nullptr, nullptr, 0) == CAFFEINE_INVALID_ARGUMENT);
    CHECK(caffeine_session_terrain_stroke_begin(session, entity, &brush, nullptr, 0) == CAFFEINE_OK);

    // A stroke on a terrain that is replaced under it is dropped, not written into freed memory.
    CHECK(caffeine_session_set_terrain(session, entity, &terrain, heights.data(), nullptr) == CAFFEINE_OK);
    caffeine_session_terrain_stroke_point(session, 0.0, 0.0, 0.0, 1.0f, nullptr);
    caffeine_session_destroy(session);
}

TEST_CASE("C API terrain strokes follow the entity's transform and a stamp image", "[capi][terrain][edit]") {
    CaffeineSession* session = caffeine_session_create();
    REQUIRE(session != nullptr);
    CaffeineEntity entity = caffeine_session_create_entity(session, "Ground");
    CaffeineTerrain terrain{};
    terrain.resolution_x = 129;
    terrain.resolution_z = 129;
    terrain.size_x = 128.0f;
    terrain.size_z = 128.0f;
    terrain.max_height = 50.0f;
    std::vector<float> heights(129 * 129, 0.5f);
    REQUIRE(caffeine_session_set_terrain(session, entity, &terrain, heights.data(), nullptr) == CAFFEINE_OK);
    REQUIRE(caffeine_session_set_entity_position(session, entity, CaffeineVec3{1000.0f, 0.0f, -500.0f}) == CAFFEINE_OK);

    // The stamp: only the right half of the brush is solid.
    std::vector<float> alpha(16 * 16, 0.0f);
    for (int z = 0; z < 16; ++z) {
        for (int x = 8; x < 16; ++x) alpha[z * 16 + x] = 1.0f;
    }
    CaffeineTerrainBrush brush{};
    brush.mode = CAFFEINE_BRUSH_RAISE;
    brush.radius_m = 8.0f;
    brush.strength = 3.0f;
    brush.falloff = CAFFEINE_FALLOFF_CONSTANT;
    REQUIRE(caffeine_session_terrain_stroke_begin(session, entity, &brush, alpha.data(), 16) == CAFFEINE_OK);
    // The point is given in world space: the terrain is 1 km east and 0.5 km north of the origin.
    REQUIRE(caffeine_session_terrain_stroke_point(session, 1000.0, 0.0, -500.0, 1.0f, nullptr) == CAFFEINE_OK);
    CaffeineTerrainStrokeResult result{};
    REQUIRE(caffeine_session_terrain_stroke_end(session, &result) == CAFFEINE_OK);
    CHECK(result.record != 0);

    float right = 0.0f, left = 0.0f;
    REQUIRE(caffeine_session_terrain_height(session, entity, 64.0f + 4.0f, 64.0f, &right) == CAFFEINE_OK);
    REQUIRE(caffeine_session_terrain_height(session, entity, 64.0f - 4.0f, 64.0f, &left) == CAFFEINE_OK);
    CHECK(right > 25.0f + 1.0f);
    CHECK(left == Approx(25.0f).margin(0.01f));
    caffeine_session_destroy(session);
}

TEST_CASE("C API terrain data: a new terrain saves as .terrain and reloads, a bare one cannot convert", "[capi][terrain][legacy]") {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "caffeine-capi-terrain-format";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "Scenes", ec);
    { std::ofstream(root / "project.caffeine") << "{}\n"; }

    CaffeineSession* session = caffeine_session_create();
    REQUIRE(session != nullptr);
    CaffeineEntity entity = caffeine_session_create_entity(session, "Ground");
    CaffeineTerrain terrain{};
    terrain.resolution_x = 129;
    terrain.resolution_z = 129;
    terrain.size_x = 256.0f;
    terrain.size_z = 256.0f;
    terrain.max_height = 60.0f;
    std::vector<float> heights(129 * 129);
    for (size_t i = 0; i < heights.size(); ++i) heights[i] = 0.2f + static_cast<float>(i % 97) * 0.003f;
    REQUIRE(caffeine_session_set_terrain(session, entity, &terrain, heights.data(), nullptr) == CAFFEINE_OK);

    int legacy = 1;
    REQUIRE(caffeine_session_terrain_data_legacy(session, entity, &legacy) == CAFFEINE_OK);
    CHECK(legacy == 0);  // a terrain made here was never legacy
    char path[256] = {};
    CHECK(caffeine_session_terrain_convert_native(session, entity, path, sizeof(path)) == CAFFEINE_ERROR);  // no data file yet

    const fs::path scene = root / "Scenes" / "main.caf";
    REQUIRE(caffeine_session_save_scene(session, scene.string().c_str()) == CAFFEINE_OK);
    bool native = false, old = false;
    for (const auto& entry : fs::recursive_directory_iterator(root / "terrain", ec)) {
        native = native || entry.path().extension() == ".terrain";
        old = old || entry.path().extension() == ".cterrain";
    }
    CHECK(native);
    CHECK_FALSE(old);  // new terrains use the native container

    caffeine_session_destroy(session);
    session = caffeine_session_create();
    REQUIRE(caffeine_session_load_scene(session, scene.string().c_str()) == CAFFEINE_OK);
    CaffeineEntity loaded = 0;
    bool found = false;
    for (uint32_t i = 0; i < caffeine_session_entity_count(session); ++i) {
        CaffeineEntityInfo info{};
        REQUIRE(caffeine_session_entity_at(session, i, &info) == CAFFEINE_OK);
        if (std::string(info.name) == "Ground") {
            loaded = info.id;
            found = true;
        }
    }
    REQUIRE(found);
    float h = 0.0f;
    REQUIRE(caffeine_session_terrain_height(session, loaded, 64.0f, 64.0f, &h) == CAFFEINE_OK);
    CHECK(h > 0.0f);
    legacy = 1;
    REQUIRE(caffeine_session_terrain_data_legacy(session, loaded, &legacy) == CAFFEINE_OK);
    CHECK(legacy == 0);
    CHECK(caffeine_session_terrain_data_legacy(session, 9999u, &legacy) == CAFFEINE_NOT_FOUND);
    CHECK(caffeine_session_terrain_data_legacy(nullptr, loaded, &legacy) == CAFFEINE_INVALID_ARGUMENT);
    caffeine_session_destroy(session);
    fs::remove_all(root, ec);
}

TEST_CASE("C API terrain cursor: a ray meets the terrain and the brush outline hugs the relief", "[capi][terrain][edit]") {
    CaffeineSession* session = caffeine_session_create();
    REQUIRE(session != nullptr);
    CaffeineEntity entity = caffeine_session_create_entity(session, "Hills");
    CaffeineTerrain terrain{};
    terrain.resolution_x = 129;
    terrain.resolution_z = 129;
    terrain.size_x = 256.0f;
    terrain.size_z = 256.0f;
    terrain.max_height = 50.0f;
    std::vector<float> heights(129 * 129);
    for (int z = 0; z < 129; ++z) {
        for (int x = 0; x < 129; ++x) heights[z * 129 + x] = 0.2f + 0.5f * static_cast<float>(x) / 128.0f;  // a ramp along X
    }
    REQUIRE(caffeine_session_set_terrain(session, entity, &terrain, heights.data(), nullptr) == CAFFEINE_OK);

    // A ray straight down over the middle of the terrain lands on the surface there.
    const double origin[3] = {10.0, 200.0, 20.0};
    const double down[3] = {0.0, -1.0, 0.0};
    double hit[3] = {};
    REQUIRE(caffeine_session_terrain_raycast(session, entity, origin, down, hit) == CAFFEINE_OK);
    CHECK(hit[0] == Approx(10.0).margin(0.5));
    CHECK(hit[2] == Approx(20.0).margin(0.5));
    float expected = 0.0f;
    REQUIRE(caffeine_session_terrain_height(session, entity, 128.0f + 10.0f, 128.0f + 20.0f, &expected) == CAFFEINE_OK);
    CHECK(hit[1] == Approx(expected).margin(0.3));
    // A ray beside the terrain misses it.
    const double aside[3] = {5000.0, 200.0, 0.0};
    CHECK(caffeine_session_terrain_raycast(session, entity, aside, down, hit) == CAFFEINE_NOT_FOUND);
    CHECK(caffeine_session_terrain_raycast(session, 9999u, origin, down, hit) == CAFFEINE_NOT_FOUND);
    CHECK(caffeine_session_terrain_raycast(session, entity, nullptr, down, hit) == CAFFEINE_INVALID_ARGUMENT);

    // The ring: 48 points at the brush radius from the centre, each on the surface (the ramp rises along X).
    std::vector<float> ring(48 * 3);
    REQUIRE(caffeine_session_terrain_brush_outline(session, entity, hit, 15.0f, 48, ring.data()) == CAFFEINE_OK);
    float minY = 1e9f, maxY = -1e9f;
    for (int i = 0; i < 48; ++i) {
        const double dx = ring[i * 3] - hit[0], dz = ring[i * 3 + 2] - hit[2];
        CHECK(std::sqrt(dx * dx + dz * dz) == Approx(15.0).margin(0.01));
        float surface = 0.0f;
        REQUIRE(caffeine_session_terrain_height(session, entity, ring[i * 3] + 128.0f, ring[i * 3 + 2] + 128.0f, &surface) == CAFFEINE_OK);
        CHECK(ring[i * 3 + 1] == Approx(surface + 0.05f).margin(0.02f));  // just above the ground
        minY = std::min(minY, ring[i * 3 + 1]);
        maxY = std::max(maxY, ring[i * 3 + 1]);
    }
    CHECK(maxY - minY > 2.5f);  // it climbs the ramp (25 m over 256 m: 2.9 m across a 30 m ring), not a flat circle
    CHECK(caffeine_session_terrain_brush_outline(session, entity, hit, 15.0f, 2, ring.data()) == CAFFEINE_INVALID_ARGUMENT);
    CHECK(caffeine_session_terrain_brush_outline(session, entity, hit, 0.0f, 48, ring.data()) == CAFFEINE_INVALID_ARGUMENT);
    CHECK(caffeine_session_terrain_brush_outline(session, 9999u, hit, 5.0f, 48, ring.data()) == CAFFEINE_NOT_FOUND);
    caffeine_session_destroy(session);
}

TEST_CASE("C API brushes: the built-in library, .brush text, stamp previews and a stroke with a preset", "[capi][terrain][brush]") {
    const uint32_t count = caffeine_terrain_brush_count();
    REQUIRE(count >= 56);
    CaffeineBrushPreset first{};
    REQUIRE(caffeine_terrain_brush_at(0, &first) == CAFFEINE_OK);
    CHECK(std::string(first.name) == "Round soft");
    CHECK(first.stamp_kind == CAFFEINE_STAMP_PROCEDURAL);
    CHECK(std::string(first.stamp) == "round_soft");
    CHECK(caffeine_terrain_brush_at(count, &first) == CAFFEINE_NOT_FOUND);
    CHECK(caffeine_terrain_brush_at(0, nullptr) == CAFFEINE_INVALID_ARGUMENT);

    // Every mode has a brush.
    bool seen[16] = {};
    for (uint32_t i = 0; i < count; ++i) {
        CaffeineBrushPreset p{};
        REQUIRE(caffeine_terrain_brush_at(i, &p) == CAFFEINE_OK);
        REQUIRE(p.mode >= 0);
        REQUIRE(p.mode < 16);
        seen[p.mode] = true;
    }
    for (bool mode : seen) CHECK(mode);

    // A stamp preview.
    std::vector<float> alpha(32 * 32);
    REQUIRE(caffeine_terrain_brush_stamp("dome", 32, 0, alpha.data()) == CAFFEINE_OK);
    CHECK(alpha[16 * 32 + 16] > 0.9f);
    CHECK(alpha[0] == Approx(0.0f).margin(0.01f));
    CHECK(caffeine_terrain_brush_stamp("nope", 32, 0, alpha.data()) == CAFFEINE_NOT_FOUND);
    CHECK(caffeine_terrain_brush_stamp("dome", 2, 0, alpha.data()) == CAFFEINE_INVALID_ARGUMENT);

    // Text round trip, and the error of a bad line.
    CaffeineBrushPreset custom{};
    std::snprintf(custom.name, sizeof(custom.name), "%s", "Ridge");
    custom.stamp_kind = CAFFEINE_STAMP_PROCEDURAL;
    std::snprintf(custom.stamp, sizeof(custom.stamp), "%s", "ridged");
    custom.stamp_size = 64;
    custom.mode = CAFFEINE_BRUSH_TERRACE;
    custom.radius_m = 12.0f;
    custom.strength = 0.9f;
    custom.step_m = 3.0f;
    custom.symmetry = CAFFEINE_SYMMETRY_RADIAL;
    custom.radial_count = 5;
    custom.protect_count = 1;
    std::snprintf(custom.protect[0].mask, sizeof(custom.protect[0].mask), "%s", "slope");
    custom.protect[0].fall1 = 0.2f;
    custom.protect[0].fall0 = 0.3f;
    char text[4096];
    REQUIRE(caffeine_terrain_brush_format(&custom, text, sizeof(text)) == CAFFEINE_OK);
    CHECK(std::string(text).find("mode terrace") != std::string::npos);
    CaffeineBrushPreset back{};
    char error[128] = {};
    REQUIRE(caffeine_terrain_brush_parse(text, &back, error, sizeof(error)) == CAFFEINE_OK);
    CHECK(std::string(back.name) == "Ridge");
    CHECK(back.mode == CAFFEINE_BRUSH_TERRACE);
    CHECK(back.step_m == Approx(3.0f));
    CHECK(back.symmetry == CAFFEINE_SYMMETRY_RADIAL);
    CHECK(back.radial_count == 5);
    CHECK(back.protect_count == 1);
    CHECK(std::string(back.protect[0].mask) == "slope");
    CHECK(caffeine_terrain_brush_parse("mode melt\n", &back, error, sizeof(error)) == CAFFEINE_INVALID_ARGUMENT);
    CHECK(std::string(error).find("line 1") == 0);
    CHECK(caffeine_terrain_brush_format(&custom, text, 8) == CAFFEINE_ERROR);

    // A stroke with a built-in preset digs a crater into a terrain; undo restores it.
    CaffeineSession* session = caffeine_session_create();
    REQUIRE(session != nullptr);
    CaffeineEntity entity = caffeine_session_create_entity(session, "Ground");
    CaffeineTerrain terrain{};
    terrain.resolution_x = 129;
    terrain.resolution_z = 129;
    terrain.size_x = 256.0f;
    terrain.size_z = 256.0f;
    terrain.max_height = 80.0f;
    std::vector<float> heights(129 * 129, 0.5f);
    REQUIRE(caffeine_session_set_terrain(session, entity, &terrain, heights.data(), nullptr) == CAFFEINE_OK);
    CaffeineBrushPreset crater{};
    for (uint32_t i = 0; i < count; ++i) {
        REQUIRE(caffeine_terrain_brush_at(i, &crater) == CAFFEINE_OK);
        if (std::string(crater.name) == "Crater") break;
    }
    REQUIRE(std::string(crater.name) == "Crater");
    REQUIRE(caffeine_session_terrain_stroke_begin_preset(session, entity, &crater) == CAFFEINE_OK);
    REQUIRE(caffeine_session_terrain_stroke_point(session, 0.0, 0.0, 0.0, 1.0f, nullptr) == CAFFEINE_OK);
    CaffeineTerrainStrokeResult result{};
    REQUIRE(caffeine_session_terrain_stroke_end(session, &result) == CAFFEINE_OK);
    CHECK(result.record != 0);
    float centre = 0.0f, rim = 0.0f;
    REQUIRE(caffeine_session_terrain_height(session, entity, 128.0f, 128.0f, &centre) == CAFFEINE_OK);
    REQUIRE(caffeine_session_terrain_height(session, entity, 128.0f + 9.5f, 128.0f, &rim) == CAFFEINE_OK);
    CHECK(centre < 40.0f - 1.0f);  // a bowl
    CHECK(rim > 40.0f);            // inside a raised rim
    REQUIRE(caffeine_session_terrain_stroke_revert(session, result.record) == CAFFEINE_OK);
    REQUIRE(caffeine_session_terrain_height(session, entity, 128.0f, 128.0f, &centre) == CAFFEINE_OK);
    CHECK(centre == Approx(40.0f).margin(1e-3f));

    // A stamp that cannot be made fails the call, and the open stroke is left alone.
    CaffeineBrushPreset broken = crater;
    broken.stamp_kind = CAFFEINE_STAMP_IMAGE;
    std::snprintf(broken.stamp, sizeof(broken.stamp), "%s", "Textures/does-not-exist.png");
    CHECK(caffeine_session_terrain_stroke_begin_preset(session, entity, &broken) == CAFFEINE_ERROR);
    CHECK(caffeine_session_terrain_stroke_begin_preset(session, 9999u, &crater) == CAFFEINE_NOT_FOUND);
    CHECK(caffeine_session_terrain_stroke_begin_preset(session, entity, nullptr) == CAFFEINE_INVALID_ARGUMENT);
    caffeine_session_destroy(session);
}

TEST_CASE("C API terrain layers: a palette, painting, auto-layering, removal and undo, and a scene round trip", "[capi][terrain][paint]") {
    CaffeineSession* session = caffeine_session_create();
    REQUIRE(session != nullptr);
    CaffeineEntity entity = caffeine_session_create_entity(session, "Ground");
    CaffeineTerrain terrain{};
    terrain.resolution_x = 129;
    terrain.resolution_z = 129;
    terrain.size_x = 256.0f;
    terrain.size_z = 256.0f;
    terrain.max_height = 80.0f;
    std::vector<float> heights(129 * 129, 0.3f);
    // A ramp on the right half: steep ground for the auto-layering.
    for (int z = 0; z < 129; ++z) {
        for (int x = 64; x < 129; ++x) heights[z * 129 + x] = 0.3f + static_cast<float>(x - 64) * 2.0f / 80.0f;
    }
    REQUIRE(caffeine_session_set_terrain(session, entity, &terrain, heights.data(), nullptr) == CAFFEINE_OK);

    // Layers are off until asked for; painting says so.
    CaffeineTerrainLayers info{};
    REQUIRE(caffeine_session_terrain_layers(session, entity, &info) == CAFFEINE_OK);
    CHECK(info.enabled == 0);
    CaffeinePaintBrush brush{};
    CHECK(caffeine_session_terrain_paint_begin(session, entity, &brush) == CAFFEINE_ERROR);
    CHECK(caffeine_session_terrain_layers_enable(session, 9999u, 1) == CAFFEINE_NOT_FOUND);

    REQUIRE(caffeine_session_terrain_layers_enable(session, entity, 1) == CAFFEINE_OK);
    REQUIRE(caffeine_session_terrain_layers(session, entity, &info) == CAFFEINE_OK);
    CHECK(info.enabled == 1);
    CHECK(info.palette_count == 4);  // the terrain's four layers
    CHECK(info.map_x >= 129);

    CaffeinePaletteLayer layer{};
    REQUIRE(caffeine_session_terrain_palette_layer(session, entity, 0, &layer) == CAFFEINE_OK);
    CHECK(std::string(layer.name) == "Grass");
    CHECK(layer.texels == info.map_x * info.map_z);  // everything is layer 0 to start with
    CHECK(caffeine_session_terrain_palette_layer(session, entity, 99, &layer) == CAFFEINE_NOT_FOUND);

    // Add a fifth layer by text; fewer layers than the palette has is refused.
    size_t needed = 0;
    REQUIRE(caffeine_session_terrain_palette_text(session, entity, nullptr, 0, &needed) == CAFFEINE_OK);
    std::string text(needed, '\0');
    REQUIRE(caffeine_session_terrain_palette_text(session, entity, text.data(), text.size(), nullptr) == CAFFEINE_OK);
    text.resize(std::strlen(text.c_str()));
    char error[200] = {};
    uint32_t record = 0;
    CHECK(caffeine_session_terrain_palette_apply(session, entity, "layer Only one\n", 0, &record, error, sizeof(error)) == CAFFEINE_ERROR);
    CHECK(std::string(error).find("fewer layers") != std::string::npos);
    CHECK(caffeine_session_terrain_palette_apply(session, entity, "layer A\n  tile many\n", 0, &record, error, sizeof(error)) == CAFFEINE_ERROR);
    CHECK(std::string(error).find("line 2") != std::string::npos);
    REQUIRE(caffeine_session_terrain_palette_apply(session, entity, (text + "layer Snow\n  tile 3\n  tint 0.9 0.9 1\n").c_str(), 0, &record, error, sizeof(error)) == CAFFEINE_OK);
    CHECK(record != 0);
    REQUIRE(caffeine_session_terrain_layers(session, entity, &info) == CAFFEINE_OK);
    CHECK(info.palette_count == 5);
    const uint32_t addRecord = record;

    // An edit that continues folds into its record: one undo step for a whole slider drag.
    {
        std::string edited = text + "layer Snow\n  tile 3.5\n  tint 0.9 0.9 1\n";
        uint32_t merged = 0;
        REQUIRE(caffeine_session_terrain_palette_apply(session, entity, edited.c_str(), addRecord, &merged, error, sizeof(error)) == CAFFEINE_OK);
        CHECK(merged == addRecord);
        CaffeinePaletteLayer snow{};
        REQUIRE(caffeine_session_terrain_palette_layer(session, entity, 4, &snow) == CAFFEINE_OK);
        CHECK(snow.tile_size_m == Approx(3.5f));
        // Without a record to fold into, a new one is made.
        edited = text + "layer Snow\n  tile 4\n  tint 0.9 0.9 1\n";
        REQUIRE(caffeine_session_terrain_palette_apply(session, entity, edited.c_str(), 0, &merged, error, sizeof(error)) == CAFFEINE_OK);
        CHECK(merged != addRecord);
        // A record that is not a palette edit of this terrain is not folded into.
        edited = text + "layer Snow\n  tile 5\n  tint 0.9 0.9 1\n";
        uint32_t again = 0;
        REQUIRE(caffeine_session_terrain_palette_apply(session, entity, edited.c_str(), 987654u, &again, error, sizeof(error)) == CAFFEINE_OK);
        CHECK(again != 987654u);
        // Undoing the folded record puts back the palette as it was before the first edit of the drag.
        REQUIRE(caffeine_session_terrain_stroke_revert(session, again) == CAFFEINE_OK);
        REQUIRE(caffeine_session_terrain_stroke_revert(session, merged) == CAFFEINE_OK);
        REQUIRE(caffeine_session_terrain_palette_layer(session, entity, 4, &snow) == CAFFEINE_OK);
        CHECK(snow.tile_size_m == Approx(3.5f));
        REQUIRE(caffeine_session_terrain_stroke_reapply(session, merged) == CAFFEINE_OK);
        REQUIRE(caffeine_session_terrain_stroke_reapply(session, again) == CAFFEINE_OK);
        // Settle on the 3 m tile the rest of this case expects.
        edited = text + "layer Snow\n  tile 3\n  tint 0.9 0.9 1\n";
        REQUIRE(caffeine_session_terrain_palette_apply(session, entity, edited.c_str(), 0, &merged, error, sizeof(error)) == CAFFEINE_OK);
    }

    // Paint layer 4 with a round brush.
    CaffeineBrushPreset round{};
    for (uint32_t i = 0; i < caffeine_terrain_brush_count(); ++i) {
        REQUIRE(caffeine_terrain_brush_at(i, &round) == CAFFEINE_OK);
        if (std::string(round.name) == "Round hard") break;
    }
    brush.shape = round;
    brush.shape.radius_m = 12.0f;
    brush.shape.strength = 1.0f;
    brush.mode = CAFFEINE_PAINT_PAINT;
    brush.layer = 4;
    CHECK(caffeine_session_terrain_paint_begin(session, entity, &brush) == CAFFEINE_OK);
    CaffeineBrushPreset keep = round;
    (void)keep;
    REQUIRE(caffeine_session_terrain_stroke_point(session, -40.0, 0.0, 0.0, 1.0f, nullptr) == CAFFEINE_OK);
    REQUIRE(caffeine_session_terrain_stroke_point(session, -30.0, 0.0, 0.0, 1.0f, nullptr) == CAFFEINE_OK);
    CaffeineTerrainStrokeResult result{};
    REQUIRE(caffeine_session_terrain_stroke_end(session, &result) == CAFFEINE_OK);
    CHECK(result.record != 0);
    CHECK(result.stamps >= 2);
    CaffeineLayerSample sample{};
    REQUIRE(caffeine_session_terrain_layer_at(session, entity, -35.0, 0.0, 0.0, &sample) == CAFFEINE_OK);
    CHECK(sample.layer[0] == 4);
    CHECK(sample.weight[0] == Approx(1.0f));
    REQUIRE(caffeine_session_terrain_layer_at(session, entity, 40.0, 0.0, 0.0, &sample) == CAFFEINE_OK);
    CHECK(sample.layer[0] == 0);
    CHECK(caffeine_session_terrain_layer_at(session, entity, 5000.0, 0.0, 0.0, &sample) == CAFFEINE_NOT_FOUND);

    // Undo and redo a paint stroke by its record, the same calls as for a height stroke.
    REQUIRE(caffeine_session_terrain_stroke_revert(session, result.record) == CAFFEINE_OK);
    REQUIRE(caffeine_session_terrain_layer_at(session, entity, -35.0, 0.0, 0.0, &sample) == CAFFEINE_OK);
    CHECK(sample.layer[0] == 0);
    REQUIRE(caffeine_session_terrain_stroke_reapply(session, result.record) == CAFFEINE_OK);
    REQUIRE(caffeine_session_terrain_layer_at(session, entity, -35.0, 0.0, 0.0, &sample) == CAFFEINE_OK);
    CHECK(sample.layer[0] == 4);

    // Bad paint requests.
    CaffeinePaintBrush wrong = brush;
    wrong.layer = 77;
    CHECK(caffeine_session_terrain_paint_begin(session, entity, &wrong) == CAFFEINE_INVALID_ARGUMENT);
    wrong = brush;
    wrong.mode = 9;
    CHECK(caffeine_session_terrain_paint_begin(session, entity, &wrong) == CAFFEINE_INVALID_ARGUMENT);

    // Auto-layering: layer 1 (rock) on steep ground, layer 0 elsewhere. One undoable edit.
    uint32_t autoRecord = 0;
    CHECK(caffeine_session_terrain_autolayer(session, entity, "class 1 base 1\n  slope 0.3 0.4 1 1\n", 0, nullptr, &autoRecord, error, sizeof(error)) == CAFFEINE_OK);
    CHECK(autoRecord != 0);
    REQUIRE(caffeine_session_terrain_layer_at(session, entity, 40.0, 0.0, 0.0, &sample) == CAFFEINE_OK);
    CHECK(sample.layer[0] == 1);
    REQUIRE(caffeine_session_terrain_layer_at(session, entity, -35.0, 0.0, 0.0, &sample) == CAFFEINE_OK);
    CHECK(sample.layer[0] == 0);  // the rules repaint everything they cover, even the stroke
    CHECK(caffeine_session_terrain_autolayer(session, entity, "class 9 base 1\n", 0, nullptr, &autoRecord, error, sizeof(error)) == CAFFEINE_INVALID_ARGUMENT);
    CHECK(std::string(error).find("class 9") != std::string::npos);
    CHECK(caffeine_session_terrain_autolayer(session, entity, "slope 1 2 3 4\n", 0, nullptr, &autoRecord, error, sizeof(error)) == CAFFEINE_ERROR);
    REQUIRE(caffeine_session_terrain_stroke_revert(session, autoRecord == 0 ? 0u : autoRecord) != CAFFEINE_OK);  // a failed call left no record
    // (The earlier auto-layering's record is the one before the failed calls.)

    // Removing a layer renumbers the painting; undo puts both back.
    uint32_t removeRecord = 0;
    REQUIRE(caffeine_session_terrain_stroke_reapply(session, result.record) == CAFFEINE_OK);  // layer 4 painted again at the old spot
    REQUIRE(caffeine_session_terrain_palette_remove(session, entity, 2, &removeRecord) == CAFFEINE_OK);
    CHECK(removeRecord != 0);
    REQUIRE(caffeine_session_terrain_layers(session, entity, &info) == CAFFEINE_OK);
    CHECK(info.palette_count == 4);
    uint32_t unused = 0;
    CHECK(caffeine_session_terrain_palette_remove(session, 9999u, 0, &unused) == CAFFEINE_NOT_FOUND);
    REQUIRE(caffeine_session_terrain_layer_at(session, entity, -35.0, 0.0, 0.0, &sample) == CAFFEINE_OK);
    CHECK(sample.layer[0] == 3);  // layer 4 is layer 3 now
    uint32_t afterRemove = 0;
    REQUIRE(caffeine_session_terrain_palette_remove(session, entity, 3, &afterRemove) == CAFFEINE_OK);
    REQUIRE(caffeine_session_terrain_stroke_revert(session, afterRemove) == CAFFEINE_OK);
    REQUIRE(caffeine_session_terrain_stroke_revert(session, removeRecord) == CAFFEINE_OK);
    REQUIRE(caffeine_session_terrain_layers(session, entity, &info) == CAFFEINE_OK);
    CHECK(info.palette_count == 5);
    REQUIRE(caffeine_session_terrain_layer_at(session, entity, -35.0, 0.0, 0.0, &sample) == CAFFEINE_OK);
    CHECK(sample.layer[0] == 4);
    // The last layer cannot be removed.
    (void)addRecord;

    // A layer move renumbers too.
    uint32_t moveRecord = 0;
    REQUIRE(caffeine_session_terrain_palette_move(session, entity, 4, 0, &moveRecord) == CAFFEINE_OK);
    REQUIRE(caffeine_session_terrain_palette_layer(session, entity, 0, &layer) == CAFFEINE_OK);
    CHECK(std::string(layer.name) == "Snow");
    REQUIRE(caffeine_session_terrain_layer_at(session, entity, -35.0, 0.0, 0.0, &sample) == CAFFEINE_OK);
    CHECK(sample.layer[0] == 0);
    REQUIRE(caffeine_session_terrain_stroke_revert(session, moveRecord) == CAFFEINE_OK);
    REQUIRE(caffeine_session_terrain_palette_layer(session, entity, 0, &layer) == CAFFEINE_OK);
    CHECK(std::string(layer.name) == "Grass");

    // The scene saves the palette and the layer map with the terrain, and a new session reads them back.
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "caffeine_layers_project";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "Scenes");
    const std::string scene = (root / "Scenes" / "main.caf").string();
    REQUIRE(caffeine_session_save_scene(session, scene.c_str()) == CAFFEINE_OK);
    CHECK(std::filesystem::exists(root / "terrain"));
    bool foundPalette = false, foundMap = false;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        foundPalette |= entry.path().extension() == ".terrainpalette";
        foundMap |= entry.path().extension() == ".terrainlayers";
    }
    CHECK(foundPalette);
    CHECK(foundMap);
    caffeine_session_destroy(session);

    CaffeineSession* second = caffeine_session_create();
    REQUIRE(caffeine_session_load_scene(second, scene.c_str()) == CAFFEINE_OK);
    CaffeineEntity loaded = 0;
    bool haveGround = false;
    for (uint32_t i = 0; i < caffeine_session_entity_count(second); ++i) {
        CaffeineEntityInfo entityInfo{};
        if (caffeine_session_entity_at(second, i, &entityInfo) == CAFFEINE_OK && std::string(entityInfo.name) == "Ground") {
            loaded = entityInfo.id;
            haveGround = true;
        }
    }
    REQUIRE(haveGround);
    CaffeineTerrainLayers back{};
    REQUIRE(caffeine_session_terrain_layers(second, loaded, &back) == CAFFEINE_OK);
    CHECK(back.enabled == 1);
    CHECK(back.palette_count == 5);
    CaffeineLayerSample again{};
    REQUIRE(caffeine_session_terrain_layer_at(second, loaded, -35.0, 0.0, 0.0, &again) == CAFFEINE_OK);
    CHECK(again.layer[0] == 4);
    caffeine_session_destroy(second);
    std::filesystem::remove_all(root);
}

TEST_CASE("C API terrain vegetation: species, procedural and painted instances, edits, undo and a scene round trip", "[capi][terrain][vegetation]") {
    CaffeineSession* session = caffeine_session_create();
    REQUIRE(session != nullptr);
    CaffeineEntity entity = caffeine_session_create_entity(session, "Ground");
    CaffeineTerrain terrain{};
    terrain.resolution_x = 129;
    terrain.resolution_z = 129;
    terrain.size_x = 256.0f;
    terrain.size_z = 256.0f;
    terrain.max_height = 80.0f;
    std::vector<float> heights(129 * 129, 0.3f);
    REQUIRE(caffeine_session_set_terrain(session, entity, &terrain, heights.data(), nullptr) == CAFFEINE_OK);

    // Off until asked for; every call says so.
    CaffeineVegInfo info{};
    REQUIRE(caffeine_session_terrain_vegetation(session, entity, &info) == CAFFEINE_OK);
    CHECK(info.enabled == 0);
    CaffeineVegBrush brush{};
    CHECK(caffeine_session_terrain_veg_paint_begin(session, entity, &brush) == CAFFEINE_ERROR);
    CHECK(caffeine_session_terrain_vegetation_enable(session, 9999u, 1) == CAFFEINE_NOT_FOUND);
    REQUIRE(caffeine_session_terrain_vegetation_enable(session, entity, 1) == CAFFEINE_OK);
    REQUIRE(caffeine_session_terrain_vegetation(session, entity, &info) == CAFFEINE_OK);
    CHECK(info.enabled == 1);
    CHECK(info.species_count == 0);

    // Two species: a pine that grows by itself on flat ground and a rock that is only ever placed by hand.
    CaffeineVegSpecies pine{};
    std::snprintf(pine.name, sizeof(pine.name), "%s", "Pine");
    pine.mesh_count = 2;
    std::snprintf(pine.meshes[0], sizeof(pine.meshes[0]), "%s", "Meshes/pine_a.obj");
    std::snprintf(pine.meshes[1], sizeof(pine.meshes[1]), "%s", "Meshes/pine_b.obj");
    pine.density_per_ha = 300.0f;
    pine.radius_m = 2.0f;
    pine.priority = 1;
    pine.min_scale = 0.8f;
    pine.max_scale = 1.2f;
    pine.tilt = 0.15f;
    pine.tint_jitter = 0.05f;
    pine.tint[0] = pine.tint[1] = pine.tint[2] = 1.0f;
    pine.max_distance_m = 300.0f;
    pine.procedural = 1;
    pine.visible = 1;
    CaffeineVegSpecies rock = pine;
    std::snprintf(rock.name, sizeof(rock.name), "%s", "Rock");
    rock.mesh_count = 1;
    std::snprintf(rock.meshes[0], sizeof(rock.meshes[0]), "%s", "Meshes/rock.obj");
    rock.density_per_ha = 0.0f;
    rock.radius_m = 1.5f;
    rock.priority = 5;
    rock.procedural = 0;
    std::vector<CaffeineVegSpecies> list = {pine, rock};
    char error[200] = {};
    uint32_t record = 0;
    CHECK(caffeine_session_terrain_veg_species_apply(session, entity, list.data(), 2, 0, &record, error, sizeof(error)) == CAFFEINE_OK);
    CHECK(record != 0);
    const uint32_t addRecord = record;
    REQUIRE(caffeine_session_terrain_vegetation(session, entity, &info) == CAFFEINE_OK);
    CHECK(info.species_count == 2);
    CaffeineVegSpecies back{};
    REQUIRE(caffeine_session_terrain_veg_species(session, entity, 0, &back) == CAFFEINE_OK);
    CHECK(std::string(back.name) == "Pine");
    CHECK(back.uid != 0);
    CHECK(back.mesh_count == 2);
    CHECK(back.density_per_ha == Approx(300.0f));
    CHECK(caffeine_session_terrain_veg_species(session, entity, 2, &back) == CAFFEINE_NOT_FOUND);

    // Slot materials, the recipe hash and adaptation go through the struct and come back.
    {
        CaffeineVegSpecies rich = list[0];
        rich.slot_material_count = 2;
        std::snprintf(rich.slot_materials[0], sizeof(rich.slot_materials[0]), "%s", "Materials/bark.mat");
        std::snprintf(rich.slot_materials[1], sizeof(rich.slot_materials[1]), "%s", "Materials/needles.mat");
        std::snprintf(rich.recipe, sizeof(rich.recipe), "%s", "Nature/Pine.generator.nature");
        rich.recipe_hash = 0xABCDEF0123456789ull;
        rich.adapt_count = 2;
        std::snprintf(rich.adapt[0].mask, sizeof(rich.adapt[0].mask), "%s", "slope");
        rich.adapt[0].target = 0;
        rich.adapt[0].at0[0] = 1.0f;
        rich.adapt[0].at1[0] = 0.4f;
        std::snprintf(rich.adapt[1].mask, sizeof(rich.adapt[1].mask), "%s", "height");
        rich.adapt[1].target = 1;
        rich.adapt[1].at0[1] = 1.0f;
        rich.adapt[1].at1[1] = 0.6f;
        std::vector<CaffeineVegSpecies> richList = {rich, list[1]};
        uint32_t richRecord = 0;
        REQUIRE(caffeine_session_terrain_veg_species_apply(session, entity, richList.data(), 2, 0, &richRecord, error, sizeof(error)) == CAFFEINE_OK);
        CHECK(richRecord != 0);
        CaffeineVegSpecies readBack{};
        REQUIRE(caffeine_session_terrain_veg_species(session, entity, 0, &readBack) == CAFFEINE_OK);
        CHECK(readBack.slot_material_count == 2);
        CHECK(std::string(readBack.slot_materials[1]) == "Materials/needles.mat");
        CHECK(readBack.recipe_hash == 0xABCDEF0123456789ull);
        CHECK(std::string(readBack.recipe) == "Nature/Pine.generator.nature");
        CHECK(readBack.adapt_count == 2);
        CHECK(std::string(readBack.adapt[0].mask) == "slope");
        CHECK(readBack.adapt[0].at1[0] == Approx(0.4f));
        CHECK(readBack.adapt[1].target == 1);
        REQUIRE(caffeine_session_terrain_stroke_revert(session, richRecord) == CAFFEINE_OK);
        REQUIRE(caffeine_session_terrain_veg_species(session, entity, 0, &readBack) == CAFFEINE_OK);
        CHECK(readBack.slot_material_count == 0);
        CHECK(readBack.adapt_count == 0);
    }

    // A bad edit is refused with the reason.
    CaffeineVegSpecies bad = list[0];
    bad.min_scale = 3.0f;
    bad.max_scale = 1.0f;
    list[0] = bad;
    CHECK(caffeine_session_terrain_veg_species_apply(session, entity, list.data(), 2, 0, &record, error, sizeof(error)) == CAFFEINE_ERROR);
    CHECK(std::string(error).find("scale") != std::string::npos);

    // The procedural pines are there: about 300 per hectare, 6.5 ha.
    std::vector<CaffeineVegInstance> found(20000);
    uint32_t count = 0;
    REQUIRE(caffeine_session_terrain_veg_instances(session, entity, 0.0, 0.0, 400.0f, found.data(), static_cast<uint32_t>(found.size()), &count) == CAFFEINE_OK);
    CHECK(count > 1200);
    CHECK(count < 2800);
    bool allPines = true;
    for (uint32_t i = 0; i < std::min<uint32_t>(count, 20000); ++i) allPines &= found[i].species == 0 && found[i].manual == 0;
    CHECK(allPines);
    // A pine edit by uid keeps its instances; dragging a slider folds into one record.
    REQUIRE(caffeine_session_terrain_veg_species(session, entity, 0, &back) == CAFFEINE_OK);
    CaffeineVegSpecies edited = back;
    edited.max_distance_m = 120.0f;
    CaffeineVegSpecies rockBack{};
    REQUIRE(caffeine_session_terrain_veg_species(session, entity, 1, &rockBack) == CAFFEINE_OK);
    std::vector<CaffeineVegSpecies> both = {edited, rockBack};
    uint32_t merged = 0;
    REQUIRE(caffeine_session_terrain_veg_species_apply(session, entity, both.data(), 2, addRecord, &merged, error, sizeof(error)) == CAFFEINE_OK);
    CHECK(merged == addRecord);
    REQUIRE(caffeine_session_terrain_veg_species(session, entity, 0, &back) == CAFFEINE_OK);
    CHECK(back.max_distance_m == Approx(120.0f));
    uint32_t countAfter = 0;
    REQUIRE(caffeine_session_terrain_veg_instances(session, entity, 0.0, 0.0, 400.0f, found.data(), static_cast<uint32_t>(found.size()), &countAfter) == CAFFEINE_OK);
    CHECK(countAfter == count);

    // Paint rocks by hand with the round brush.
    CaffeineBrushPreset round{};
    for (uint32_t i = 0; i < caffeine_terrain_brush_count(); ++i) {
        REQUIRE(caffeine_terrain_brush_at(i, &round) == CAFFEINE_OK);
        if (std::string(round.name) == "Round hard") break;
    }
    brush.shape = round;
    brush.shape.radius_m = 12.0f;
    brush.shape.strength = 1.0f;
    brush.tool = CAFFEINE_VEG_PAINT;
    brush.species = 1;
    brush.only_species = -1;
    brush.per_100m2 = 6.0f;
    brush.respect_rules = 1;
    CHECK(caffeine_session_terrain_veg_paint_begin(session, entity, &brush) == CAFFEINE_OK);
    REQUIRE(caffeine_session_terrain_stroke_point(session, 0.0, 0.0, 0.0, 1.0f, nullptr) == CAFFEINE_OK);
    REQUIRE(caffeine_session_terrain_stroke_point(session, 20.0, 0.0, 0.0, 1.0f, nullptr) == CAFFEINE_OK);
    CaffeineTerrainStrokeResult result{};
    REQUIRE(caffeine_session_terrain_stroke_end(session, &result) == CAFFEINE_OK);
    CHECK(result.record != 0);
    REQUIRE(caffeine_session_terrain_vegetation(session, entity, &info) == CAFFEINE_OK);
    const uint64_t painted = info.manual_instances;
    CHECK(painted > 10);
    REQUIRE(caffeine_session_terrain_veg_species(session, entity, 1, &rockBack) == CAFFEINE_OK);
    CHECK(rockBack.manual_instances == painted);

    // Undo and redo the stroke by its record, the same calls as for a height stroke.
    REQUIRE(caffeine_session_terrain_stroke_revert(session, result.record) == CAFFEINE_OK);
    REQUIRE(caffeine_session_terrain_vegetation(session, entity, &info) == CAFFEINE_OK);
    CHECK(info.manual_instances == 0);
    REQUIRE(caffeine_session_terrain_stroke_reapply(session, result.record) == CAFFEINE_OK);
    REQUIRE(caffeine_session_terrain_vegetation(session, entity, &info) == CAFFEINE_OK);
    CHECK(info.manual_instances == painted);

    // Erase pines under the brush: an override, not a deletion; undo gives them back.
    uint32_t pinesBefore = 0;
    REQUIRE(caffeine_session_terrain_veg_instances(session, entity, 0.0, 0.0, 30.0f, found.data(), static_cast<uint32_t>(found.size()), &pinesBefore) == CAFFEINE_OK);
    CaffeineVegBrush erase = brush;
    erase.tool = CAFFEINE_VEG_ERASE;
    erase.only_species = 0;
    erase.shape.radius_m = 40.0f;
    REQUIRE(caffeine_session_terrain_veg_paint_begin(session, entity, &erase) == CAFFEINE_OK);
    REQUIRE(caffeine_session_terrain_stroke_point(session, 0.0, 0.0, 0.0, 1.0f, nullptr) == CAFFEINE_OK);
    CaffeineTerrainStrokeResult erased{};
    REQUIRE(caffeine_session_terrain_stroke_end(session, &erased) == CAFFEINE_OK);
    uint32_t pinesAfter = 0;
    REQUIRE(caffeine_session_terrain_veg_instances(session, entity, 0.0, 0.0, 30.0f, found.data(), static_cast<uint32_t>(found.size()), &pinesAfter) == CAFFEINE_OK);
    CHECK(pinesAfter < pinesBefore);
    REQUIRE(caffeine_session_terrain_vegetation(session, entity, &info) == CAFFEINE_OK);
    CHECK(info.overrides > 0);
    REQUIRE(caffeine_session_terrain_stroke_revert(session, erased.record) == CAFFEINE_OK);
    uint32_t pinesBack = 0;
    REQUIRE(caffeine_session_terrain_veg_instances(session, entity, 0.0, 0.0, 30.0f, found.data(), static_cast<uint32_t>(found.size()), &pinesBack) == CAFFEINE_OK);
    CHECK(pinesBack == pinesBefore);
    REQUIRE(caffeine_session_terrain_stroke_reapply(session, erased.record) == CAFFEINE_OK);

    // Bad requests.
    CaffeineVegBrush wrong = brush;
    wrong.species = 9;
    CHECK(caffeine_session_terrain_veg_paint_begin(session, entity, &wrong) == CAFFEINE_INVALID_ARGUMENT);
    wrong = brush;
    wrong.tool = 12;
    CHECK(caffeine_session_terrain_veg_paint_begin(session, entity, &wrong) == CAFFEINE_INVALID_ARGUMENT);

    // Bake the pines: the same trees, now manual; the procedural source of the species is off; undo restores it.
    uint32_t bakeRecord = 0;
    REQUIRE(caffeine_session_terrain_veg_bake(session, entity, 0, &bakeRecord) == CAFFEINE_OK);
    CHECK(bakeRecord != 0);
    REQUIRE(caffeine_session_terrain_vegetation(session, entity, &info) == CAFFEINE_OK);
    CHECK(info.manual_instances > painted + 1000);
    REQUIRE(caffeine_session_terrain_veg_species(session, entity, 0, &back) == CAFFEINE_OK);
    CHECK(back.procedural == 0);
    REQUIRE(caffeine_session_terrain_stroke_revert(session, bakeRecord) == CAFFEINE_OK);
    REQUIRE(caffeine_session_terrain_vegetation(session, entity, &info) == CAFFEINE_OK);
    CHECK(info.manual_instances == painted);
    REQUIRE(caffeine_session_terrain_veg_species(session, entity, 0, &back) == CAFFEINE_OK);
    CHECK(back.procedural == 1);

    // A new seed grows another forest.
    uint32_t seedRecord = 0;
    REQUIRE(caffeine_session_terrain_veg_reseed(session, entity, 777, &seedRecord) == CAFFEINE_OK);
    CHECK(seedRecord != 0);
    std::vector<CaffeineVegInstance> other(20000);
    uint32_t otherCount = 0;
    REQUIRE(caffeine_session_terrain_veg_instances(session, entity, 0.0, 0.0, 30.0f, other.data(), static_cast<uint32_t>(other.size()), &otherCount) == CAFFEINE_OK);
    CHECK(other[0].id != found[0].id);
    REQUIRE(caffeine_session_terrain_stroke_revert(session, seedRecord) == CAFFEINE_OK);

    // The scene saves the vegetation next to the terrain; a new session reads it back.
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "caffeine_vegetation_project";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "Scenes");
    const std::string scene = (root / "Scenes" / "main.caf").string();
    REQUIRE(caffeine_session_save_scene(session, scene.c_str()) == CAFFEINE_OK);
    bool foundFile = false;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) foundFile |= entry.path().extension() == ".terrainveg";
    CHECK(foundFile);
    REQUIRE(caffeine_session_terrain_vegetation(session, entity, &info) == CAFFEINE_OK);
    const uint64_t manualNow = info.manual_instances;
    caffeine_session_destroy(session);

    CaffeineSession* second = caffeine_session_create();
    REQUIRE(caffeine_session_load_scene(second, scene.c_str()) == CAFFEINE_OK);
    CaffeineEntity loaded = 0;
    bool haveGround = false;
    for (uint32_t i = 0; i < caffeine_session_entity_count(second); ++i) {
        CaffeineEntityInfo entityInfo{};
        if (caffeine_session_entity_at(second, i, &entityInfo) == CAFFEINE_OK && std::string(entityInfo.name) == "Ground") {
            loaded = entityInfo.id;
            haveGround = true;
        }
    }
    REQUIRE(haveGround);
    CaffeineVegInfo loadedInfo{};
    REQUIRE(caffeine_session_terrain_vegetation(second, loaded, &loadedInfo) == CAFFEINE_OK);
    CHECK(loadedInfo.enabled == 1);
    CHECK(loadedInfo.species_count == 2);
    CHECK(loadedInfo.manual_instances == manualNow);
    uint32_t loadedPines = 0;
    REQUIRE(caffeine_session_terrain_veg_instances(second, loaded, 0.0, 0.0, 30.0f, found.data(), static_cast<uint32_t>(found.size()), &loadedPines) == CAFFEINE_OK);
    CHECK(loadedPines == pinesAfter);  // the erase override came back too
    caffeine_session_destroy(second);
    std::filesystem::remove_all(root);
}

TEST_CASE("C API builds a World Composer composition without a session", "[capi][world]") {
    CHECK(caffeine_capi_version() >= 32);
    constexpr uint32_t N = 65;
    std::vector<float> heights(N * N);
    std::vector<uint8_t> wetness(N * N);
    for (uint32_t z = 0; z < N; ++z) {
        for (uint32_t x = 0; x < N; ++x) {
            const float fx = static_cast<float>(x) - 20.0f, fz = static_cast<float>(z) - 40.0f;
            heights[z * N + x] = std::min(0.1f + 0.0002f * (fx * fx + fz * fz), 1.0f);
            wetness[z * N + x] = static_cast<uint8_t>(255.0f * (1.0f - heights[z * N + x]));
        }
    }
    const char* graph =
        "seed 9\n"
        "terrain Terrain/Hill.generator.terrain\n"
        "water Water/Lake.generator.water fill basins level 0.3\n"
        "class rock ref Nature/Rock.generator.nature priority 30 cell 6 radius 1.5 variants 3\n"
        "class tree ref Nature/Tree.generator.nature priority 20 cell 4 radius 1.2\n"
        "  wetness 0.1 0.2 1 1\n"
        "  adapt size wetness\n";
    CaffeineWorldMask mask{"wetness", N, N, wetness.data()};
    CaffeineWorldRecipe recipes[] = {
        {"Terrain/Hill.generator.terrain", CAFFEINE_RECIPE_TERRAIN, 2, 11},
        {"Water/Lake.generator.water", CAFFEINE_RECIPE_WATER, 2, 12},
        {"Nature/Rock.generator.nature", CAFFEINE_RECIPE_NATURE, 2, 13},
        {"Nature/Tree.generator.nature", CAFFEINE_RECIPE_NATURE, 2, 14},
    };
    CaffeineWorldBuildDesc desc{};
    desc.graph_text = graph;
    desc.generator_version = 2;
    desc.recipes = recipes;
    desc.recipe_count = 4;
    desc.check_refs = 1;
    desc.heights = heights.data();
    desc.resolution_x = N;
    desc.resolution_z = N;
    desc.size_x = 128.0f;
    desc.size_z = 128.0f;
    desc.max_height = 20.0f;
    desc.masks = &mask;
    desc.mask_count = 1;

    CaffeineWorld* world = nullptr;
    REQUIRE(caffeine_world_build(&desc, &world) == CAFFEINE_OK);
    REQUIRE(world);
    CaffeineWorldInfo info{};
    caffeine_world_info(world, &info);
    char error[256] = {};
    if (info.error_count) caffeine_world_error(world, 0, error, sizeof(error));
    INFO(error);
    REQUIRE(info.ok == 1);
    CHECK(info.class_count == 2);
    CHECK(info.instance_count > 0);
    CHECK(info.total == info.instance_count);
    CHECK(info.water_body_count == 1);
    CHECK(info.resolution_x == N);

    CaffeineWorldClass first{}, second{};
    REQUIRE(caffeine_world_class(world, 0, &first) == CAFFEINE_OK);
    REQUIRE(caffeine_world_class(world, 1, &second) == CAFFEINE_OK);
    CHECK(std::string(first.id) == "rock");
    CHECK(second.first_instance == first.instance_count);
    CHECK(first.instance_count + second.instance_count == info.instance_count);
    CHECK(caffeine_world_class(world, 2, &first) == CAFFEINE_NOT_FOUND);

    std::vector<CaffeineWorldInstance> instances(info.instance_count);
    CHECK(caffeine_world_instances(world, 0, instances.data(), info.instance_count) == info.instance_count);
    for (const CaffeineWorldInstance& i : instances) {
        CHECK(i.position[0] >= 0.0f);
        CHECK(i.position[0] < 128.0f);
        CHECK(i.scale > 0.0f);
    }
    CaffeineWorldWaterBody body{};
    std::vector<uint8_t> coverage(N * N), distance(N * N);
    REQUIRE(caffeine_world_water_body(world, 0, &body, coverage.data(), nullptr) == CAFFEINE_OK);
    CHECK(body.lake == 1);
    CHECK(body.cells > 0);
    CHECK(caffeine_world_water_distance(world, distance.data()) == CAFFEINE_OK);

    // The same desc gives the same world.
    CaffeineWorld* again = nullptr;
    REQUIRE(caffeine_world_build(&desc, &again) == CAFFEINE_OK);
    CaffeineWorldInfo againInfo{};
    caffeine_world_info(again, &againInfo);
    CHECK(againInfo.instance_hash == info.instance_hash);
    CHECK(againInfo.cache_key == info.cache_key);
    caffeine_world_free(again);
    caffeine_world_free(world);

    // A reference of the wrong kind builds nothing and says why.
    recipes[3].kind = CAFFEINE_RECIPE_MATERIAL;
    REQUIRE(caffeine_world_build(&desc, &world) == CAFFEINE_OK);
    caffeine_world_info(world, &info);
    CHECK(info.ok == 0);
    REQUIRE(info.error_count == 1);
    REQUIRE(caffeine_world_error(world, 0, error, sizeof(error)) == CAFFEINE_OK);
    CHECK(std::string(error).find("Tree.generator.nature") != std::string::npos);
    caffeine_world_free(world);

    // The bake: written, read back as a world, and served by the cache on the second build.
    recipes[3].kind = CAFFEINE_RECIPE_NATURE;
    const std::string dir = (std::filesystem::temp_directory_path() / "caffeine-capi-world").string();
    std::filesystem::remove_all(dir);
    REQUIRE(caffeine_world_build(&desc, &world) == CAFFEINE_OK);
    caffeine_world_info(world, &info);
    REQUIRE(info.ok == 1);
    const std::string bake = dir + "/valley.cinst";
    CHECK(caffeine_world_write_cinst(world, bake.c_str(), 32.0f) == CAFFEINE_OK);
    CaffeineWorld* read = nullptr;
    REQUIRE(caffeine_world_read_cinst(bake.c_str(), &read, error, sizeof(error)) == CAFFEINE_OK);
    CaffeineWorldInfo readInfo{};
    caffeine_world_info(read, &readInfo);
    CHECK(readInfo.ok == 1);
    CHECK(readInfo.instance_count == info.instance_count);
    CHECK(readInfo.cache_key == info.cache_key);
    caffeine_world_free(read);
    CHECK(caffeine_world_read_cinst((dir + "/missing.cinst").c_str(), &read, error, sizeof(error)) ==
          CAFFEINE_INVALID_ARGUMENT);

    uint64_t key = 0;
    REQUIRE(caffeine_world_cache_key(&desc, &key) == CAFFEINE_OK);
    CHECK(key == info.cache_key);
    int32_t hit = -1;
    CaffeineWorld* cached = nullptr;
    REQUIRE(caffeine_world_build_cached(&desc, dir.c_str(), &cached, &hit) == CAFFEINE_OK);
    CHECK(hit == 0);
    caffeine_world_free(cached);
    REQUIRE(caffeine_world_build_cached(&desc, dir.c_str(), &cached, &hit) == CAFFEINE_OK);
    CHECK(hit == 1);
    CaffeineWorldInfo cachedInfo{};
    caffeine_world_info(cached, &cachedInfo);
    CHECK(cachedInfo.instance_hash == info.instance_hash);
    caffeine_world_free(cached);
    caffeine_world_free(world);
    std::filesystem::remove_all(dir);

    CHECK(caffeine_world_build(nullptr, &world) == CAFFEINE_INVALID_ARGUMENT);
    CHECK(caffeine_world_check(graph, error, sizeof(error)) == CAFFEINE_OK);
    CHECK(caffeine_world_check("class oak\n", error, sizeof(error)) == CAFFEINE_INVALID_ARGUMENT);
    CHECK(std::string(error).find("no `terrain`") != std::string::npos);
}
