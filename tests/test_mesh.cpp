#include "catch.hpp"
#include "../src/Caffeine.hpp"
#include "../src/ecs/Components3D.hpp"
#include "../src/assets/MeshTypes.hpp"
#include "../src/assets/MeshLoader.hpp"

#include <cmath>

using namespace Caffeine;
using namespace Caffeine::ECS;
using namespace Caffeine::Assets;

static constexpr f32 kEps = 0.001f;
static bool approxEq(f32 a, f32 b) { return std::fabs(a - b) < kEps; }

static const char* kTriangleOBJ = R"(
v 0 0 0
v 1 0 0
v 0 1 0
vn 0 0 1
vt 0 0
vt 1 0
vt 0 1
f 1/1/1 2/2/1 3/3/1
)";

static const char* kQuadOBJ = R"(
v 0 0 0
v 1 0 0
v 1 1 0
v 0 1 0
vn 0 0 1
f 1//1 2//1 3//1 4//1
)";

static const char* kSimpleOBJ = R"(
v 0 0 0
v 1 0 0
v 0 1 0
f 1 2 3
)";

// ============================================================================
// Vertex3D
// ============================================================================

TEST_CASE("Vertex3D - defaults to zero", "[mesh]") {
    Vertex3D v = {};
    REQUIRE(approxEq(v.position.x, 0.0f));
    REQUIRE(approxEq(v.position.y, 0.0f));
    REQUIRE(approxEq(v.position.z, 0.0f));
    REQUIRE(approxEq(v.normal.x, 0.0f));
    REQUIRE(approxEq(v.normal.y, 0.0f));
    REQUIRE(approxEq(v.normal.z, 0.0f));
    REQUIRE(approxEq(v.texcoord.x, 0.0f));
    REQUIRE(approxEq(v.texcoord.y, 0.0f));
    REQUIRE(approxEq(v.tangent.x, 0.0f));
    REQUIRE(approxEq(v.tangent.y, 0.0f));
    REQUIRE(approxEq(v.tangent.z, 0.0f));
    REQUIRE(approxEq(v.tangent.w, 0.0f));
}

// ============================================================================
// Rect3D
// ============================================================================

TEST_CASE("Rect3D - center computed correctly", "[mesh]") {
    Rect3D r;
    r.min = Vec3(0.0f, 0.0f, 0.0f);
    r.max = Vec3(2.0f, 4.0f, 6.0f);
    Vec3 c = r.center();
    REQUIRE(approxEq(c.x, 1.0f));
    REQUIRE(approxEq(c.y, 2.0f));
    REQUIRE(approxEq(c.z, 3.0f));
}

TEST_CASE("Rect3D - extents computed correctly", "[mesh]") {
    Rect3D r;
    r.min = Vec3(0.0f, 0.0f, 0.0f);
    r.max = Vec3(2.0f, 4.0f, 6.0f);
    Vec3 e = r.extents();
    REQUIRE(approxEq(e.x, 1.0f));
    REQUIRE(approxEq(e.y, 2.0f));
    REQUIRE(approxEq(e.z, 3.0f));
}

// ============================================================================
// Color
// ============================================================================

TEST_CASE("Color - defaults", "[mesh]") {
    Color c = {};
    REQUIRE(approxEq(c.a, 1.0f));
}

TEST_CASE("Color - white is 1,1,1,1", "[mesh]") {
    Color c = Color::white();
    REQUIRE(approxEq(c.r, 1.0f));
    REQUIRE(approxEq(c.g, 1.0f));
    REQUIRE(approxEq(c.b, 1.0f));
    REQUIRE(approxEq(c.a, 1.0f));
}

TEST_CASE("Color - black is 0,0,0,1", "[mesh]") {
    Color c = Color::black();
    REQUIRE(approxEq(c.r, 0.0f));
    REQUIRE(approxEq(c.g, 0.0f));
    REQUIRE(approxEq(c.b, 0.0f));
    REQUIRE(approxEq(c.a, 1.0f));
}

// ============================================================================
// SubMesh
// ============================================================================

