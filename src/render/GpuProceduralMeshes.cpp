#include "render/GpuProceduralMeshes.hpp"

#include <cmath>
#include <utility>  // std::pair

namespace Caffeine::Render {
namespace {

Assets::Vertex3D vtx(Vec3 pos, Vec3 normal, Vec2 uv) {
    return Assets::Vertex3D{pos, normal, uv, Vec4(1.0f, 0.0f, 0.0f, 1.0f)};
}

void addQuad(std::vector<Assets::Vertex3D>& vertices, std::vector<u32>& indices,
             Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, Vec3 outward) {
    Vec3 n = (p1 - p0).cross(p2 - p0);
    if (n.dot(outward) < 0.0f) {
        std::swap(p1, p3);
    }
    Vec3 nn = outward;
    if (nn.lengthSquared() > 1e-8f) nn = nn.normalized();
    const u32 base = static_cast<u32>(vertices.size());
    vertices.push_back(vtx(p0, nn, {0.0f, 0.0f}));
    vertices.push_back(vtx(p1, nn, {1.0f, 0.0f}));
    vertices.push_back(vtx(p2, nn, {1.0f, 1.0f}));
    vertices.push_back(vtx(p3, nn, {0.0f, 1.0f}));
    indices.push_back(base + 0);
    indices.push_back(base + 1);
    indices.push_back(base + 2);
    indices.push_back(base + 0);
    indices.push_back(base + 2);
    indices.push_back(base + 3);
}

void emitTriangle(std::vector<Assets::Vertex3D>& vertices, std::vector<u32>& indices, u32 i0,
                  u32 i1, u32 i2) {
    const Vec3 a = vertices[i0].position;
    const Vec3 b = vertices[i1].position;
    const Vec3 c = vertices[i2].position;
    const Vec3 outward = a + b + c;
    Vec3 n = (b - a).cross(c - a);
    if (n.dot(outward) < 0.0f) std::swap(i1, i2);
    indices.push_back(i0);
    indices.push_back(i1);
    indices.push_back(i2);
}

void emitQuad(std::vector<Assets::Vertex3D>& vertices, std::vector<u32>& indices, u32 i0, u32 i1,
              u32 i2, u32 i3) {
    emitTriangle(vertices, indices, i0, i1, i2);
    emitTriangle(vertices, indices, i0, i2, i3);
}

Vec3 radialNormalXZ(f32 angle) { return Vec3(std::cos(angle), 0.0f, std::sin(angle)); }

Vec3 coneSideNormal(f32 angle, f32 radius, f32 height) {
    Vec3 n(std::cos(angle) * height, radius, std::sin(angle) * height);
    if (n.lengthSquared() > 1e-8f) n = n.normalized();
    return n;
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
            auto emit = [&](u32 i0, u32 i1, u32 i2) {
                const Vec3 a = mesh.vertices[i0].position;
                const Vec3 b = mesh.vertices[i1].position;
                const Vec3 c = mesh.vertices[i2].position;
                const Vec3 outward = a + b + c;
                Vec3 n = (b - a).cross(c - a);
                if (n.dot(outward) < 0.0f) std::swap(i1, i2);
                mesh.indices.push_back(i0);
                mesh.indices.push_back(i1);
                mesh.indices.push_back(i2);
            };
            emit(first, first + 1, second);
            emit(second, first + 1, second + 1);
        }
    }
    mesh.bounds = {{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}};
}

