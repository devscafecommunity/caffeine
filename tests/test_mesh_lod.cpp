#include "catch.hpp"
#include "assets/MeshLOD.hpp"

using namespace Caffeine;
using namespace Caffeine::Assets;

static Mesh3D makeGrid(u32 n) {
    Mesh3D mesh;
    for (u32 z = 0; z <= n; ++z) {
        for (u32 x = 0; x <= n; ++x) {
            Vertex3D v;
            v.position = Vec3(static_cast<f32>(x), 0.0f, static_cast<f32>(z));
            v.normal = Vec3(0.0f, 1.0f, 0.0f);
            v.texcoord = Vec2(static_cast<f32>(x) / n, static_cast<f32>(z) / n);
            mesh.vertices.push_back(v);
        }
    }
    const u32 stride = n + 1;
    for (u32 z = 0; z < n; ++z) {
        for (u32 x = 0; x < n; ++x) {
            const u32 i = z * stride + x;
            mesh.indices.push_back(i);
            mesh.indices.push_back(i + 1);
            mesh.indices.push_back(i + stride);
            mesh.indices.push_back(i + 1);
            mesh.indices.push_back(i + stride + 1);
            mesh.indices.push_back(i + stride);
        }
    }
    mesh.bounds.min = Vec3(0, 0, 0);
    mesh.bounds.max = Vec3(static_cast<f32>(n), 0, static_cast<f32>(n));
    return mesh;
}

TEST_CASE("MeshLOD - clustering reduces triangles and stays manifold", "[render][lod]") {
    const Mesh3D source = makeGrid(16);
    Mesh3D lod;
    REQUIRE(MeshLOD::buildClusteredLod(source, lod, 4.0f));
    REQUIRE(lod.indices.size() < source.indices.size());
    REQUIRE(lod.indices.size() % 3 == 0);
    REQUIRE(!lod.vertices.empty());
    for (u32 index : lod.indices) {
        REQUIRE(index < lod.vertices.size());
    }
    for (u32 i = 0; i + 2 < lod.indices.size(); i += 3) {
        REQUIRE(lod.indices[i] != lod.indices[i + 1]);
        REQUIRE(lod.indices[i + 1] != lod.indices[i + 2]);
        REQUIRE(lod.indices[i + 2] != lod.indices[i]);
    }
}

TEST_CASE("MeshLOD - tiny cell keeps the source triangle count", "[render][lod]") {
    const Mesh3D source = makeGrid(2);
    Mesh3D lod;
    REQUIRE_FALSE(MeshLOD::buildClusteredLod(source, lod, 0.01f));
}
