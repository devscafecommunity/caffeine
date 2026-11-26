#include "catch.hpp"

#include "ecs/ComponentQuery.hpp"
#include "ecs/Components.hpp"
#include "ecs/World.hpp"
#include "ecs/WorldInstancesComponents.hpp"
#include "math/Quat.hpp"
#include "scene/ComponentReflection.hpp"
#include "scene/EditorSceneSerializer.hpp"
#include "scene/HierarchySystem.hpp"
#include "world/WorldInstanceDraw.hpp"
#include "world/WorldInstanceFile.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace Caffeine;
using namespace Caffeine::World;

namespace {

InstanceRecord record(u32 cls, u32 variant, i32 cx, i32 cz, Vec3 position, f32 scale = 1.0f) {
    InstanceRecord r;
    r.classIndex = cls;
    r.variant = variant;
    r.cellX = cx;
    r.cellZ = cz;
    r.position = {position.x, position.y, position.z};
    r.scale = scale;
    return r;
}

bool near(const Mat4& a, const Mat4& b, f32 eps = 1e-4f) {
    for (int i = 0; i < 16; ++i) {
        if (std::abs(a.data()[i] - b.data()[i]) > eps) return false;
    }
    return true;
}

void writeText(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

}  // namespace

TEST_CASE("World instances: the models file reads variant and cell entries", "[world][world-instances]") {
    const std::string text = R"(# trees of the valley
0 0 Nature/oak0_trunk.obj slots Materials/bark.mat
0 0 Nature/oak0_leaves.obj slots Materials/leaf.mat;Materials/leaf_back.mat cutout on   # leaves
0 1 Nature/oak1.obj cutout off

1 cell -3 7 Nature/unique_rock.obj
1 cell -3 7 Nature/unique_moss.obj cutout
)";
    InstanceModels models;
    std::string error;
    REQUIRE(parseInstanceModels(text, models, &error));
    REQUIRE(models.variants.size() == 2);
    const auto& oak0 = models.variants.at({0, 0});
    REQUIRE(oak0.size() == 2);
    CHECK(oak0[0].meshPath == "Nature/oak0_trunk.obj");
    CHECK(oak0[0].slots == "Materials/bark.mat");
    CHECK_FALSE(oak0[0].cutout);
    CHECK(oak0[1].slots == "Materials/leaf.mat;Materials/leaf_back.mat");
    CHECK(oak0[1].cutout);
    CHECK_FALSE(models.variants.at({0, 1})[0].cutout);
    const auto& rock = models.cells.at({1, -3, 7});
    REQUIRE(rock.size() == 2);
    CHECK(rock[0].meshPath == "Nature/unique_rock.obj");
    CHECK(rock[1].cutout);

    SECTION("errors name the line") {
        CHECK_FALSE(parseInstanceModels("# ok\nx 0 a.obj\n", models, &error));
        CHECK(error.find("line 2") != std::string::npos);
        CHECK_FALSE(parseInstanceModels("0 0\n", models, &error));
        CHECK(error.find("line 1") != std::string::npos);
        CHECK_FALSE(parseInstanceModels("0 0 a.obj shiny on\n", models, &error));
        CHECK(error.find("shiny") != std::string::npos);
        CHECK_FALSE(parseInstanceModels("0 0 a.obj slots\n", models, &error));
        CHECK_FALSE(parseInstanceModels("0 cell 1 a.obj\n", models, &error));
        CHECK_FALSE(parseInstanceModels("0 cell 1 z a.obj\n", models, &error));
        CHECK_FALSE(parseInstanceModels("0 -1 a.obj\n", models, &error));
        CHECK(models.empty());
    }
}