void buildCylinder(Assets::Mesh3D& mesh, int segments = 24) {
    mesh.vertices.clear();
    mesh.indices.clear();
    const f32 radius = 0.5f;
    const f32 halfH = 0.5f;

    const u32 sideBase = static_cast<u32>(mesh.vertices.size());
    for (int i = 0; i <= segments; ++i) {
        const f32 a = (2.0f * 3.14159265f * static_cast<f32>(i)) / static_cast<f32>(segments);
        const Vec3 n = radialNormalXZ(a);
        const f32 u = static_cast<f32>(i) / static_cast<f32>(segments);
        mesh.vertices.push_back(vtx({n.x * radius, halfH, n.z * radius}, n, {u, 1.0f}));
        mesh.vertices.push_back(vtx({n.x * radius, -halfH, n.z * radius}, n, {u, 0.0f}));
    }
    for (int i = 0; i < segments; ++i) {
        const u32 t0 = sideBase + static_cast<u32>(i * 2);
        const u32 t1 = sideBase + static_cast<u32>((i + 1) * 2);
        const u32 b0 = t0 + 1;
        const u32 b1 = t1 + 1;
        emitQuad(mesh.vertices, mesh.indices, t0, t1, b1, b0);
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
        auto emit = [&](u32 i0, u32 i1, u32 i2, Vec3 outward) {
            const Vec3 a = mesh.vertices[i0].position;
            const Vec3 b = mesh.vertices[i1].position;
            const Vec3 c = mesh.vertices[i2].position;
            Vec3 n = (b - a).cross(c - a);
            if (n.dot(outward) < 0.0f) std::swap(i1, i2);
            mesh.indices.push_back(i0);
            mesh.indices.push_back(i1);
            mesh.indices.push_back(i2);
        };
        emit(topCenter, v0, v1, {0.0f, 1.0f, 0.0f});
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
        auto emit = [&](u32 i0, u32 i1, u32 i2, Vec3 outward) {
            const Vec3 a = mesh.vertices[i0].position;
            const Vec3 b = mesh.vertices[i1].position;
            const Vec3 c = mesh.vertices[i2].position;
            Vec3 n = (b - a).cross(c - a);
            if (n.dot(outward) < 0.0f) std::swap(i1, i2);
            mesh.indices.push_back(i0);
            mesh.indices.push_back(i1);
            mesh.indices.push_back(i2);
        };
        emit(bottomCenter, v0, v1, {0.0f, -1.0f, 0.0f});
    }

    mesh.bounds = {{-radius, -halfH, -radius}, {radius, halfH, radius}};
}

void appendTriangle(std::vector<Assets::Vertex3D>& vertices, std::vector<u32>& indices, Vec3 p0,
                    Vec3 p1, Vec3 p2) {
    const u32 base = static_cast<u32>(vertices.size());
    Vec3 n = (p1 - p0).cross(p2 - p0);
    if (n.lengthSquared() > 1e-8f) n = n.normalized();
    vertices.push_back(vtx(p0, n, {0.0f, 0.0f}));
    vertices.push_back(vtx(p1, n, {1.0f, 0.0f}));
    vertices.push_back(vtx(p2, n, {0.5f, 1.0f}));
    indices.push_back(base + 0);
    indices.push_back(base + 1);
    indices.push_back(base + 2);
}