TEST_CASE("SubMesh - defaults", "[mesh]") {
    SubMesh sm;
    REQUIRE(sm.indexOffset == 0);
    REQUIRE(sm.indexCount == 0);
    REQUIRE(sm.materialIndex == 0);
}

// ============================================================================
// Material3D
// ============================================================================

TEST_CASE("Material3D - defaults", "[mesh]") {
    Material3D mat;
    REQUIRE(approxEq(mat.roughness, 0.5f));
    REQUIRE(approxEq(mat.metallic, 0.0f));
}

TEST_CASE("Material3D - roughness default is 0.5", "[mesh]") {
    Material3D mat;
    REQUIRE(approxEq(mat.roughness, 0.5f));
}

// ============================================================================
// Mesh3D
// ============================================================================

TEST_CASE("Mesh3D - empty state", "[mesh]") {
    Mesh3D mesh;
    REQUIRE(mesh.vertices.empty());
    REQUIRE(mesh.indices.empty());
    REQUIRE(mesh.subMeshes.empty());
    REQUIRE(mesh.lodCount == 1);
}

// ============================================================================
// MeshRenderer
// ============================================================================

TEST_CASE("MeshRenderer - defaults", "[mesh]") {
    MeshRenderer mr;
    REQUIRE(mr.mesh == nullptr);
    REQUIRE(mr.material == nullptr);
    REQUIRE(mr.castShadows == true);
    REQUIRE(mr.receiveShadows == true);
}

// ============================================================================
// Components3D
// ============================================================================

TEST_CASE("Position3D - default is zero", "[mesh]") {
    Position3D p;
    REQUIRE(approxEq(p.position.x, 0.0f));
    REQUIRE(approxEq(p.position.y, 0.0f));
    REQUIRE(approxEq(p.position.z, 0.0f));
}

TEST_CASE("Rotation3D - default quaternion is identity", "[mesh]") {
    Rotation3D r;
    REQUIRE(approxEq(r.quaternion.x, 0.0f));
    REQUIRE(approxEq(r.quaternion.y, 0.0f));
    REQUIRE(approxEq(r.quaternion.z, 0.0f));
    REQUIRE(approxEq(r.quaternion.w, 1.0f));
}

TEST_CASE("Rotation3D - quaternion w is 1.0", "[mesh]") {
    Rotation3D r;
    REQUIRE(approxEq(r.quaternion.w, 1.0f));
}

TEST_CASE("Scale3D - default is uniform one", "[mesh]") {
    Scale3D s;
    REQUIRE(approxEq(s.scale.x, 1.0f));
    REQUIRE(approxEq(s.scale.y, 1.0f));
    REQUIRE(approxEq(s.scale.z, 1.0f));
}

// ============================================================================
// MeshLoader::fromMemory
// ============================================================================

TEST_CASE("MeshLoader::fromMemory - simple triangle", "[mesh]") {
    Vertex3D verts[3] = {};
    verts[0].position = Vec3(0.0f, 0.0f, 0.0f);
    verts[1].position = Vec3(1.0f, 0.0f, 0.0f);
    verts[2].position = Vec3(0.0f, 1.0f, 0.0f);
    
    u32 indices[3] = {0, 1, 2};
    
    Mesh3D* mesh = MeshLoader::fromMemory(verts, 3, indices, 3);
    REQUIRE(mesh != nullptr);
    REQUIRE(mesh->vertices.size() == 3);
    REQUIRE(mesh->indices.size() == 3);
    REQUIRE(mesh->subMeshes.size() == 1);
    REQUIRE(mesh->subMeshes[0].indexCount == 3);
    delete mesh;
}

TEST_CASE("MeshLoader::fromMemory - multiple triangles", "[mesh]") {
    Vertex3D verts[4] = {};
    verts[0].position = Vec3(0.0f, 0.0f, 0.0f);
    verts[1].position = Vec3(1.0f, 0.0f, 0.0f);
    verts[2].position = Vec3(1.0f, 1.0f, 0.0f);
    verts[3].position = Vec3(0.0f, 1.0f, 0.0f);
    
    u32 indices[6] = {0, 1, 2, 0, 2, 3};
    
    Mesh3D* mesh = MeshLoader::fromMemory(verts, 4, indices, 6);
    REQUIRE(mesh != nullptr);
    REQUIRE(mesh->vertices.size() == 4);
    REQUIRE(mesh->indices.size() == 6);
    delete mesh;
}