TEST_CASE("World instances: a cell entry wins over the variant entry", "[world][world-instances]") {
    InstanceModels models;
    REQUIRE(parseInstanceModels("2 1 shared.obj\n2 cell 4 5 unique.obj\n", models));
    const auto* atCell = models.partsFor(record(2, 1, 4, 5, Vec3(0, 0, 0)));
    REQUIRE(atCell != nullptr);
    CHECK(atCell->front().meshPath == "unique.obj");
    const auto* elsewhere = models.partsFor(record(2, 1, 4, 6, Vec3(0, 0, 0)));
    REQUIRE(elsewhere != nullptr);
    CHECK(elsewhere->front().meshPath == "shared.obj");
    CHECK(models.partsFor(record(2, 0, 0, 0, Vec3(0, 0, 0))) == nullptr);  // no model for that variant
    CHECK(models.partsFor(record(3, 1, 4, 5, Vec3(0, 0, 0))) == nullptr);
}

TEST_CASE("World instances: the instance matrix follows the scatter convention", "[world][world-instances]") {
    InstanceRecord r = record(0, 0, 0, 0, Vec3(12.0f, 3.5f, -4.0f), 1.7f);
    r.up = {0.3f, 0.9f, -0.2f};
    r.yaw = 1.1f;
    const Mat4 m = instanceLocalMatrix(r);

    // What caffeine_session_scatter_terrain hands an entity: the same quaternion as Euler degrees on a Transform.
    const Vec3 up = Vec3(r.up[0], r.up[1], r.up[2]).normalized();
    const Vec3 axis = Vec3(0.0f, 1.0f, 0.0f).cross(up);
    const Quat align = Quat::fromAxisAngle(axis.normalized(), std::acos(std::clamp(up.y, -1.0f, 1.0f)));
    const Quat q = (align * Quat::fromAxisAngle(Vec3(0.0f, 1.0f, 0.0f), r.yaw)).normalized();
    const Vec3 euler = q.toEuler();
    ECS::Transform t;
    t.position = Vec3(12.0f, 3.5f, -4.0f);
    t.rotation = euler * 57.29577951308232f;
    t.scale = Vec3(1.7f, 1.7f, 1.7f);
    CHECK(near(m, Scene::buildLocalTRS(t), 1e-3f));

    // +Y goes to the instance's up; the pivot to its position.
    const Vec3 origin = m.transformPoint(Vec3(0.0f, 0.0f, 0.0f));
    const Vec3 tip = m.transformPoint(Vec3(0.0f, 1.0f, 0.0f));
    CHECK((origin - Vec3(12.0f, 3.5f, -4.0f)).length() < 1e-4f);
    CHECK(((tip - origin) / 1.7f - up).length() < 1e-4f);

    // Upright with no yaw: translation and scale only.
    InstanceRecord plain = record(0, 0, 0, 0, Vec3(1.0f, 2.0f, 3.0f), 2.0f);
    CHECK(near(instanceLocalMatrix(plain), Mat4::translation(1.0f, 2.0f, 3.0f) * Mat4::scale(2.0f)));
}

TEST_CASE("World instances: tints are quantised so instances keep batching", "[world][world-instances]") {
    InstanceRecord a = record(0, 0, 0, 0, Vec3(0, 0, 0));
    InstanceRecord b = a;
    a.tint = {0.9001f, 1.004f, 0.5f};
    b.tint = {0.9049f, 0.999f, 0.5101f};
    const Vec3 qa = quantisedInstanceTint(a);
    const Vec3 qb = quantisedInstanceTint(b);
    CHECK(qa.x == qb.x);
    CHECK(qa.y == qb.y);
    CHECK(qa.z == qb.z);
    CHECK(qa.y == 1.0f);
    CHECK(std::abs(qa.x - 0.9f) <= kInstanceModifierStep * 0.5f);
    CHECK(quantiseInstanceModifier(-0.01f) == 0.0f);
}

