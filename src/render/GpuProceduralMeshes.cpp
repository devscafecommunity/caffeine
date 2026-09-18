#include "render/GpuProceduralMeshes.hpp"

#include <cmath>

namespace Caffeine::Render {
namespace {

Assets::Vertex3D vtx(Vec3 pos, Vec3 normal, Vec2 uv) {
    return Assets::Vertex3D{pos, normal, uv, Vec4(1.0f, 0.0f, 0.0f, 1.0f)};
}

void addQuad(std::vector<Assets::Vertex3D>& vertices, std::vector<u32>& indices,
             const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, Vec3 normal) {
    const u32 base = static_cast<u32>(vertices.size());
    vertices.push_back(vtx(p0, normal, {0.0f, 0.0f}));
    vertices.push_back(vtx(p1, normal, {1.0f, 0.0f}));
    vertices.push_back(vtx(p2, normal, {1.0f, 1.0f}));
    vertices.push_back(vtx(p3, normal, {0.0f, 1.0f}));
    indices.push_back(base + 0);
    indices.push_back(base + 1);
    indices.push_back(base + 2);
    indices.push_back(base + 0);
    indices.push_back(base + 2);
    indices.push_back(base + 3);
}

void buildCube(Assets::Mesh3D& mesh) {
    mesh.vertices.clear();
    mesh.indices.clear();
    const f32 h = 0.5f;
    addQuad(mesh.vertices, mesh.indices, {-h, -h, -h}, {h, -h, -h}, {h, h, -h}, {-h, h, -h},
            {0, 0, -1});
    addQuad(mesh.vertices, mesh.indices, {h, -h, h}, {-h, -h, h}, {-h, h, h}, {h, h, h},
            {0, 0, 1});
    addQuad(mesh.vertices, mesh.indices, {-h, -h, h}, {-h, -h, -h}, {-h, h, -h}, {-h, h, h},
            {-1, 0, 0});
    addQuad(mesh.vertices, mesh.indices, {h, -h, -h}, {h, -h, h}, {h, h, h}, {h, h, -h},
            {1, 0, 0});
    addQuad(mesh.vertices, mesh.indices, {-h, h, -h}, {h, h, -h}, {h, h, h}, {-h, h, h},
            {0, 1, 0});
    addQuad(mesh.vertices, mesh.indices, {-h, -h, h}, {h, -h, h}, {h, -h, -h}, {-h, -h, -h},
            {0, -1, 0});
    mesh.bounds = {{-h, -h, -h}, {h, h, h}};
}

void buildPlane(Assets::Mesh3D& mesh) {
    mesh.vertices.clear();
    mesh.indices.clear();
    const f32 h = 0.5f;
    addQuad(mesh.vertices, mesh.indices, {-h, 0.0f, -h}, {h, 0.0f, -h}, {h, 0.0f, h}, {-h, 0.0f, h},
            {0, 1, 0});
    mesh.bounds = {{-h, 0.0f, -h}, {h, 0.0f, h}};
}

void buildSphere(Assets::Mesh3D& mesh, int segments = 24) {
    mesh.vertices.clear();
    mesh.indices.clear();
    for (int lat = 0; lat <= segments; ++lat) {
        const f32 theta = static_cast<f32>(lat) * 3.14159265f / static_cast<f32>(segments);
        for (int lon = 0; lon <= segments; ++lon) {
            const f32 phi = static_cast<f32>(lon) * 2.0f * 3.14159265f / static_cast<f32>(segments);
            const Vec3 n(std::sin(theta) * std::cos(phi), std::cos(theta),
                         std::sin(theta) * std::sin(phi));
            const Vec3 p = n * 0.5f;
            mesh.vertices.push_back(
                vtx(p, n, {static_cast<f32>(lon) / static_cast<f32>(segments),
                           static_cast<f32>(lat) / static_cast<f32>(segments)}));
        }
    }
    for (int lat = 0; lat < segments; ++lat) {
        for (int lon = 0; lon < segments; ++lon) {
            const u32 first = static_cast<u32>(lat * (segments + 1) + lon);
            const u32 second = first + static_cast<u32>(segments + 1);
            mesh.indices.push_back(first);
            mesh.indices.push_back(second);
            mesh.indices.push_back(first + 1);
            mesh.indices.push_back(second);
            mesh.indices.push_back(second + 1);
            mesh.indices.push_back(first + 1);
        }
    }
    mesh.bounds = {{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}};
}

void buildCylinder(Assets::Mesh3D& mesh, int segments = 24) {
    mesh.vertices.clear();
    mesh.indices.clear();
    const f32 radius = 0.5f;
    const f32 halfH = 0.5f;

    for (int i = 0; i < segments; ++i) {
        const f32 a0 = (2.0f * 3.14159265f * static_cast<f32>(i)) / static_cast<f32>(segments);
        const f32 a1 =
            (2.0f * 3.14159265f * static_cast<f32>((i + 1) % segments)) / static_cast<f32>(segments);
        const Vec3 n0(std::cos(a0), 0.0f, std::sin(a0));
        const Vec3 n1(std::cos(a1), 0.0f, std::sin(a1));
        const Vec3 t0(n0.x * radius, halfH, n0.z * radius);
        const Vec3 t1(n1.x * radius, halfH, n1.z * radius);
        const Vec3 b0(n0.x * radius, -halfH, n0.z * radius);
        const Vec3 b1(n1.x * radius, -halfH, n1.z * radius);

        addQuad(mesh.vertices, mesh.indices, b0, b1, t1, t0, n0);
    }

    const u32 topCenter = static_cast<u32>(mesh.vertices.size());
    mesh.vertices.push_back(vtx({0.0f, halfH, 0.0f}, {0, 1, 0}, {0.5f, 0.5f}));
    for (int i = 0; i < segments; ++i) {
        const f32 a0 = (2.0f * 3.14159265f * static_cast<f32>(i)) / static_cast<f32>(segments);
        const f32 a1 =
            (2.0f * 3.14159265f * static_cast<f32>((i + 1) % segments)) / static_cast<f32>(segments);
        const u32 v0 = static_cast<u32>(mesh.vertices.size());
        mesh.vertices.push_back(
            vtx({std::cos(a0) * radius, halfH, std::sin(a0) * radius}, {0, 1, 0}, {0.5f, 0.5f}));
        const u32 v1 = static_cast<u32>(mesh.vertices.size());
        mesh.vertices.push_back(
            vtx({std::cos(a1) * radius, halfH, std::sin(a1) * radius}, {0, 1, 0}, {0.5f, 0.5f}));
        mesh.indices.push_back(topCenter);
        mesh.indices.push_back(v0);
        mesh.indices.push_back(v1);
    }

    const u32 bottomCenter = static_cast<u32>(mesh.vertices.size());
    mesh.vertices.push_back(vtx({0.0f, -halfH, 0.0f}, {0, -1, 0}, {0.5f, 0.5f}));
    for (int i = 0; i < segments; ++i) {
        const f32 a0 = (2.0f * 3.14159265f * static_cast<f32>(i)) / static_cast<f32>(segments);
        const f32 a1 =
            (2.0f * 3.14159265f * static_cast<f32>((i + 1) % segments)) / static_cast<f32>(segments);
        const u32 v0 = static_cast<u32>(mesh.vertices.size());
        mesh.vertices.push_back(
            vtx({std::cos(a0) * radius, -halfH, std::sin(a0) * radius}, {0, -1, 0}, {0.5f, 0.5f}));
        const u32 v1 = static_cast<u32>(mesh.vertices.size());
        mesh.vertices.push_back(
            vtx({std::cos(a1) * radius, -halfH, std::sin(a1) * radius}, {0, -1, 0}, {0.5f, 0.5f}));
        mesh.indices.push_back(bottomCenter);
        mesh.indices.push_back(v1);
        mesh.indices.push_back(v0);
    }

    mesh.bounds = {{-radius, -halfH, -radius}, {radius, halfH, radius}};
}

Assets::Mesh3D g_cube;
Assets::Mesh3D g_plane;
Assets::Mesh3D g_sphere;
Assets::Mesh3D g_cylinder;
bool g_built = false;

}  // namespace

void GpuProceduralMeshes::ensureBuilt() {
    if (g_built) return;
    buildCube(g_cube);
    buildPlane(g_plane);
    buildSphere(g_sphere);
    buildCylinder(g_cylinder);
    g_built = true;
}

Assets::Mesh3D& GpuProceduralMeshes::cube() { return g_cube; }
Assets::Mesh3D& GpuProceduralMeshes::plane() { return g_plane; }
Assets::Mesh3D& GpuProceduralMeshes::sphere() { return g_sphere; }
Assets::Mesh3D& GpuProceduralMeshes::cylinder() { return g_cylinder; }

#ifdef CF_HAS_SDL3
void GpuProceduralMeshes::releaseGpuResources(RHI::RenderDevice* device) {
    if (!device) return;
    ensureBuilt();
    auto releaseMesh = [&](Assets::Mesh3D& mesh) {
        if (mesh.vertexBuffer) {
            device->destroyBuffer(mesh.vertexBuffer);
            mesh.vertexBuffer = nullptr;
        }
        if (mesh.indexBuffer) {
            device->destroyBuffer(mesh.indexBuffer);
            mesh.indexBuffer = nullptr;
        }
    };
    releaseMesh(g_cube);
    releaseMesh(g_plane);
    releaseMesh(g_sphere);
    releaseMesh(g_cylinder);
}
#endif

Assets::Mesh3D* GpuProceduralMeshes::get(ECS::MeshPrimitive primitive) {
    ensureBuilt();
    switch (primitive) {
        case ECS::MeshPrimitive::Cube: return &g_cube;
        case ECS::MeshPrimitive::Plane: return &g_plane;
        case ECS::MeshPrimitive::Sphere: return &g_sphere;
        case ECS::MeshPrimitive::Cylinder:
        case ECS::MeshPrimitive::Capsule: return &g_cylinder;
        case ECS::MeshPrimitive::Custom: return nullptr;
    }
    return nullptr;
}

}  // namespace Caffeine::Render
