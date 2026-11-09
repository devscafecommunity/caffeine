#include "catch.hpp"

#include "ecs/ComponentQuery.hpp"
#include "ecs/TerrainComponents.hpp"
#include "ecs/World.hpp"
#include "scene/EditorSceneSerializer.hpp"
#include "math/Mat4.hpp"
#include "terrain/TerrainCache.hpp"
#include "terrain/TerrainHeightmap.hpp"
#include "terrain/TerrainSculptor.hpp"
#include "terrain/TerrainSplatPainter.hpp"
#include "terrain/TerrainSplatmap.hpp"

#include <cstring>
#include <filesystem>

using namespace Caffeine;
using namespace Caffeine::Terrain;

namespace {

ECS::TerrainComponent flatTerrain() {
    ECS::TerrainComponent terrain;
    terrain.resolutionX = 17;
    terrain.resolutionZ = 17;
    terrain.splatResolutionScale = 1;
    terrain.worldSizeX = 16.0f;
    terrain.worldSizeZ = 16.0f;
    terrain.maxHeight = 10.0f;
    terrain.useChunks = false;
    return terrain;
}

}  // namespace

TEST_CASE("Terrain splatmap defaults to grass layer", "[terrain][splat]") {
    TerrainSplatmap splatmap(8, 8);
    const Vec4 weights = splatmap.sample(0, 0);
    REQUIRE(weights.x == Approx(1.0f));
    REQUIRE(weights.y == Approx(0.0f));
    REQUIRE(weights.z == Approx(0.0f));
    REQUIRE(weights.w == Approx(0.0f));

    splatmap.fillLayer(2, 1.0f);
    const Vec4 sand = splatmap.sample(4, 4);
    REQUIRE(sand.z == Approx(1.0f));
}

TEST_CASE("Terrain flatten uses the clicked height", "[terrain][sculpt]") {
    TerrainHeightmap heightmap(17, 17);
    heightmap.fill(0.2f);
    const ECS::TerrainComponent terrain = flatTerrain();

    TerrainBrushSettings brush;
    brush.mode = TerrainBrushMode::Flatten;
    brush.radius = 4.0f;
    brush.strength = 1.0f;
    brush.targetHeight = 0.7f;

    TerrainVertexBounds bounds;
    TerrainSculptor::applyBrush(heightmap, terrain, Mat4::identity(), Vec3(0.0f, 0.0f, 0.0f),
                                brush, 1.0f / 60.0f, &bounds);
    REQUIRE(bounds.valid);
    REQUIRE(heightmap.sampleNormalized(8, 8) > 0.6f);
    REQUIRE(heightmap.sampleNormalized(8, 8) < 0.8f);
    // Outside the brush radius the terrain must not change.
    REQUIRE(heightmap.sampleNormalized(0, 0) == Approx(0.2f));
}

TEST_CASE("Terrain raise and lower brushes edit only the brush area", "[terrain][sculpt]") {
    TerrainHeightmap heightmap(17, 17);
    heightmap.fill(0.5f);
    const ECS::TerrainComponent terrain = flatTerrain();

    TerrainBrushSettings brush;
    brush.mode = TerrainBrushMode::Raise;
    brush.radius = 3.0f;
    brush.strength = 0.5f;
    TerrainSculptor::applyBrush(heightmap, terrain, Mat4::identity(), Vec3(0.0f, 0.0f, 0.0f),
                                brush, 1.0f / 60.0f);
    const f32 raised = heightmap.sampleNormalized(8, 8);
    REQUIRE(raised > 0.5f);
    REQUIRE(heightmap.sampleNormalized(0, 0) == Approx(0.5f));

    brush.mode = TerrainBrushMode::Lower;
    TerrainSculptor::applyBrush(heightmap, terrain, Mat4::identity(), Vec3(0.0f, 0.0f, 0.0f),
                                brush, 1.0f / 60.0f);
    REQUIRE(heightmap.sampleNormalized(8, 8) < raised);
}