TEST_CASE("World instances: chunks are culled by draw distance and frustum", "[world][world-instances]") {
    InstanceFile file;
    file.chunkSize = 64.0f;
    InstanceChunk nearChunk;
    nearChunk.instances = {record(0, 0, 0, 0, Vec3(10, 0, 10)), record(0, 0, 1, 1, Vec3(50, 2, 40), 2.0f)};
    InstanceChunk farChunk;
    farChunk.chunkX = 15;
    farChunk.instances = {record(0, 0, 0, 0, Vec3(1000, 0, 10))};
    file.chunks = {nearChunk, farChunk, InstanceChunk{}};
    const std::vector<InstanceChunkBounds> bounds = computeChunkBounds(file);
    REQUIRE(bounds.size() == 3);
    CHECK(bounds[0].min.x == 10.0f);
    CHECK(bounds[0].max.x == 50.0f);
    CHECK(bounds[0].max.y == 2.0f);
    CHECK(bounds[0].maxScale == 2.0f);
    CHECK(bounds[2].empty);

    const Mat4 identity = Mat4::identity();
    const Vec3 camera(0.0f, 2.0f, 0.0f);
    // No limit and no frustum: everything with instances.
    CHECK(chunkVisible(bounds[0], identity, 1.0f, nullptr, camera, 0.0f));
    CHECK(chunkVisible(bounds[1], identity, 1.0f, nullptr, camera, 0.0f));
    CHECK_FALSE(chunkVisible(bounds[2], identity, 1.0f, nullptr, camera, 0.0f));
    // A 200 m draw distance keeps the near chunk only; the model radius grows the box.
    CHECK(chunkVisible(bounds[0], identity, 1.0f, nullptr, camera, 200.0f));
    CHECK_FALSE(chunkVisible(bounds[1], identity, 1.0f, nullptr, camera, 200.0f));
    CHECK_FALSE(chunkVisible(bounds[0], identity, 0.0f, nullptr, Vec3(-30, 0, 10), 39.0f));
    CHECK(chunkVisible(bounds[0], identity, 5.0f, nullptr, Vec3(-30, 0, 10), 39.0f));  // 5 m x scale 2 = 10 m
    // The entity's transform moves the chunk: shifted 1000 m back, the far chunk is the near one.
    CHECK(chunkVisible(bounds[1], Mat4::translation(-1000.0f, 0.0f, 0.0f), 1.0f, nullptr, camera, 200.0f));

    // Looking down +X: the far chunk is ahead, a chunk behind the camera is not seen.
    const Spatial::Frustum ahead =
        Spatial::Frustum::fromCamera(camera, camera + Vec3(1, 0, 0), Vec3(0, 1, 0), 1.0f, 1.5f, 0.1f, 5000.0f);
    CHECK(chunkVisible(bounds[1], identity, 1.0f, &ahead, camera, 0.0f));
    CHECK_FALSE(chunkVisible(bounds[1], Mat4::translation(-2000.0f, 0.0f, 0.0f), 1.0f, &ahead, camera, 0.0f));
}

TEST_CASE("World instances: bakes and models load once and reload when changed", "[world][world-instances]") {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "caffeine-world-instances-test";
    std::filesystem::remove_all(root);
    InstanceFile file;
    InstanceChunk chunk;
    chunk.instances = {record(0, 0, 0, 0, Vec3(1, 0, 1)), record(0, 1, 1, 0, Vec3(3, 0, 1))};
    file.chunks = {chunk};
    InstanceFileClass oak;
    oak.id = "oak";
    oak.variants = 2;
    file.classes = {oak};
    std::filesystem::create_directories(root / "assets" / "Worlds");
    REQUIRE(writeInstanceFile(root / "assets" / "Worlds" / "valley.cinst", file));
    writeText(root / "assets" / "Worlds" / "valley.models", "0 0 a.obj\n");

    // Paths resolve like a Mesh Filter's Mesh: relative to the project and its assets folder.
    CHECK(resolveWorldAssetPath("Worlds/valley.cinst", root.string()) ==
          (root / "assets" / "Worlds" / "valley.cinst").string());
    CHECK(resolveWorldAssetPath("Worlds/missing.cinst", root.string()).empty());

    WorldInstanceCache& cache = WorldInstanceCache::instance();
    cache.clear();
    const u64 before = cache.loads();
    const WorldInstanceSet& first = cache.get("Worlds/valley.cinst", "Worlds/valley.models", root.string());
    CHECK(first.error.empty());
    CHECK(first.bake.instanceCount() == 2);
    CHECK(first.chunkBounds.size() == 1);
    CHECK(first.models.variants.size() == 1);
    CHECK(cache.loads() == before + 2);
    cache.get("Worlds/valley.cinst", "Worlds/valley.models", root.string());
    CHECK(cache.loads() == before + 2);  // unchanged files are not read again

    const std::filesystem::path models = root / "assets" / "Worlds" / "valley.models";
    writeText(models, "0 0 a.obj\n0 1 b.obj\n");
    std::filesystem::last_write_time(models, std::filesystem::last_write_time(models) + std::chrono::seconds(5));
    const WorldInstanceSet& second = cache.get("Worlds/valley.cinst", "Worlds/valley.models", root.string());
    CHECK(second.models.variants.size() == 2);
    CHECK(cache.loads() == before + 4);

    const WorldInstanceSet& missing = cache.get("Worlds/none.cinst", "Worlds/valley.models", root.string());
    CHECK(missing.bake.chunks.empty());
    CHECK(missing.error.find("not found") != std::string::npos);
    cache.clear();
    std::filesystem::remove_all(root);
}

