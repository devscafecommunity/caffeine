#include "../Catch2/catch.hpp"

#include "../../src/editor/PhotonPanel.hpp"
#include "../../src/editor/EditorContext.hpp"
#include "../../src/ecs/Components3D.hpp"
#include "../../src/ecs/MeshComponents.hpp"
#include "../../src/ecs/World.hpp"

#include <filesystem>

using namespace Caffeine;

namespace Caffeine::RHI {
class RenderDevice;
}

extern bool g_gpuAvailable;
extern Caffeine::RHI::RenderDevice* g_testRenderDevice;

TEST_CASE("Photon panel - queued selected bake writes material", "[photon][panel]") {
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

    Editor::EditorContext ctx;
    ctx.projectRootPath = "/tmp/opencode/photon_panel_bake";
    ctx.selectedEntity = cube;
    std::filesystem::remove_all(ctx.projectRootPath);

    Editor::PhotonPanel panel;
    panel.setRenderDevice(g_gpuAvailable ? g_testRenderDevice : nullptr);
    panel.queueBakeSelected();
    panel.processPendingBake(world, ctx);

    const auto* filter = world.get<ECS::MeshFilterComponent>(cube);
    REQUIRE(filter != nullptr);
    REQUIRE(!filter->customMaterialPath.empty());
    const std::filesystem::path materialPath =
        ctx.projectRootPath / filter->customMaterialPath;
    REQUIRE(std::filesystem::exists(materialPath));
    REQUIRE(std::filesystem::exists(ctx.projectRootPath / "assets/processed/ao/ao_1.png"));
}