TEST_CASE("Terrain raycast hits the height field", "[terrain][sculpt]") {
    TerrainHeightmap heightmap(17, 17);
    heightmap.fill(0.5f);
    const ECS::TerrainComponent terrain = flatTerrain();

    Vec3 hit;
    REQUIRE(TerrainSculptor::raycast(Mat4::identity(), heightmap, terrain,
                                     Vec3(0.0f, 20.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f), hit));
    REQUIRE(hit.y == Approx(5.0f).margin(0.05f));

    REQUIRE_FALSE(TerrainSculptor::raycast(Mat4::identity(), heightmap, terrain,
                                           Vec3(40.0f, 20.0f, 40.0f),
                                           Vec3(0.0f, -1.0f, 0.0f), hit));
}

TEST_CASE("Terrain splat painter moves weight to the target layer", "[terrain][splat]") {
    TerrainSplatmap splatmap(17, 17);
    splatmap.fillLayer(0, 1.0f);
    const ECS::TerrainComponent terrain = flatTerrain();

    TerrainSplatBrushSettings brush;
    brush.targetLayer = 2;
    brush.radius = 4.0f;
    brush.strength = 1.0f;
    TerrainSplatPainter::applyBrush(splatmap, terrain, Mat4::identity(),
                                    Vec3(0.0f, 0.0f, 0.0f), brush, 1.0f / 60.0f);
    TerrainSplatPainter::applyBrush(splatmap, terrain, Mat4::identity(),
                                    Vec3(0.0f, 0.0f, 0.0f), brush, 1.0f / 60.0f);

    const Vec4 weights = splatmap.sample(8, 8);
    REQUIRE(weights.z > weights.x);
    REQUIRE(weights.x + weights.y + weights.z + weights.w == Approx(1.0f));
}

TEST_CASE("Terrain cache initializes new terrains with grass", "[terrain][cache]") {
    ECS::World world;
    ECS::Entity entity = world.create();
    auto& terrain = world.add<ECS::TerrainComponent>(entity);
    terrain.resolutionX = 33;
    terrain.resolutionZ = 33;
    terrain.splatResolutionScale = 1;
    terrain.useChunks = false;

    auto& cache = TerrainCache::instance();
    cache.initializeEntity(world, entity);

    const TerrainSplatmap* splatmap = cache.splatmapFor(entity);
    REQUIRE(splatmap != nullptr);
    const Vec4 weights = splatmap->sample(0, 0);
    REQUIRE(weights.x == Approx(1.0f));

    cache.removeEntity(entity);
}


