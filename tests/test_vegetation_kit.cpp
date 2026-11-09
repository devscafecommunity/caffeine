#include "catch.hpp"

#include "assets/MaterialCache.hpp"
#include "assets/MaterialFile.hpp"
#include "assets/MeshCache.hpp"
#include "ecs/CameraComponents.hpp"
#include "ecs/Components3D.hpp"
#include "ecs/MeshComponents.hpp"
#include "ecs/World.hpp"
#include "scene/EditorSceneSerializer.hpp"

#include <cmath>
#include <filesystem>
#include <string>

using namespace Caffeine;
using namespace Caffeine::Assets;

namespace {

const std::filesystem::path kRepoRoot =
    std::filesystem::path(__FILE__).parent_path().parent_path();
const std::filesystem::path kKitRoot = kRepoRoot / "assets" / "vegetation-kit";

struct ModelExpectation {
    const char* model;
    u8 alphaMode;
    int albedoChannels;
};

const ModelExpectation kModels[] = {
    {"models/fir_sapling/fir_sapling.gltf", 0, 0},
    {"models/fir_sapling/fir_sapling_branches.gltf", 0, 0},
    {"models/fir_sapling/fir_sapling_twigs.gltf", 0, 0},
    {"models/tree_stump_01/tree_stump_01.gltf", 0, 0},
    {"models/shrub_03/shrub_03.gltf", 1, 4},
    {"models/shrub_sorrel_01/shrub_sorrel_01.gltf", 1, 4},
    {"models/fern_02/fern_02.gltf", 1, 4},
    {"models/grass_medium_02/grass_medium_02.gltf", 2, 4},
    {"models/celandine_01/celandine_01.gltf", 2, 4},
    {"models/nettle_plant/nettle_plant.gltf", 1, 4},
    {"models/moss_01/moss_01.gltf", 1, 4},
};

}  // namespace

TEST_CASE("Vegetation kit - every model imports with its alpha mode",
          "[vegetation][kit]") {
    REQUIRE(std::filesystem::exists(kKitRoot / "manifest.json"));
    REQUIRE(std::filesystem::exists(kKitRoot / "LICENSES/CC0-1.0.txt"));

    for (const ModelExpectation& expectation : kModels) {
        INFO(expectation.model);
        const std::string path = (kKitRoot / expectation.model).string();
        Mesh3D* mesh = MeshCache::getInstance().getMesh(path, kRepoRoot.string());
        INFO(MeshCache::getInstance().getLastError());
        REQUIRE(mesh != nullptr);
        REQUIRE(!mesh->vertices.empty());
        REQUIRE(!mesh->indices.empty());
        REQUIRE(!mesh->materials.empty());
        REQUIRE(mesh->materials[0].alphaMode == expectation.alphaMode);
        REQUIRE(mesh->materials[0].alphaCutoff >= 0.0f);
        REQUIRE(mesh->materials[0].alphaCutoff <= 1.0f);
        if (expectation.albedoChannels > 0) {
            REQUIRE(mesh->materials[0].albedoChannels == expectation.albedoChannels);
        }
    }
}

TEST_CASE("Vegetation kit - every .mat resolves its PBR maps", "[vegetation][kit]") {
    usize checked = 0;
    for (const auto& entry : std::filesystem::directory_iterator(kKitRoot / "materials")) {
        if (!entry.is_regular_file() || entry.path().extension() != ".mat") continue;

        INFO(entry.path().string());
        MaterialSurface surface;
        REQUIRE(loadMaterialFile(entry.path(), surface));
        REQUIRE(surface.valid);
        const std::string name = entry.path().filename().string();
        // The demo ground is an untextured albedo colour with PBR detail maps.
        if (name != "ground.mat") {
            REQUIRE(!surface.albedoMap.empty());
            REQUIRE(std::filesystem::exists(kRepoRoot / surface.albedoMap));
        }
        REQUIRE(!surface.normalMap.empty());
        REQUIRE(!surface.ormMap.empty());
        REQUIRE(std::filesystem::exists(kRepoRoot / surface.normalMap));
        REQUIRE(std::filesystem::exists(kRepoRoot / surface.ormMap));
        REQUIRE(surface.alphaCutoff >= 0.0f);
        REQUIRE(surface.alphaCutoff <= 1.0f);

        if (name.find("_blend") != std::string::npos) {
            REQUIRE(surface.alphaMode == MaterialAlphaMode::Blend);
        } else if (name.find("_cutout") != std::string::npos) {
            REQUIRE(surface.alphaMode == MaterialAlphaMode::Cutout);
        }
        ++checked;
    }
    REQUIRE(checked >= 10);
}