void buildCapsule(Assets::Mesh3D& mesh, int segments = 24) {
    mesh.vertices.clear();
    mesh.indices.clear();
    const f32 radius = 0.5f;
    const f32 cylHalf = 0.25f;
    const int latSegments = segments / 2;
    const int lonSegments = segments;
    constexpr f32 kPi = 3.14159265f;

    auto sphereNormal = [](f32 theta, f32 phi) {
        return Vec3(std::sin(theta) * std::cos(phi), std::cos(theta),
                    std::sin(theta) * std::sin(phi));
    };

    const u32 bottomBase = static_cast<u32>(mesh.vertices.size());
    for (int lat = 0; lat <= latSegments; ++lat) {
        const f32 theta =
            kPi - static_cast<f32>(lat) * (kPi * 0.5f) / static_cast<f32>(latSegments);
        for (int lon = 0; lon <= lonSegments; ++lon) {
            const f32 phi = (2.0f * kPi * static_cast<f32>(lon)) / static_cast<f32>(lonSegments);
            const Vec3 n = sphereNormal(theta, phi);
            const Vec3 p(n.x * radius, -cylHalf + n.y * radius, n.z * radius);
            mesh.vertices.push_back(vtx(p, n, {static_cast<f32>(lon) / static_cast<f32>(lonSegments),
                                               static_cast<f32>(lat) / static_cast<f32>(latSegments)}));
        }
    }
    for (int lat = 0; lat < latSegments; ++lat) {
        for (int lon = 0; lon < lonSegments; ++lon) {
            const u32 row = static_cast<u32>(lat * (lonSegments + 1));
            const u32 next = static_cast<u32>((lat + 1) * (lonSegments + 1));
            const u32 i0 = bottomBase + row + static_cast<u32>(lon);
            const u32 i1 = bottomBase + row + static_cast<u32>(lon + 1);
            const u32 i2 = bottomBase + next + static_cast<u32>(lon + 1);
            const u32 i3 = bottomBase + next + static_cast<u32>(lon);
            emitQuad(mesh.vertices, mesh.indices, i0, i1, i2, i3);
        }
    }

    const u32 topRingBase = static_cast<u32>(mesh.vertices.size());
    for (int lon = 0; lon <= lonSegments; ++lon) {
        const f32 phi = (2.0f * kPi * static_cast<f32>(lon)) / static_cast<f32>(lonSegments);
        const Vec3 n = radialNormalXZ(phi);
        mesh.vertices.push_back(vtx({n.x * radius, cylHalf, n.z * radius}, n,
                                    {static_cast<f32>(lon) / static_cast<f32>(lonSegments), 0.5f}));
    }

    const u32 bottomEquatorRow = bottomBase + static_cast<u32>(latSegments * (lonSegments + 1));
    for (int lon = 0; lon < lonSegments; ++lon) {
        const u32 b0 = bottomEquatorRow + static_cast<u32>(lon);
        const u32 b1 = bottomEquatorRow + static_cast<u32>(lon + 1);
        const u32 t0 = topRingBase + static_cast<u32>(lon);
        const u32 t1 = topRingBase + static_cast<u32>(lon + 1);
        emitQuad(mesh.vertices, mesh.indices, b0, b1, t1, t0);
    }

    const u32 topCapBase = static_cast<u32>(mesh.vertices.size());
    for (int lat = 1; lat <= latSegments; ++lat) {
        const f32 theta =
            kPi * 0.5f - static_cast<f32>(lat) * (kPi * 0.5f) / static_cast<f32>(latSegments);
        for (int lon = 0; lon <= lonSegments; ++lon) {
            const f32 phi = (2.0f * kPi * static_cast<f32>(lon)) / static_cast<f32>(lonSegments);
            const Vec3 n = sphereNormal(theta, phi);
            const Vec3 p(n.x * radius, cylHalf + n.y * radius, n.z * radius);
            mesh.vertices.push_back(vtx(p, n, {static_cast<f32>(lon) / static_cast<f32>(lonSegments),
                                               static_cast<f32>(lat) / static_cast<f32>(latSegments)}));
        }
    }
    for (int lat = 0; lat < latSegments; ++lat) {
        for (int lon = 0; lon < lonSegments; ++lon) {
            const u32 i0 = (lat == 0) ? (topRingBase + static_cast<u32>(lon))
                                      : (topCapBase + static_cast<u32>((lat - 1) * (lonSegments + 1) + lon));
            const u32 i1 = (lat == 0) ? (topRingBase + static_cast<u32>(lon + 1))
                                      : (topCapBase + static_cast<u32>((lat - 1) * (lonSegments + 1) + lon + 1));
            const u32 i2 =
                topCapBase + static_cast<u32>(lat * (lonSegments + 1) + lon + 1);
            const u32 i3 = topCapBase + static_cast<u32>(lat * (lonSegments + 1) + lon);
            emitQuad(mesh.vertices, mesh.indices, i0, i1, i2, i3);
        }
    }

    mesh.bounds = {{-radius, -cylHalf - radius, -radius}, {radius, cylHalf + radius, radius}};
}