// ============================================================================
// MeshLoader::parseOBJ
// ============================================================================

TEST_CASE("MeshLoader::parseOBJ - minimal OBJ parses without crash", "[mesh]") {
    Mesh3D* mesh = MeshLoader::parseOBJ(kTriangleOBJ, strlen(kTriangleOBJ));
    REQUIRE(mesh != nullptr);
    delete mesh;
}

TEST_CASE("MeshLoader::parseOBJ - correct vertex count for triangle", "[mesh]") {
    Mesh3D* mesh = MeshLoader::parseOBJ(kTriangleOBJ, strlen(kTriangleOBJ));
    REQUIRE(mesh != nullptr);
    REQUIRE(mesh->vertices.size() == 3);
    delete mesh;
}

TEST_CASE("MeshLoader::parseOBJ - returns nullptr on empty input", "[mesh]") {
    Mesh3D* mesh = MeshLoader::parseOBJ("", 0);
    REQUIRE(mesh == nullptr);
}

TEST_CASE("MeshLoader::parseOBJ - bounds computed correctly", "[mesh]") {
    Mesh3D* mesh = MeshLoader::parseOBJ(kTriangleOBJ, strlen(kTriangleOBJ));
    REQUIRE(mesh != nullptr);
    REQUIRE(approxEq(mesh->bounds.min.x, 0.0f));
    REQUIRE(approxEq(mesh->bounds.min.y, 0.0f));
    REQUIRE(approxEq(mesh->bounds.max.x, 1.0f));
    REQUIRE(approxEq(mesh->bounds.max.y, 1.0f));
    delete mesh;
}

TEST_CASE("MeshLoader::parseOBJ - quad becomes 2 triangles", "[mesh]") {
    Mesh3D* mesh = MeshLoader::parseOBJ(kQuadOBJ, strlen(kQuadOBJ));
    REQUIRE(mesh != nullptr);
    REQUIRE(mesh->vertices.size() == 6);
    delete mesh;
}

TEST_CASE("MeshLoader::parseOBJ - v//vn format", "[mesh]") {
    Mesh3D* mesh = MeshLoader::parseOBJ(kQuadOBJ, strlen(kQuadOBJ));
    REQUIRE(mesh != nullptr);
    REQUIRE(mesh->vertices.size() > 0);
    delete mesh;
}

TEST_CASE("MeshLoader::parseOBJ - only v and f format", "[mesh]") {
    Mesh3D* mesh = MeshLoader::parseOBJ(kSimpleOBJ, strlen(kSimpleOBJ));
    REQUIRE(mesh != nullptr);
    REQUIRE(mesh->vertices.size() == 3);
    delete mesh;
}

// ============================================================================
// MeshLoader::loadOBJ
// ============================================================================

TEST_CASE("MeshLoader::loadOBJ - returns nullptr for nonexistent file", "[mesh]") {
    MeshLoader loader;
    Mesh3D* mesh = loader.loadOBJ("nonexistent.obj");
    REQUIRE(mesh == nullptr);
}

// ============================================================================
// MeshSystem
// ============================================================================

TEST_CASE("MeshSystem::onUpdate - empty world doesn't crash", "[mesh]") {
    World world;
    MeshSystem sys;
    sys.onUpdate(world, 0.016f);
    REQUIRE(true);
}

TEST_CASE("MeshSystem::onUpdate - entity with Position3D+MeshRenderer doesn't crash", "[mesh]") {
    World world;
    MeshSystem sys;
    Entity e = world.create();
    world.add<Position3D>(e, Position3D{});
    world.add<MeshRenderer>(e, MeshRenderer{});
    sys.onUpdate(world, 0.016f);
    REQUIRE(true);
}