TEST_CASE("World instances: the component is reflected and survives a scene save and load",
          "[world][world-instances][scene]") {
    namespace Refl = Scene::Reflection;
    const Refl::ComponentDesc* desc = Refl::findByName("World Instances");
    REQUIRE(desc != nullptr);
    CHECK(std::string(desc->name) == "World Instances");
    REQUIRE(desc->fields.size() == 4);
    CHECK(std::string(desc->fields[0].name) == "Bake");
    CHECK(desc->fields[0].kind == Refl::FieldKind::String);
    CHECK(std::string(desc->fields[1].name) == "Models");
    CHECK(desc->fields[1].kind == Refl::FieldKind::String);
    CHECK(std::string(desc->fields[2].name) == "Draw distance");
    CHECK(desc->fields[2].kind == Refl::FieldKind::F32);
    CHECK(std::string(desc->fields[3].name) == "Cast shadows");
    CHECK(desc->fields[3].kind == Refl::FieldKind::Bool);
    CHECK(Refl::findById(desc->id) == desc);

    ECS::World world;
    const ECS::Entity entity = world.create();
    desc->add(world, entity);
    const ECS::WorldInstancesComponent* defaults = world.get<ECS::WorldInstancesComponent>(entity);
    REQUIRE(defaults != nullptr);
    CHECK(defaults->drawDistance == 0.0f);
    CHECK(defaults->castShadows);

    Refl::FieldValue value;
    value.kind = Refl::FieldKind::String;
    std::snprintf(value.text, sizeof(value.text), "%s", "Worlds/valley.cinst");
    REQUIRE(desc->fields[0].set(world, entity, value));
    std::snprintf(value.text, sizeof(value.text), "%s", "Worlds/valley.models");
    REQUIRE(desc->fields[1].set(world, entity, value));
    value = Refl::FieldValue{};
    value.vec[0] = 350.0f;
    REQUIRE(desc->fields[2].set(world, entity, value));
    value = Refl::FieldValue{};
    value.kind = Refl::FieldKind::Bool;
    value.boolean = 0;
    REQUIRE(desc->fields[3].set(world, entity, value));

    std::vector<u8> snapshot;
    Editor::SceneSerializer serializer(world);
    REQUIRE(serializer.serializeToMemory(snapshot));
    ECS::World restored;
    Editor::SceneSerializer loader(restored);
    REQUIRE(loader.deserializeFromMemory(snapshot));

    ECS::Entity found;
    ECS::ComponentQuery query;
    query.with<ECS::WorldInstancesComponent>();
    restored.forEach<ECS::WorldInstancesComponent>(query,
                                                   [&](ECS::Entity e, ECS::WorldInstancesComponent&) { found = e; });
    REQUIRE(found.isValid());
    const ECS::WorldInstancesComponent* c = restored.get<ECS::WorldInstancesComponent>(found);
    REQUIRE(c != nullptr);
    CHECK(c->bakePath == "Worlds/valley.cinst");
    CHECK(c->modelsPath == "Worlds/valley.models");
    CHECK(c->drawDistance == 350.0f);
    CHECK_FALSE(c->castShadows);
}
