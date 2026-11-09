#include "catch.hpp"

#include "assets/MaterialFile.hpp"
#include "caffeine/photon/Photon.hpp"
#include "caffeine/photon/PhotonBake.hpp"
#include "render/PhotonCascades.hpp"
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

// ============================================================================
// Photon quality tiers (P1a: one enum shared by component, UI and renderer)
// ============================================================================

TEST_CASE("PhotonQuality - Legacy numeric values are preserved", "[photon][quality]") {
    // Scenes serialise the component as raw POD, so 1/2/3 must keep meaning
    // Low/Balanced/High and Eco is appended at 4.
    REQUIRE(static_cast<u8>(ECS::PhotonQuality::Off) == 0);
    REQUIRE(static_cast<u8>(ECS::PhotonQuality::Low) == 1);
    REQUIRE(static_cast<u8>(ECS::PhotonQuality::Balanced) == 2);
    REQUIRE(static_cast<u8>(ECS::PhotonQuality::High) == 3);
    REQUIRE(static_cast<u8>(ECS::PhotonQuality::Eco) == 4);
}

TEST_CASE("PhotonQuality - UI order is Off, Eco, Low, Balanced, High", "[photon][quality]") {
    REQUIRE(ECS::kPhotonQualityCount == 5);
    REQUIRE(ECS::kPhotonQualityOrder[0] == ECS::PhotonQuality::Off);
    REQUIRE(ECS::kPhotonQualityOrder[1] == ECS::PhotonQuality::Eco);
    REQUIRE(ECS::kPhotonQualityOrder[2] == ECS::PhotonQuality::Low);
    REQUIRE(ECS::kPhotonQualityOrder[3] == ECS::PhotonQuality::Balanced);
    REQUIRE(ECS::kPhotonQualityOrder[4] == ECS::PhotonQuality::High);
}

TEST_CASE("PhotonQuality - Fallback budget grows monotonically", "[photon][quality]") {
    const auto eco = ECS::photonFallbackBudget(ECS::PhotonQuality::Eco);
    const auto low = ECS::photonFallbackBudget(ECS::PhotonQuality::Low);
    const auto balanced = ECS::photonFallbackBudget(ECS::PhotonQuality::Balanced);
    const auto high = ECS::photonFallbackBudget(ECS::PhotonQuality::High);
    const auto off = ECS::photonFallbackBudget(ECS::PhotonQuality::Off);

    REQUIRE(off.rays == 0);
    REQUIRE(off.steps == 0);
    REQUIRE(eco.rays < low.rays);
    REQUIRE(low.rays < balanced.rays);
    REQUIRE(balanced.rays < high.rays);
    REQUIRE(eco.steps <= low.steps);
    REQUIRE(low.steps <= balanced.steps);
    REQUIRE(balanced.steps < high.steps);
}

TEST_CASE("PhotonQuality - Every tier has a name", "[photon][quality]") {
    REQUIRE(std::string(ECS::photonQualityName(ECS::PhotonQuality::Off)) == "Off");
    REQUIRE(std::string(ECS::photonQualityName(ECS::PhotonQuality::Eco)) == "Eco");
    REQUIRE(std::string(ECS::photonQualityName(ECS::PhotonQuality::Low)) == "Low");
    REQUIRE(std::string(ECS::photonQualityName(ECS::PhotonQuality::Balanced)) == "Balanced");
    REQUIRE(std::string(ECS::photonQualityName(ECS::PhotonQuality::High)) == "High");
}

TEST_CASE("PhotonGIComponent - Default tier is Balanced", "[photon][quality]") {
    ECS::PhotonGIComponent gi;
    REQUIRE(gi.enabled == false);
    REQUIRE(gi.quality == ECS::PhotonQuality::Balanced);
}

// ============================================================================
// P2 — PhotonPath and cascade-0 compute resources
// ============================================================================

TEST_CASE("PhotonPath - Values are stable for POD serialisation", "[photon][cascades]") {
    REQUIRE(static_cast<u8>(ECS::PhotonPath::ScreenFallback) == 0);
    REQUIRE(static_cast<u8>(ECS::PhotonPath::ComputeC0) == 1);
    REQUIRE(std::string(ECS::photonPathName(ECS::PhotonPath::ScreenFallback)) ==
            "Screen fallback");
    REQUIRE(std::string(ECS::photonPathName(ECS::PhotonPath::ComputeC0)) ==
            "Compute C0 (experimental)");
}

TEST_CASE("PhotonGIComponent - P2/P3 fields have safe defaults", "[photon][cascades]") {
    ECS::PhotonGIComponent gi;
    REQUIRE(gi.path == ECS::PhotonPath::ScreenFallback);
    REQUIRE(gi.cascades == 5);      // Balanced preset
    REQUIRE(gi.s0 == 4);            // Balanced preset
    REQUIRE(gi.computeSteps == 16); // Balanced preset
}

TEST_CASE("PhotonCascades - Default state is unavailable", "[photon][cascades]") {
    Render::PhotonCascades cascades;
    REQUIRE(cascades.ready() == false);
    REQUIRE(cascades.shAtlas() == nullptr);
    REQUIRE(cascades.gridWidth() == 0);
    REQUIRE(cascades.gridHeight() == 0);
    REQUIRE(cascades.probeSpacing() == 0);
    REQUIRE(cascades.cascadeCount() == 0);
    REQUIRE(cascades.init(nullptr) == false);
    REQUIRE(cascades.execute(nullptr, nullptr, nullptr, nullptr, nullptr, Mat4::identity(),
                             Mat4::identity(), 320, 180, ECS::PhotonGIComponent{}, 0) == false);
    cascades.shutdown(nullptr);
}