void buildCone(Assets::Mesh3D& mesh, int segments = 24) {
    mesh.vertices.clear();
    mesh.indices.clear();
    const f32 radius = 0.5f;
    const f32 apexY = 0.5f;
    const f32 baseY = -0.5f;
    const f32 height = apexY - baseY;

    const u32 apexIdx = static_cast<u32>(mesh.vertices.size());
    mesh.vertices.push_back(vtx({0.0f, apexY, 0.0f}, Vec3(0.0f, 1.0f, 0.0f), {0.5f, 1.0f}));

    const u32 mantleRing = static_cast<u32>(mesh.vertices.size());
    for (int i = 0; i <= segments; ++i) {
        const f32 a = (2.0f * 3.14159265f * static_cast<f32>(i)) / static_cast<f32>(segments);
        const Vec3 n = coneSideNormal(a, radius, height);
        mesh.vertices.push_back(
            vtx({std::cos(a) * radius, baseY, std::sin(a) * radius}, n,
                {static_cast<f32>(i) / static_cast<f32>(segments), 0.0f}));
    }
    for (int i = 0; i < segments; ++i) {
        emitTriangle(mesh.vertices, mesh.indices, apexIdx, mantleRing + static_cast<u32>(i + 1),
                     mantleRing + static_cast<u32>(i));
    }

    const u32 baseCenter = static_cast<u32>(mesh.vertices.size());
    mesh.vertices.push_back(vtx({0.0f, baseY, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.5f, 0.5f}));
    const u32 capRing = static_cast<u32>(mesh.vertices.size());
    for (int i = 0; i <= segments; ++i) {
        const f32 a = (2.0f * 3.14159265f * static_cast<f32>(i)) / static_cast<f32>(segments);
        mesh.vertices.push_back(
            vtx({std::cos(a) * radius, baseY, std::sin(a) * radius}, {0.0f, -1.0f, 0.0f},
                {static_cast<f32>(i) / static_cast<f32>(segments), 0.0f}));
    }
    for (int i = 0; i < segments; ++i) {
        emitTriangle(mesh.vertices, mesh.indices, baseCenter, capRing + static_cast<u32>(i),
                     capRing + static_cast<u32>(i + 1));
    }

    mesh.bounds = {{-radius, baseY, -radius}, {radius, apexY, radius}};
}

void buildPyramid(Assets::Mesh3D& mesh) {
    mesh.vertices.clear();
    mesh.indices.clear();
    const f32 half = 0.5f;
    const f32 baseY = -0.5f;
    const f32 apexY = 0.5f;
    const Vec3 apex(0.0f, apexY, 0.0f);
    const Vec3 b0(-half, baseY, -half);
    const Vec3 b1(half, baseY, -half);
    const Vec3 b2(half, baseY, half);
    const Vec3 b3(-half, baseY, half);

    appendTriangle(mesh.vertices, mesh.indices, apex, b1, b0);
    appendTriangle(mesh.vertices, mesh.indices, apex, b2, b1);
    appendTriangle(mesh.vertices, mesh.indices, apex, b3, b2);
    appendTriangle(mesh.vertices, mesh.indices, apex, b0, b3);
    appendTriangle(mesh.vertices, mesh.indices, b0, b2, b1);
    appendTriangle(mesh.vertices, mesh.indices, b0, b3, b2);

    mesh.bounds = {{-half, baseY, -half}, {half, apexY, half}};
}

void buildTorus(Assets::Mesh3D& mesh, int segments = 24, int tubeSegments = 16) {
    mesh.vertices.clear();
    mesh.indices.clear();
    const f32 majorR = 0.35f;
    const f32 minorR = 0.15f;
    constexpr f32 kPi = 3.14159265f;

    auto torusFrame = [&](f32 u, f32 v) {
        const f32 cu = std::cos(u);
        const f32 su = std::sin(u);
        const f32 cv = std::cos(v);
        const f32 sv = std::sin(v);
        const Vec3 normal(cu * cv, sv, su * cv);
        const Vec3 center(majorR * cu, 0.0f, majorR * su);
        return std::pair<Vec3, Vec3>(center + normal * minorR, normal);
    };

    const u32 gridBase = static_cast<u32>(mesh.vertices.size());
    for (int i = 0; i <= segments; ++i) {
        const f32 u = (2.0f * kPi * static_cast<f32>(i)) / static_cast<f32>(segments);
        for (int j = 0; j <= tubeSegments; ++j) {
            const f32 v = (2.0f * kPi * static_cast<f32>(j)) / static_cast<f32>(tubeSegments);
            const auto [p, n] = torusFrame(u, v);
            mesh.vertices.push_back(vtx(p, n, {static_cast<f32>(i) / static_cast<f32>(segments),
                                               static_cast<f32>(j) / static_cast<f32>(tubeSegments)}));
        }
    }
    const int rowStride = tubeSegments + 1;
    for (int i = 0; i < segments; ++i) {
        for (int j = 0; j < tubeSegments; ++j) {
            const u32 row = static_cast<u32>(i * rowStride);
            const u32 next = static_cast<u32>((i + 1) * rowStride);
            const u32 i0 = gridBase + row + static_cast<u32>(j);
            const u32 i1 = gridBase + row + static_cast<u32>(j + 1);
            const u32 i2 = gridBase + next + static_cast<u32>(j + 1);
            const u32 i3 = gridBase + next + static_cast<u32>(j);
            emitQuad(mesh.vertices, mesh.indices, i0, i1, i2, i3);
        }
    }

    const f32 extent = majorR + minorR;
    mesh.bounds = {{-extent, -minorR, -extent}, {extent, minorR, extent}};
}