TEST_CASE("Terrain snapshots restore heights and splat weights", "[terrain][undo]") {
    ECS::World world;
    ECS::Entity entity = world.create();
    auto& terrain = world.add<ECS::TerrainComponent>(entity);
    terrain.resolutionX = 17;
    terrain.resolutionZ = 17;
    terrain.splatResolutionScale = 1;
    terrain.useChunks = false;
    terrain.tint = Vec3(0.2f, 0.3f, 0.4f);

    auto& cache = TerrainCache::instance();
    cache.initializeEntity(world, entity);
    auto* heightmap = cache.heightmapFor(entity);
    auto* splatmap = cache.splatmapFor(entity);
    REQUIRE(heightmap != nullptr);
    REQUIRE(splatmap != nullptr);

    heightmap->setNormalized(8, 8, 0.75f);
    splatmap->set(8, 8, Vec4(0.0f, 0.0f, 1.0f, 0.0f));

    std::vector<u8> snapshot;
    Editor::SceneSerializer serializer(world);
    REQUIRE(serializer.serializeToMemory(snapshot));

    heightmap->setNormalized(8, 8, 0.05f);
    splatmap->set(8, 8, Vec4(1.0f, 0.0f, 0.0f, 0.0f));

    ECS::World restored;
    Editor::SceneSerializer restoreSerializer(restored);
    REQUIRE(restoreSerializer.deserializeFromMemory(snapshot));

    ECS::Entity restoredEntity;
    ECS::ComponentQuery query;
    query.with<ECS::TerrainComponent>();
    restored.forEach<ECS::TerrainComponent>(
        query, [&](ECS::Entity e, ECS::TerrainComponent&) { restoredEntity = e; });
    REQUIRE(restoredEntity.isValid());

    const TerrainHeightmap* restoredHeightmap = cache.heightmapFor(restoredEntity);
    const TerrainSplatmap* restoredSplatmap = cache.splatmapFor(restoredEntity);
    REQUIRE(restoredHeightmap != nullptr);
    REQUIRE(restoredSplatmap != nullptr);
    REQUIRE(restoredHeightmap->sampleNormalized(8, 8) == Approx(0.75f));
    REQUIRE(restoredSplatmap->sample(8, 8).z == Approx(1.0f));
    const auto* restoredTerrain = restored.get<ECS::TerrainComponent>(restoredEntity);
    REQUIRE(restoredTerrain != nullptr);
    REQUIRE(restoredTerrain->tint.x == Approx(0.2f));
    REQUIRE(restoredTerrain->tint.y == Approx(0.3f));
    REQUIRE(restoredTerrain->tint.z == Approx(0.4f));
}


TEST_CASE("Terrain cache migrates legacy unpainted Light splat", "[terrain][migration]") {
    ECS::World world;
    ECS::Entity entity = world.create();
    auto& terrain = world.add<ECS::TerrainComponent>(entity);
    terrain.resolutionX = 17;
    terrain.resolutionZ = 17;
    terrain.splatResolutionScale = 1;
    terrain.useChunks = false;
    std::strncpy(terrain.texturePath,
                 "kenney_prototype-textures/PNG/Light/texture_07.png",
                 sizeof(terrain.texturePath) - 1);
    const char* legacyLayers[] = {
        "kenney_prototype-textures/PNG/Green/texture_07.png",
        "kenney_prototype-textures/PNG/Dark/texture_07.png",
        "kenney_prototype-textures/PNG/Orange/texture_07.png",
        "kenney_prototype-textures/PNG/Light/texture_07.png",
    };
    for (u32 i = 0; i < ECS::kTerrainSplatLayerCount; ++i) {
        std::strncpy(terrain.splatLayerPaths[i], legacyLayers[i],
                     sizeof(terrain.splatLayerPaths[i]) - 1);
    }

    auto& cache = TerrainCache::instance();
    cache.initializeEntity(world, entity);
    auto* splatmap = cache.splatmapFor(entity);
    REQUIRE(splatmap != nullptr);
    // Put the component back into its legacy state before saving so the load
    // path exercises the migration instead of initializeEntity.
    std::strncpy(terrain.texturePath,
                 "kenney_prototype-textures/PNG/Light/texture_07.png",
                 sizeof(terrain.texturePath) - 1);
    for (u32 i = 0; i < ECS::kTerrainSplatLayerCount; ++i) {
        std::strncpy(terrain.splatLayerPaths[i], legacyLayers[i],
                     sizeof(terrain.splatLayerPaths[i]) - 1);
    }
    splatmap->fillLayer(3, 1.0f);

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "caffeine_terrain_legacy_splat.cterrain";
    REQUIRE(cache.saveTerrainFile(entity, path, true));
    REQUIRE(cache.loadTerrainFile(world, entity, terrain, path));

    REQUIRE(std::string(terrain.texturePath).find("Ground068") != std::string::npos);
    const TerrainSplatmap* restored = cache.splatmapFor(entity);
    REQUIRE(restored != nullptr);
    REQUIRE(restored->sample(8, 8).x == Approx(1.0f));

    std::filesystem::remove(path);
    cache.removeEntity(entity);
}
