#include "../Catch2/catch.hpp"

#include "caffeine/environment/NaturalFluids.hpp"

#include "ecs/Components3D.hpp"
#include "ecs/MeshComponents.hpp"
#include "ecs/MeshGeometry.hpp"
#include "ecs/World.hpp"
#include "scene/EditorSceneSerializer.hpp"
#include "fluid/FluidWorld.hpp"
#include "scene/NaturalFluidSystem.hpp"

#include <cmath>
#include <cstddef>
#include <cstring>
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

    // Analytic waves are displaced on the GPU: the grid is flat, uploaded once and never rebuilt,
    // while the wave data the vertex shader reads advances with time.
    Scene::GpuWaterWaves first;
    REQUIRE(Scene::naturalFluidGpuWaves(world, entity.id(), first));
    REQUIRE(first.count > 0);
    const u32 revision = geometry->revision;
    const Vec3 before = geometry->positions[expectedVertices / 2];
    for (int frame = 0; frame < 10; ++frame) {
        Scene::tickNaturalFluids(world, 1.0f / 60.0f, true);
    }
    geometry = world.get<MeshGeometryComponent>(entity);
    REQUIRE(geometry != nullptr);
    REQUIRE(geometry->revision == revision);
    REQUIRE((geometry->positions[expectedVertices / 2] - before).lengthSquared() == Approx(0.0f));
    // Culling still covers the waves.
    REQUIRE(geometry->boundsPadding.y > 0.0f);
    Scene::GpuWaterWaves later;
    REQUIRE(Scene::naturalFluidGpuWaves(world, entity.id(), later));
    REQUIRE(later.time > first.time);

    Scene::resetNaturalFluidRuntime();
}

