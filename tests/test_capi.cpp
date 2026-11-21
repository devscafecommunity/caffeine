#include "catch.hpp"

#include "../src/caffeine/math/Quat.hpp"
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