Assets::Mesh3D g_cube;
Assets::Mesh3D g_plane;
Assets::Mesh3D g_sphere;
Assets::Mesh3D g_cylinder;
Assets::Mesh3D g_capsule;
Assets::Mesh3D g_cone;
Assets::Mesh3D g_pyramid;
Assets::Mesh3D g_torus;
bool g_built = false;

}  // namespace

void GpuProceduralMeshes::ensureBuilt() {
    if (g_built) return;
    auto resetMesh = [](Assets::Mesh3D& mesh) {
        mesh.vertexBuffer = nullptr;
        mesh.indexBuffer = nullptr;
    };
    resetMesh(g_cube);
    resetMesh(g_plane);
    resetMesh(g_sphere);
    resetMesh(g_cylinder);
    resetMesh(g_capsule);
    resetMesh(g_cone);
    resetMesh(g_pyramid);
    resetMesh(g_torus);

    buildCube(g_cube);
    buildPlane(g_plane);
    buildSphere(g_sphere);
    buildCylinder(g_cylinder);
    buildCapsule(g_capsule);
    buildCone(g_cone);
    buildPyramid(g_pyramid);
    buildTorus(g_torus);
    g_built = true;
}

Assets::Mesh3D& GpuProceduralMeshes::cube() { return g_cube; }
Assets::Mesh3D& GpuProceduralMeshes::plane() { return g_plane; }
Assets::Mesh3D& GpuProceduralMeshes::sphere() { return g_sphere; }
Assets::Mesh3D& GpuProceduralMeshes::cylinder() { return g_cylinder; }
Assets::Mesh3D& GpuProceduralMeshes::capsule() { return g_capsule; }
Assets::Mesh3D& GpuProceduralMeshes::cone() { return g_cone; }
Assets::Mesh3D& GpuProceduralMeshes::pyramid() { return g_pyramid; }
Assets::Mesh3D& GpuProceduralMeshes::torus() { return g_torus; }

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
    releaseMesh(g_capsule);
    releaseMesh(g_cone);
    releaseMesh(g_pyramid);
    releaseMesh(g_torus);
}
#endif

Assets::Mesh3D* GpuProceduralMeshes::get(ECS::MeshPrimitive primitive) {
    ensureBuilt();
    switch (primitive) {
        case ECS::MeshPrimitive::Cube: return &g_cube;
        case ECS::MeshPrimitive::Plane: return &g_plane;
        case ECS::MeshPrimitive::Sphere: return &g_sphere;
        case ECS::MeshPrimitive::Cylinder: return &g_cylinder;
        case ECS::MeshPrimitive::Capsule: return &g_capsule;
        case ECS::MeshPrimitive::Cone: return &g_cone;
        case ECS::MeshPrimitive::Pyramid: return &g_pyramid;
        case ECS::MeshPrimitive::Torus: return &g_torus;
        case ECS::MeshPrimitive::Custom: return nullptr;
    }
    return nullptr;
}

}  // namespace Caffeine::Render