TEST_CASE("Natural fluids with shallow-water physics keep the CPU surface", "[naturalfluids][scene]") {
    Scene::resetNaturalFluidRuntime();
    World world;
    NaturalFluidComponent component = realisticWater(24, 32.0f);
    component.shallowPhysics = 1;
    Entity entity = addFluidEntity(world, component);
    Scene::tickNaturalFluids(world, 1.0f / 60.0f, true);
    Scene::GpuWaterWaves waves;
    REQUIRE_FALSE(Scene::naturalFluidGpuWaves(world, entity.id(), waves));
    MeshGeometryComponent* geometry = world.get<MeshGeometryComponent>(entity);
    REQUIRE(geometry != nullptr);
    const size_t middle = geometry->positions.size() / 2;
    const Vec3 before = geometry->positions[middle];
    for (int frame = 0; frame < 10; ++frame) {
        Scene::tickNaturalFluids(world, 1.0f / 60.0f, true);
    }
    geometry = world.get<MeshGeometryComponent>(entity);
    REQUIRE((geometry->positions[middle] - before).lengthSquared() > 0.0f);
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

TEST_CASE("Authored waves that would fold the surface are scaled back", "[fluid][water]") {
    // Eight steep waves heading the same way: sum(Q k A) is about 3, far past the point where the
    // surface folds over itself (negative Jacobian, flipped normals, dark patches).
    WaveSettings settings;
    settings.quality = WaterQuality::Realistic;
    for (int i = 0; i < 8; ++i) {
        GerstnerWave wave;
        wave.direction = Vec2(1.0f, 0.1f * static_cast<f32>(i)).normalized();
        wave.wavelength = 6.0f + 1.5f * static_cast<f32>(i);
        wave.amplitude = 0.45f;
        wave.steepness = 1.0f;
        wave.phase = 0.7f * static_cast<f32>(i);
        settings.explicitWaves.push_back(wave);
    }
    WaveField field;
    field.configure(settings);
    REQUIRE(field.waves().size() == 8);
    f32 total = 0.0f;
    for (const GerstnerWave& wave : field.waves()) {
        total += wave.steepness * (6.28318530718f / wave.wavelength) * wave.amplitude;
    }
    CHECK(total <= 0.951f);
    // No fold anywhere: the surface keeps its orientation (positive Jacobian, upward normal).
    f32 minJacobian = 1e9f;
    f32 minNormalY = 1e9f;
    for (int ix = 0; ix < 60; ++ix) {
        for (int iz = 0; iz < 60; ++iz) {
            const WaveSample sample =
                field.sample(static_cast<f32>(ix) * 0.7f, static_cast<f32>(iz) * 0.7f, 2.0f);
            minJacobian = std::min(minJacobian, sample.jacobian);
            minNormalY = std::min(minNormalY, sample.normal.y);
        }
    }
    CHECK(minJacobian > 0.0f);
    CHECK(minNormalY > 0.0f);
}

TEST_CASE("Authored Gerstner waves replace the generated spectrum", "[fluid][water]") {
    WaveSettings settings;
    settings.quality = WaterQuality::Realistic;
    GerstnerWave swell;
    swell.direction = Vec2(1.0f, 0.0f);
    swell.wavelength = 20.0f;
    swell.amplitude = 0.5f;
    swell.steepness = 0.3f;
    settings.explicitWaves.push_back(swell);

    WaveField field;
    field.configure(settings);
    REQUIRE(field.waves().size() == 1);
    CHECK(field.waves()[0].omega > 0.0f);  // dispersion filled in
    // One 0.5 m wave: the surface stays within its amplitude.
    f32 lo = 1e9f, hi = -1e9f;
    for (int i = 0; i < 200; ++i) {
        const f32 h = field.heightAt(static_cast<f32>(i) * 0.3f, 0.0f, 1.25f);
        lo = std::min(lo, h);
        hi = std::max(hi, h);
    }
    CHECK(hi <= 0.5001f);
    CHECK(lo >= -0.5001f);
    CHECK(hi - lo > 0.8f);

    WaveSettings wind;
    wind.windSpeed = 14.0f;
    WaveField generated;
    generated.configure(wind);
    CHECK(generated.waves().size() > 1);
}

TEST_CASE("Custom waves survive the component prefix serialisation", "[fluid][water]") {
    NaturalFluidComponent component;
    component.customWaveCount = 2;
    component.customWaves[1][2] = 33.0f;
    static_assert(sizeof(NaturalFluidComponent) > 400, "waves are appended to the component");
    NaturalFluidComponent copy;
    std::memcpy(&copy, &component, sizeof(component));
    CHECK(copy.customWaveCount == 2);
    CHECK(copy.customWaves[1][2] == 33.0f);
    // An older scene stores a shorter prefix: the new fields keep their defaults.
    NaturalFluidComponent old;
    std::memcpy(&old, &component, offsetof(NaturalFluidComponent, customWaveCount));
    CHECK(old.customWaveCount == 0);
}

TEST_CASE("Mesh Renderer modifiers survive a scene round trip and old scenes still load", "[scene]") {
    const std::filesystem::path scenePath =
        std::filesystem::temp_directory_path() / "caffeine_modifiers_scene_test.caf";
    {
        World world;
        Entity entity = world.create("Wall");
        auto& renderer = world.add<MeshRendererComponent>(entity);
        renderer.meshPath = "Models/wall.obj";
        renderer.materialPath = "Materials/brick.mat";
        renderer.modTint = Vec4(0.5f, 0.6f, 0.7f, 1.0f);
        renderer.modWetness = 0.3f;
        renderer.modSnow = 0.8f;
        renderer.modAge = 0.1f;
        Editor::SceneSerializer serializer(world);
        REQUIRE(serializer.serialize(scenePath.string()));
    }
    World loaded;
    Editor::SceneSerializer serializer(loaded);
    REQUIRE(serializer.deserialize(scenePath.string()));
    bool found = false;
    ComponentQuery query;
    query.with<MeshRendererComponent>();
    loaded.forEach<MeshRendererComponent>(query, [&](Entity, MeshRendererComponent& renderer) {
        found = true;
        REQUIRE(renderer.materialPath == "Materials/brick.mat");
        REQUIRE(renderer.modTint.y == Approx(0.6f));
        REQUIRE(renderer.modWetness == Approx(0.3f));
        REQUIRE(renderer.modSnow == Approx(0.8f));
        REQUIRE(renderer.modAge == Approx(0.1f));
    });
    REQUIRE(found);
    std::filesystem::remove(scenePath);
}

TEST_CASE("Natural fluids: a new surface never reuses the geometry revision of a deleted one", "[naturalfluids][scene]") {
    Scene::resetNaturalFluidRuntime();
    World world;
    Entity first = addFluidEntity(world, realisticWater(24, 32.0f));
    Scene::tickNaturalFluids(world, 1.0f / 60.0f, true);
    const u32 firstRevision = world.get<MeshGeometryComponent>(first)->revision;
    Scene::resetNaturalFluidRuntime();
    // The mesh cache is keyed by entity id and ids are reused: the second surface must not look
    // "already uploaded" because its revision matches the first one's.
    World other;
    Entity second = addFluidEntity(other, realisticWater(24, 32.0f));
    Scene::tickNaturalFluids(other, 1.0f / 60.0f, true);
    const u32 secondRevision = other.get<MeshGeometryComponent>(second)->revision;
    CHECK(secondRevision > firstRevision);
    Scene::resetNaturalFluidRuntime();
}
