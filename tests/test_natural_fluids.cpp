#include "../Catch2/catch.hpp"

#include "caffeine/environment/NaturalFluids.hpp"

#include "ecs/Components3D.hpp"
#include "ecs/MeshComponents.hpp"
#include "ecs/MeshGeometry.hpp"
#include "ecs/World.hpp"
#include "editor/SceneSerializer.hpp"
#include "fluid/FluidWorld.hpp"
#include "scene/NaturalFluidSystem.hpp"

#include <cmath>
#include <filesystem>

using namespace Caffeine;
using namespace Caffeine::ECS;
using namespace Caffeine::Fluids;

namespace {

NaturalFluidComponent realisticWater(u32 resolution, f32 size) {
    NaturalFluidComponent component;
    component.mode = static_cast<u8>(NaturalFluidMode::RealisticWater);
    component.sizeX = size;
    component.sizeZ = size;
    component.surfaceResolution = resolution;
    component.shallowPhysics = 0;
    component.windSpeed = 12.0f;
    component.previewInEditor = 1;
    return component;
}

Entity addFluidEntity(World& world, const NaturalFluidComponent& component) {
    Entity entity = world.create("Fluid Test");
    world.add<Position3D>(entity);
    world.add<MeshFilterComponent>(entity).primitive = MeshPrimitive::Custom;
    world.add<MeshRendererComponent>(entity);
    world.add<NaturalFluidComponent>(entity, component);
    return entity;
}

}  // namespace

TEST_CASE("Natural fluids surface system generates a live smooth mesh", "[naturalfluids][scene]") {
    Scene::resetNaturalFluidRuntime();
    World world;
    Entity entity = addFluidEntity(world, realisticWater(24, 32.0f));

    Scene::tickNaturalFluids(world, 1.0f / 60.0f, true);

    MeshGeometryComponent* geometry = world.get<MeshGeometryComponent>(entity);
    REQUIRE(geometry != nullptr);
    const size_t expectedVertices = 25u * 25u;
    const size_t expectedIndices = 24u * 24u * 6u;
    REQUIRE(geometry->positions.size() == expectedVertices);
    REQUIRE(geometry->normals.size() == expectedVertices);
    REQUIRE(geometry->texcoords.size() == expectedVertices);
    REQUIRE(geometry->indices.size() == expectedIndices);

    bool unitNormals = true;
    for (const Vec3& normal : geometry->normals) {
        unitNormals = unitNormals && std::abs(normal.length() - 1.0f) < 2.0e-3f;
    }
    REQUIRE(unitNormals);

    const Vec3 before = geometry->positions[expectedVertices / 2];
    for (int frame = 0; frame < 10; ++frame) {
        Scene::tickNaturalFluids(world, 1.0f / 60.0f, true);
    }
    geometry = world.get<MeshGeometryComponent>(entity);
    REQUIRE(geometry != nullptr);
    REQUIRE((geometry->positions[expectedVertices / 2] - before).lengthSquared() > 0.0f);

    Scene::resetNaturalFluidRuntime();
}

TEST_CASE("Natural fluids volume system steps a real 3D solver", "[naturalfluids][scene]") {
    Scene::resetNaturalFluidRuntime();
    World world;
    NaturalFluidComponent component;
    component.mode = static_cast<u8>(NaturalFluidMode::FluidVolume);
    component.preset = static_cast<u8>(NaturalFluidPreset::Fire);
    component.sizeX = 6.0f;
    component.sizeY = 6.0f;
    component.sizeZ = 6.0f;
    component.fluidResolution = 14;
    component.fluidSourceRate = 1.0f;
    component.fluidSourceRadius = 1.2f;
    component.fluidSourceTemperature = 2.0f;
    component.previewInEditor = 1;
    Entity entity = addFluidEntity(world, component);

    for (int frame = 0; frame < 60; ++frame) {
        Scene::tickNaturalFluids(world, 1.0f / 60.0f, true);
    }

    const FluidWorld* fluid = Scene::naturalFluidWorldFor(world, entity.id());
    REQUIRE(fluid != nullptr);
    REQUIRE(fluid->volume().totalDensity() > 0.0f);
    REQUIRE(fluid->volume().temperatureCenterOfMass().z > 0.0f);

    const MeshGeometryComponent* geometry = world.get<MeshGeometryComponent>(entity);
    REQUIRE(geometry != nullptr);
    REQUIRE(!geometry->positions.empty());
    REQUIRE(geometry->positions.size() % 24u == 0u);
    REQUIRE(geometry->indices.size() == geometry->positions.size() / 4u * 6u);

    Scene::resetNaturalFluidRuntime();
}

