#include "catch.hpp"

#include "assets/MeshLoader.hpp"
#include "ecs/ComponentQuery.hpp"
#include "ecs/MeshComponents.hpp"
#include "ecs/World.hpp"
#include "scene/EditorSceneSerializer.hpp"

#include <cstring>
#include <string>
#include <vector>

using namespace Caffeine;

namespace {

// Two quads: the first two triangles use "bark", the next two "leaf", then one more "bark" triangle (it must end up
// with the other bark faces) and one face before any usemtl.
const char* kTwoMaterialObj =
    "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nv 2 0 0\nv 3 0 0\nv 3 1 0\n"
    "f 5 6 7\n"
    "usemtl bark\nf 1 2 3\nf 1 3 4\n"
    "usemtl leaf\nf 2 5 7\nf 2 7 3\n"
    "usemtl bark\nf 1 2 4\n";

}  // namespace

TEST_CASE("OBJ usemtl groups become one sub-mesh per material, in order of first use", "[slots][mesh]") {
    Assets::Mesh3D* mesh = Assets::MeshLoader::parseOBJ(kTwoMaterialObj, std::strlen(kTwoMaterialObj));
    REQUIRE(mesh != nullptr);
    // Three groups: the unnamed one first (faces before any usemtl), then bark, then leaf.
    REQUIRE(mesh->subMeshes.size() == 3);
    CHECK(std::string(mesh->subMeshes[0].name.cStr()) == "");
    CHECK(std::string(mesh->subMeshes[1].name.cStr()) == "bark");
    CHECK(std::string(mesh->subMeshes[2].name.cStr()) == "leaf");
    // 1 + 3 + 2 triangles, contiguous and covering the index buffer once.
    CHECK(mesh->subMeshes[0].indexCount == 3);
    CHECK(mesh->subMeshes[1].indexCount == 9);
    CHECK(mesh->subMeshes[2].indexCount == 6);
    u32 expected = 0;
    for (const Assets::SubMesh& sub : mesh->subMeshes) {
        CHECK(sub.indexOffset == expected);
        expected += sub.indexCount;
    }
    CHECK(expected == mesh->indices.size());
    // Both bark triangles from the first group and the later one landed together.
    for (u32 i = mesh->subMeshes[2].indexOffset; i < mesh->subMeshes[2].indexOffset + 6; ++i) {
        // leaf faces use vertex 5 (x == 2) or only x in 1..3; none is the single-triangle bark face.
        CHECK(mesh->vertices[mesh->indices[i]].position.x >= 1.0f);
    }
    delete mesh;
}

TEST_CASE("An OBJ with one material, or none, keeps a single sub-mesh", "[slots][mesh]") {
    const char* one = "v 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl stone\nf 1 2 3\n";
    Assets::Mesh3D* mesh = Assets::MeshLoader::parseOBJ(one, std::strlen(one));
    REQUIRE(mesh != nullptr);
    REQUIRE(mesh->subMeshes.size() == 1);
    CHECK(mesh->subMeshes[0].indexCount == 3);
    delete mesh;
}

TEST_CASE("OBJ vertex colours follow their triangles when the groups are reordered", "[slots][mesh]") {
    const char* text =
        "v 0 0 0 1 0 0\nv 1 0 0 1 0 0\nv 0 1 0 1 0 0\nv 5 0 0 0 1 0\nv 6 0 0 0 1 0\nv 5 1 0 0 1 0\n"
        "usemtl a\nf 1 2 3\nusemtl b\nf 4 5 6\nusemtl a\nf 1 3 2\n";
    Assets::Mesh3D* mesh = Assets::MeshLoader::parseOBJ(text, std::strlen(text));
    REQUIRE(mesh != nullptr);
    REQUIRE(mesh->subMeshes.size() == 2);
    REQUIRE(mesh->vertexColors.size() == mesh->vertices.size());
    for (const Assets::SubMesh& sub : mesh->subMeshes) {
        for (u32 i = sub.indexOffset; i < sub.indexOffset + sub.indexCount; ++i) {
            const bool red = mesh->vertexColors[mesh->indices[i]].x > 0.5f;
            CHECK(red == (sub.materialIndex == 0));
        }
    }
    delete mesh;
}

TEST_CASE("Material slot lists split at ; and keep empty slots in place", "[slots]") {
    using ECS::splitMaterialSlots;
    CHECK(splitMaterialSlots("").empty());
    CHECK(splitMaterialSlots("a.mat") == std::vector<std::string>{"a.mat"});
    CHECK(splitMaterialSlots(" a.mat ; ;c.mat") == std::vector<std::string>{"a.mat", "", "c.mat"});
    CHECK(splitMaterialSlots("a;b;") == std::vector<std::string>{"a", "b", ""});
}

TEST_CASE("Material slots and Micromesh settings survive a scene save and load", "[slots][scene]") {
    ECS::World world;
    ECS::Entity entity = world.create();
    auto& filter = world.add<ECS::MeshFilterComponent>(entity);
    filter.primitive = ECS::MeshPrimitive::Custom;
    filter.customMeshPath = "Meshes/tree.obj";
    filter.customMaterialPath = "Materials/fallback.mat";
    filter.slotMaterials = "Materials/bark.mat;;Materials/leaf.mat";
    auto& renderer = world.add<ECS::MeshRendererComponent>(entity);
    renderer.micromesh = true;
    renderer.micromeshOcclusion = true;
    renderer.micromeshPixels = 2.5f;
    renderer.modWetness = 0.25f;

    std::vector<u8> snapshot;
    Editor::SceneSerializer serializer(world);
    REQUIRE(serializer.serializeToMemory(snapshot));

    ECS::World restored;
    Editor::SceneSerializer loader(restored);
    REQUIRE(loader.deserializeFromMemory(snapshot));

    ECS::Entity found;
    ECS::ComponentQuery query;
    query.with<ECS::MeshFilterComponent>();
    restored.forEach<ECS::MeshFilterComponent>(query, [&](ECS::Entity e, ECS::MeshFilterComponent&) { found = e; });
    REQUIRE(found.isValid());
    const auto* f = restored.get<ECS::MeshFilterComponent>(found);
    const auto* r = restored.get<ECS::MeshRendererComponent>(found);
    REQUIRE(f != nullptr);
    REQUIRE(r != nullptr);
    CHECK(f->customMaterialPath == "Materials/fallback.mat");
    CHECK(f->slotMaterials == "Materials/bark.mat;;Materials/leaf.mat");
    CHECK(f->customMeshPath == "Meshes/tree.obj");
    CHECK(r->micromesh);
    CHECK(r->micromeshOcclusion);
    CHECK(r->micromeshPixels == Approx(2.5f));
    CHECK(r->modWetness == Approx(0.25f));
}
