#include "../Catch2/catch.hpp"

#include "caffeine/photon/PhotonBake.hpp"
#include "ecs/Components3D.hpp"
#include "ecs/MeshComponents.hpp"
#include "ecs/World.hpp"

#include <filesystem>

using namespace Caffeine;

namespace Caffeine::RHI {
class RenderDevice;
}

extern bool g_gpuAvailable;
extern Caffeine::RHI::RenderDevice* g_testRenderDevice;

TEST_CASE("Photon bake - GPU path produces AO", "[photon][gpu]") {
    if (!g_gpuAvailable || !g_testRenderDevice) {
        SUCCEED("GPU unavailable; CPU test covers the baker");
        return;
    }

    ECS::World world;
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

    Photon::BakeSettings settings;
    settings.resolution = 64;
    settings.rays = 8;
    settings.maxDistance = 4.0f;
    settings.useGpu = true;

    const std::string root = "/tmp/opencode/photon_bake_gpu";
    std::filesystem::remove_all(root);
    const Photon::BakeResult result =
        Photon::bakeAmbientOcclusion(world, cube, settings, root, g_testRenderDevice);

    INFO(result.error);
    REQUIRE(result.ok);
    REQUIRE(result.usedGpu);
    REQUIRE(result.coveredTexels > 0);
    REQUIRE(std::filesystem::exists(std::filesystem::path(root) / result.ormPath));
    REQUIRE(std::filesystem::exists(std::filesystem::path(root) / result.materialPath));
}
