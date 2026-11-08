#include "catch.hpp"

#include "assets/MaterialFile.hpp"
#include "caffeine/photon/PhotonBake.hpp"
#include "ecs/Components3D.hpp"
#include "ecs/MeshComponents.hpp"
#include "ecs/World.hpp"
#include "scene/HierarchySystem.hpp"

#include <filesystem>
#include <fstream>
#include <string>

using namespace Caffeine;

namespace {

void populateBakedScene(ECS::World& world) {

    ECS::Entity floor = world.create("Floor");
    world.add<ECS::Position3D>(floor, Vec3(0.0f, -1.0f, 0.0f));
    world.add<ECS::Scale3D>(floor, Vec3(12.0f, 1.0f, 12.0f));
    world.add<ECS::MeshFilterComponent>(floor);
    world.get<ECS::MeshFilterComponent>(floor)->primitive = ECS::MeshPrimitive::Plane;
    world.add<ECS::MeshRendererComponent>(floor);

    ECS::Entity cube = world.create("Cube");
    world.add<ECS::Position3D>(cube, Vec3(0.0f, 0.0f, 0.0f));
    world.add<ECS::Scale3D>(cube, Vec3(1.5f, 1.5f, 1.5f));
    world.add<ECS::MeshFilterComponent>(cube);
    world.get<ECS::MeshFilterComponent>(cube)->primitive = ECS::MeshPrimitive::Cube;
    world.add<ECS::MeshRendererComponent>(cube);
}

}  // namespace

TEST_CASE("Photon bake - CPU AO writes ORM material", "[photon][bake]") {
    ECS::World world;
    populateBakedScene(world);
    ECS::Entity cube = ECS::Entity::INVALID;
    ECS::ComponentQuery cubeQuery;
    cubeQuery.with<ECS::MeshFilterComponent>();
    world.forEach<ECS::MeshFilterComponent>(cubeQuery, [&](ECS::Entity entity, ECS::MeshFilterComponent& filter) {
        if (filter.primitive == ECS::MeshPrimitive::Cube) cube = entity;
    });
    REQUIRE(cube.isValid());

    Photon::BakeSettings settings;
    settings.resolution = 64;
    settings.rays = 8;
    settings.maxDistance = 4.0f;
    settings.useGpu = false;

    const std::string root = "/tmp/opencode/photon_bake_cpu";
    std::filesystem::remove_all(root);
    const Photon::BakeResult result =
        Photon::bakeAmbientOcclusion(world, cube, settings, root, nullptr);

    INFO(result.error);
    REQUIRE(result.ok);
    REQUIRE_FALSE(result.usedGpu);
    REQUIRE(result.coveredTexels > 0);
    REQUIRE(!result.ormPath.empty());
    REQUIRE(!result.materialPath.empty());

    const std::filesystem::path ormPath = std::filesystem::path(root) / result.ormPath;
    const std::filesystem::path materialPath = std::filesystem::path(root) / result.materialPath;
    REQUIRE(std::filesystem::exists(ormPath));
    REQUIRE(std::filesystem::file_size(ormPath) > 200);
    REQUIRE(std::filesystem::exists(materialPath));

    std::ifstream material(materialPath);
    REQUIRE(material.is_open());
    const std::string materialText((std::istreambuf_iterator<char>(material)),
                                   std::istreambuf_iterator<char>());
    REQUIRE(materialText.find("orm_map") != std::string::npos);
    REQUIRE(materialText.find(result.ormPath) != std::string::npos);
    REQUIRE(materialText.find("ao_strength 1") != std::string::npos);

    const auto* filter = world.get<ECS::MeshFilterComponent>(cube);
    REQUIRE(filter != nullptr);
    REQUIRE(filter->customMaterialPath == result.materialPath);
}


TEST_CASE("Photon bake - missing mesh reports a clear error", "[photon][bake]") {
    ECS::World world;
    ECS::Entity empty = world.create("Empty");
    Photon::BakeSettings settings;
    settings.resolution = 32;
    settings.rays = 4;
    settings.useGpu = false;

    const Photon::BakeResult result =
        Photon::bakeAmbientOcclusion(world, empty, settings, "/tmp/opencode/photon_bake_missing", nullptr);
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.find("no mesh") != std::string::npos);
}

TEST_CASE("Photon bake - existing material keeps roughness and metallic", "[photon][bake]") {
    ECS::World world;
    populateBakedScene(world);
    ECS::Entity cube = ECS::Entity::INVALID;
    ECS::ComponentQuery query;
    query.with<ECS::MeshFilterComponent>();
    world.forEach<ECS::MeshFilterComponent>(query, [&](ECS::Entity entity, ECS::MeshFilterComponent& filter) {
        if (filter.primitive == ECS::MeshPrimitive::Cube) cube = entity;
    });
    REQUIRE(cube.isValid());

    const std::string root = "/tmp/opencode/photon_bake_material";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(std::filesystem::path(root) / "assets/processed");
    Assets::MaterialSurface surface;
    surface.name = "Existing";
    surface.valid = true;
    surface.roughness = 0.73f;
    surface.metallic = 0.21f;
    REQUIRE(Assets::saveMaterialFile(
        std::filesystem::path(root) / "assets/processed/existing.mat", surface));

    auto* filter = world.get<ECS::MeshFilterComponent>(cube);
    REQUIRE(filter != nullptr);
    filter->customMaterialPath = "assets/processed/existing.mat";

    Photon::BakeSettings settings;
    settings.resolution = 64;
    settings.rays = 8;
    settings.useGpu = false;
    const Photon::BakeResult result =
        Photon::bakeAmbientOcclusion(world, cube, settings, root, nullptr);
    INFO(result.error);
    REQUIRE(result.ok);

    Assets::MaterialSurface loaded;
    REQUIRE(Assets::loadMaterialFile(
        std::filesystem::path(root) / "assets/processed/existing.mat", loaded));
    REQUIRE(loaded.roughness == Approx(0.73f));
    REQUIRE(loaded.metallic == Approx(0.21f));
    REQUIRE(!loaded.ormMap.empty());
    REQUIRE(loaded.aoStrength == Approx(1.0f));
}