TEST_CASE("Vegetation kit - demo scene deserializes with camera and meshes",
          "[vegetation][kit][scene]") {
    ECS::World world;
    Editor::SceneSerializer serializer(world);
    const std::filesystem::path scenePath = kKitRoot / "scenes" / "vegetation-kit.caf";
    REQUIRE(std::filesystem::exists(scenePath));
    REQUIRE(serializer.deserialize(scenePath.string()));

    int cameras = 0;
    ECS::ComponentQuery cameraQuery;
    cameraQuery.with<ECS::Camera3DComponent>();
    world.forEach<ECS::Camera3DComponent>(cameraQuery,
                                          [&](ECS::Entity, ECS::Camera3DComponent&) { ++cameras; });
    REQUIRE(cameras >= 1);

    int meshes = 0;
    int kitMeshes = 0;
    int withMaterials = 0;
    ECS::ComponentQuery meshQuery;
    meshQuery.with<ECS::MeshFilterComponent>();
    world.forEach<ECS::MeshFilterComponent>(
        meshQuery, [&](ECS::Entity, ECS::MeshFilterComponent& filter) {
            ++meshes;
            if (filter.customMeshPath.find("vegetation-kit") != std::string::npos) ++kitMeshes;
            if (!filter.customMaterialPath.empty()) ++withMaterials;
        });
    REQUIRE(meshes >= 20);
    REQUIRE(kitMeshes >= 20);
    REQUIRE(withMaterials >= 20);
}

TEST_CASE("Vegetation kit - demo camera transform", "[vegetation][kit][camera]") {
    ECS::World world;
    Editor::SceneSerializer serializer(world);
    REQUIRE(serializer.deserialize((kKitRoot / "scenes" / "vegetation-kit.caf").string()));
    ECS::ComponentQuery q;
    q.with<ECS::Camera3DComponent>();
    q.with<ECS::CameraActiveComponent>();
    q.with<ECS::Position3D>();
    q.with<ECS::Rotation3D>();
    world.forEach<ECS::Camera3DComponent, ECS::CameraActiveComponent, ECS::Position3D,
                  ECS::Rotation3D>(
        q, [&](ECS::Entity, ECS::Camera3DComponent& cam, ECS::CameraActiveComponent& active,
               ECS::Position3D& pos, ECS::Rotation3D& rot) {
            REQUIRE(cam.fov > 1.0f);
            REQUIRE(pos.position.y > 0.0f);
            REQUIRE(std::abs(rot.quaternion.w) > 0.9f);
            REQUIRE_FALSE(active.is2D);
        });
}

TEST_CASE("Vegetation kit - material and texture paths resolve from project root",
          "[vegetation][kit][paths]") {
    const std::string texture = "assets/vegetation-kit/models/fern_02/textures/fern_02_albedo_1k.png";
    const std::string resolved = MeshCache::resolveTexturePath(texture, kRepoRoot.string());
    INFO(resolved);
    REQUIRE(!resolved.empty());
    REQUIRE(std::filesystem::exists(resolved));

    MaterialSurface ground = MaterialCache::instance().resolve(
        "assets/vegetation-kit/materials/ground.mat", kRepoRoot.string());
    REQUIRE(ground.valid);
    REQUIRE(ground.albedo.y == Approx(0.26f));
    REQUIRE(ground.normalMap.find("moss_01") != std::string::npos);
}