TEST_CASE("NaturalFluidComponent survives a scene round-trip", "[naturalfluids][scene]") {
    Scene::resetNaturalFluidRuntime();
    const std::filesystem::path scenePath =
        std::filesystem::temp_directory_path() / "caffeine_natural_fluid_test.caf";

    {
        World world;
        NaturalFluidComponent component = realisticWater(32, 48.0f);
        component.windSpeed = 17.5f;
        component.shallowPhysics = 1;
        component.preset = static_cast<u8>(NaturalFluidPreset::Water);
        addFluidEntity(world, component);

        Editor::SceneSerializer serializer(world);
        REQUIRE(serializer.serialize(scenePath.string()));
    }

    World loaded;
    Editor::SceneSerializer serializer(loaded);
    REQUIRE(serializer.deserialize(scenePath.string()));

    bool found = false;
    ComponentQuery query;
    query.with<NaturalFluidComponent>();
    loaded.forEach<NaturalFluidComponent>(
        query, [&](Entity, NaturalFluidComponent& component) {
            found = true;
            REQUIRE(component.mode == static_cast<u8>(NaturalFluidMode::RealisticWater));
            REQUIRE(component.shallowPhysics == 1);
            REQUIRE(component.windSpeed == Approx(17.5f));
            REQUIRE(component.sizeX == Approx(48.0f));
            REQUIRE(component.surfaceResolution == 32u);
        });
    REQUIRE(found);

    std::error_code ec;
    std::filesystem::remove(scenePath, ec);
}

TEST_CASE("Natural fluids core runs the four product levels", "[naturalfluids][scene]") {
    const NaturalFluidSettings flat = waterPreset(WaterQuality::Flat);
    const NaturalFluidSettings wind = waterPreset(WaterQuality::Wind);
    const NaturalFluidSettings realistic = waterPreset(WaterQuality::Realistic);
    const NaturalFluidSettings fire = fluidPreset(FluidKind::Fire);

    REQUIRE(!flat.simulateShallowWater);
    REQUIRE(wind.simulateShallowWater);
    REQUIRE(realistic.waves.waveCount >= 12);
    REQUIRE(fire.simulateVolume);
    REQUIRE(fire.fluidPreset == FluidKind::Fire);

    FluidWorld world;
    world.configure(realistic);
    world.reset();
    for (int frame = 0; frame < 30; ++frame) world.update(1.0f / 60.0f);

    SurfaceMesh mesh;
    world.buildSurfaceMesh(16, 64.0f, 64.0f, mesh);
    REQUIRE(mesh.positions.size() == 17u * 17u);
    REQUIRE(mesh.indices.size() == 16u * 16u * 6u);

    f32 maxHeight = 0.0f;
    for (const Vec3& position : mesh.positions) {
        maxHeight = std::max(maxHeight, std::abs(position.y));
    }
    REQUIRE(maxHeight > 0.01f);

    FluidWorld fluid;
    fluid.configure(fire);
    fluid.reset();
    fluid.addVolumeSource(6.0f, 2.0f, 6.0f, 1.0f, 1.5f);
    fluid.volume().addTemperature(6.0f, 2.0f, 6.0f, 3.0f, 1.5f);
    for (int frame = 0; frame < 45; ++frame) fluid.update(1.0f / 60.0f);
    REQUIRE(fluid.volume().totalDensity() > 0.0f);
    REQUIRE(fluid.volume().temperatureCenterOfMass().z > 2.0f);
}
