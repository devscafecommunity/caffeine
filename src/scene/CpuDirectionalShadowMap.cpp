#include "scene/CpuDirectionalShadowMap.hpp"

#include "scene/LightingSystem.hpp"
#include "assets/MeshCache.hpp"
#include "ecs/ComponentQuery.hpp"
#include "ecs/MeshComponents.hpp"
#include "math/Mat4.hpp"
#include "math/Quat.hpp"
#include "scene/HierarchySystem.hpp"
#include "scene/SceneComponents.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>

namespace Caffeine::Scene {

namespace {

constexpr f32 kDegToRad = 3.14159265f / 180.0f;
constexpr f32 kShadowLit = 1.0f;
constexpr f32 kShadowDark = 0.28f;

Mat4 buildLocalMatrix(const ECS::Transform& t) {
    return Mat4::translation(t.position) * Mat4::rotationZ(t.rotation.z * kDegToRad) *
           Mat4::rotationY(t.rotation.y * kDegToRad) * Mat4::rotationX(t.rotation.x * kDegToRad) *
           Mat4::scale(t.scale.x, t.scale.y, t.scale.z);
}

Mat4 buildLocalMatrix3D(const ECS::Position3D* p, const ECS::Rotation3D* r, const ECS::Scale3D* s) {
    Mat4 T = p ? Mat4::translation(p->position) : Mat4::identity();
    Mat4 R = r ? Quat(r->quaternion.x, r->quaternion.y, r->quaternion.z, r->quaternion.w)
                     .normalized()
                     .toMatrix()
               : Mat4::identity();
    Mat4 S = s ? Mat4::scale(s->scale.x, s->scale.y, s->scale.z) : Mat4::identity();
    return T * R * S;
}

Mat4 entityMatrix(ECS::World& world, ECS::Entity entity) {
    if (auto* wt = world.get<WorldTransform>(entity)) return wt->matrix;
    if (auto* t = world.get<ECS::Transform>(entity)) return buildLocalMatrix(*t);
    auto* p3 = world.get<ECS::Position3D>(entity);
    auto* r3 = world.get<ECS::Rotation3D>(entity);
    auto* s3 = world.get<ECS::Scale3D>(entity);
    return buildLocalMatrix3D(p3, r3, s3);
}

Vec3 entityForward(ECS::World& world, ECS::Entity entity) {
    const Mat4 m = entityMatrix(world, entity);
    Vec3 axis(m(0, 2), m(1, 2), m(2, 2));
    const f32 lenSq = axis.lengthSquared();
    if (lenSq > 1e-6f) axis = axis / std::sqrt(lenSq);
    return -1.0f * axis;
}

bool tryGetEntityPosition(ECS::World& world, ECS::Entity entity, Vec3& outPosition) {
    if (auto* wt = world.get<WorldTransform>(entity)) {
        outPosition = wt->matrix.transformPoint(Vec3(0.0f, 0.0f, 0.0f));
        return true;
    }
    if (auto* t = world.get<ECS::Transform>(entity)) {
        outPosition = t->position;
        return true;
    }
    if (auto* p3 = world.get<ECS::Position3D>(entity)) {
        outPosition = p3->position;
        return true;
    }
    return false;
}

f32 edgeFunction(f32 ax, f32 ay, f32 bx, f32 by, f32 cx, f32 cy) {
    return (cx - ax) * (by - ay) - (cy - ay) * (bx - ax);
}

struct ShadowVert {
    f32 sx = 0.0f;
    f32 sy = 0.0f;
    f32 depth = 1.1f;
};

ShadowVert projectShadowVert(const Mat4& lightVP, int resolution, const Vec3& worldPos) {
    ShadowVert out;
    const Vec4 clip = lightVP.transformVec4(Vec4(worldPos.x, worldPos.y, worldPos.z, 1.0f));
    if (clip.w <= 1e-6f) return out;
    const f32 invW = 1.0f / clip.w;
    const f32 ndcX = clip.x * invW;
    const f32 ndcY = clip.y * invW;
    const f32 ndcZ = clip.z * invW;
    if (ndcX < -1.05f || ndcX > 1.05f || ndcY < -1.05f || ndcY > 1.05f) return out;
    out.sx = (ndcX + 1.0f) * 0.5f * static_cast<f32>(resolution);
    out.sy = (1.0f - ndcY) * 0.5f * static_cast<f32>(resolution);
    out.depth = ndcZ;
    return out;
}

void rasterizeShadowTriangle(const Mat4& lightVP, int resolution, std::vector<f32>& depth,
                             const Vec3& p0, const Vec3& p1, const Vec3& p2) {
    const ShadowVert v0 = projectShadowVert(lightVP, resolution, p0);
    const ShadowVert v1 = projectShadowVert(lightVP, resolution, p1);
    const ShadowVert v2 = projectShadowVert(lightVP, resolution, p2);

    const f32 area = edgeFunction(v0.sx, v0.sy, v1.sx, v1.sy, v2.sx, v2.sy);
    if (std::abs(area) < 1e-4f) return;

    const int minX =
        std::max(0, static_cast<int>(std::floor(std::min({v0.sx, v1.sx, v2.sx}))));
    const int maxX =
        std::min(resolution - 1, static_cast<int>(std::ceil(std::max({v0.sx, v1.sx, v2.sx}))));
    const int minY =
        std::max(0, static_cast<int>(std::floor(std::min({v0.sy, v1.sy, v2.sy}))));
    const int maxY =
        std::min(resolution - 1, static_cast<int>(std::ceil(std::max({v0.sy, v1.sy, v2.sy}))));

    for (int y = minY; y <= maxY; ++y) {
        const f32 py = static_cast<f32>(y) + 0.5f;
        for (int x = minX; x <= maxX; ++x) {
            const f32 px = static_cast<f32>(x) + 0.5f;
            const f32 w0 = edgeFunction(v1.sx, v1.sy, v2.sx, v2.sy, px, py) / area;
            const f32 w1 = edgeFunction(v2.sx, v2.sy, v0.sx, v0.sy, px, py) / area;
            const f32 w2 = edgeFunction(v0.sx, v0.sy, v1.sx, v1.sy, px, py) / area;
            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;

            const f32 z = w0 * v0.depth + w1 * v1.depth + w2 * v2.depth;
            const int idx = y * resolution + x;
            if (z < depth[idx]) {
                depth[idx] = z;
            }
        }
    }
}

void rasterizeMeshTriangles(const Mat4& lightVP, int resolution, std::vector<f32>& depth,
                            const Assets::Mesh3D& mesh, const Mat4& worldMatrix) {
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const u32 i0 = mesh.indices[i];
        const u32 i1 = mesh.indices[i + 1];
        const u32 i2 = mesh.indices[i + 2];
        if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size()) {
            continue;
        }
        const Vec3 p0 = worldMatrix.transformPoint(mesh.vertices[i0].position);
        const Vec3 p1 = worldMatrix.transformPoint(mesh.vertices[i1].position);
        const Vec3 p2 = worldMatrix.transformPoint(mesh.vertices[i2].position);
        rasterizeShadowTriangle(lightVP, resolution, depth, p0, p1, p2);
    }
}

void rasterizeCube(const Mat4& lightVP, int resolution, std::vector<f32>& depth,
                   const Mat4& worldMatrix) {
    const std::array<Vec3, 8> corners = {{
        {-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f},
        {-0.5f, -0.5f, 0.5f},  {0.5f, -0.5f, 0.5f},  {0.5f, 0.5f, 0.5f},  {-0.5f, 0.5f, 0.5f},
    }};
    const int faces[][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}, {0, 3, 7, 4}, {1, 2, 6, 5},
                            {3, 2, 6, 7}, {0, 1, 5, 4}};
    std::array<Vec3, 8> wp;
    for (size_t i = 0; i < corners.size(); ++i) {
        wp[i] = worldMatrix.transformPoint(corners[i]);
    }
    for (const auto& face : faces) {
        rasterizeShadowTriangle(lightVP, resolution, depth, wp[face[0]], wp[face[1]], wp[face[2]]);
        rasterizeShadowTriangle(lightVP, resolution, depth, wp[face[0]], wp[face[2]], wp[face[3]]);
    }
}

void rasterizePlane(const Mat4& lightVP, int resolution, std::vector<f32>& depth,
                    const Mat4& worldMatrix) {
    const Vec3 corners[4] = {{-0.5f, 0.0f, -0.5f}, {0.5f, 0.0f, -0.5f}, {0.5f, 0.0f, 0.5f},
                             {-0.5f, 0.0f, 0.5f}};
    Vec3 wp[4];
    for (int i = 0; i < 4; ++i) wp[i] = worldMatrix.transformPoint(corners[i]);
    rasterizeShadowTriangle(lightVP, resolution, depth, wp[0], wp[1], wp[2]);
    rasterizeShadowTriangle(lightVP, resolution, depth, wp[0], wp[2], wp[3]);
}

void rasterizeSphere(const Mat4& lightVP, int resolution, std::vector<f32>& depth,
                     const Mat4& worldMatrix) {
    constexpr int segments = 16;
    constexpr int rings = 12;
    std::vector<Vec3> verts;
    verts.reserve(static_cast<size_t>((rings + 1) * (segments + 1)));
    for (int lat = 0; lat <= rings; ++lat) {
        const f32 theta = static_cast<f32>(lat) * 3.14159265f / static_cast<f32>(rings);
        const f32 sinT = std::sin(theta);
        const f32 cosT = std::cos(theta);
        for (int lon = 0; lon <= segments; ++lon) {
            const f32 phi = static_cast<f32>(lon) * 2.0f * 3.14159265f / static_cast<f32>(segments);
            const Vec3 local(0.5f * sinT * std::cos(phi), 0.5f * cosT, 0.5f * sinT * std::sin(phi));
            verts.push_back(worldMatrix.transformPoint(local));
        }
    }
    for (int lat = 0; lat < rings; ++lat) {
        for (int lon = 0; lon < segments; ++lon) {
            const int i0 = lat * (segments + 1) + lon;
            const int i1 = i0 + 1;
            const int i2 = i0 + segments + 1;
            const int i3 = i2 + 1;
            rasterizeShadowTriangle(lightVP, resolution, depth, verts[i0], verts[i2], verts[i1]);
            rasterizeShadowTriangle(lightVP, resolution, depth, verts[i1], verts[i2], verts[i3]);
        }
    }
}

void rasterizeCylinder(const Mat4& lightVP, int resolution, std::vector<f32>& depth,
                       const Mat4& worldMatrix) {
    constexpr int segments = 16;
    std::vector<Vec3> top;
    std::vector<Vec3> bottom;
    top.reserve(segments);
    bottom.reserve(segments);
    for (int i = 0; i < segments; ++i) {
        const f32 a = static_cast<f32>(i) * 2.0f * 3.14159265f / static_cast<f32>(segments);
        const f32 x = 0.5f * std::cos(a);
        const f32 z = 0.5f * std::sin(a);
        top.push_back(worldMatrix.transformPoint(Vec3(x, 0.5f, z)));
        bottom.push_back(worldMatrix.transformPoint(Vec3(x, -0.5f, z)));
    }
    const Vec3 topCenter = worldMatrix.transformPoint(Vec3(0.0f, 0.5f, 0.0f));
    const Vec3 bottomCenter = worldMatrix.transformPoint(Vec3(0.0f, -0.5f, 0.0f));
    for (int i = 0; i < segments; ++i) {
        const int next = (i + 1) % segments;
        rasterizeShadowTriangle(lightVP, resolution, depth, top[i], bottom[i], top[next]);
        rasterizeShadowTriangle(lightVP, resolution, depth, top[next], bottom[i], bottom[next]);
        rasterizeShadowTriangle(lightVP, resolution, depth, topCenter, top[i], top[next]);
        rasterizeShadowTriangle(lightVP, resolution, depth, bottomCenter, bottom[next], bottom[i]);
    }
}

f32 compareShadowDepth(int resolution, const std::vector<f32>& depth, f32 u, f32 v, f32 ndcZ,
                       f32 bias) {
    const int x = std::clamp(static_cast<int>(u * static_cast<f32>(resolution - 1)), 0, resolution - 1);
    const int y = std::clamp(static_cast<int>(v * static_cast<f32>(resolution - 1)), 0, resolution - 1);
    const f32 stored = depth[static_cast<size_t>(y * resolution + x)];
    return (ndcZ - bias > stored) ? kShadowDark : kShadowLit;
}

f32 sampleDepthPcf(const Mat4& lightVP, int resolution, const std::vector<f32>& depth,
                   const Vec3& worldPos, f32 bias, int kernelRadius) {
    if (depth.empty()) return kShadowLit;

    const Vec4 clip = lightVP.transformVec4(Vec4(worldPos.x, worldPos.y, worldPos.z, 1.0f));
    if (clip.w <= 1e-6f) return kShadowLit;
    const f32 invW = 1.0f / clip.w;
    const f32 ndcX = clip.x * invW;
    const f32 ndcY = clip.y * invW;
    const f32 ndcZ = clip.z * invW;
    if (ndcX < -1.0f || ndcX > 1.0f || ndcY < -1.0f || ndcY > 1.0f || ndcZ < -1.0f ||
        ndcZ > 1.0f) {
        return kShadowLit;
    }

    const f32 baseU = (ndcX + 1.0f) * 0.5f;
    const f32 baseV = (1.0f - ndcY) * 0.5f;
    const f32 texel = 1.0f / static_cast<f32>(resolution);

    f32 accum = 0.0f;
    int count = 0;
    for (int oy = -kernelRadius; oy <= kernelRadius; ++oy) {
        for (int ox = -kernelRadius; ox <= kernelRadius; ++ox) {
            accum += compareShadowDepth(resolution, depth, baseU + static_cast<f32>(ox) * texel,
                                        baseV + static_cast<f32>(oy) * texel, ndcZ, bias);
            count++;
        }
    }
    return accum / static_cast<f32>(count);
}

}  // namespace

void CpuDirectionalShadowMap::reset() {
    valid = false;
    depth.clear();
}

void CpuDirectionalShadowMap::setup(const Vec3& lightDirection, const Vec3& focus,
                                    f32 shadowDistance, int res) {
    resolution = std::max(64, res);
    depth.assign(static_cast<size_t>(resolution) * static_cast<size_t>(resolution), 1.1f);

    Vec3 lightDir = lightDirection;
    const f32 len = lightDir.length();
    if (len < 1e-6f) {
        valid = false;
        return;
    }
    lightDir = lightDir / len;

    const f32 extent = std::max(5.0f, shadowDistance);
    const Vec3 eye = focus - lightDir * extent;
    Vec3 up(0.0f, 1.0f, 0.0f);
    if (std::abs(lightDir.dot(up)) > 0.95f) {
        up = Vec3(0.0f, 0.0f, 1.0f);
    }

    const Mat4 view = Mat4::lookAt(eye, focus, up);
    const Mat4 proj = Mat4::ortho(-extent, extent, -extent, extent, 0.1f, extent * 4.0f);
    lightVP = proj * view;
    valid = true;
}

f32 CpuDirectionalShadowMap::sample(const Vec3& worldPos, f32 bias) const {
    return samplePcf(worldPos, bias, 0);
}

f32 CpuDirectionalShadowMap::samplePcf(const Vec3& worldPos, f32 bias, int kernelRadius) const {
    if (!valid) return kShadowLit;
    return sampleDepthPcf(lightVP, resolution, depth, worldPos, bias, kernelRadius);
}

void CpuSpotShadowMap::reset() {
    valid = false;
    depth.clear();
}

void CpuSpotShadowMap::setup(const Vec3& position, const Vec3& direction, f32 radius,
                             f32 angleDegrees, int res) {
    resolution = std::max(64, res);
    depth.assign(static_cast<size_t>(resolution) * static_cast<size_t>(resolution), 1.1f);

    Vec3 lightDir = direction;
    const f32 len = lightDir.length();
    if (len < 1e-6f || radius <= 0.01f || angleDegrees <= 1.0f) {
        valid = false;
        return;
    }
    lightDir = lightDir / len;

    const Vec3 target = position + lightDir * radius;
    Vec3 up(0.0f, 1.0f, 0.0f);
    if (std::abs(lightDir.dot(up)) > 0.95f) {
        up = Vec3(0.0f, 0.0f, 1.0f);
    }

    const Mat4 view = Mat4::lookAt(position, target, up);
    const f32 fovY = std::max(0.05f, angleDegrees * kDegToRad);
    const Mat4 proj = Mat4::perspective(fovY, 1.0f, 0.1f, std::max(0.2f, radius));
    lightVP = proj * view;
    valid = true;
}

f32 CpuSpotShadowMap::sample(const Vec3& worldPos, f32 bias) const {
    return samplePcf(worldPos, bias, 0);
}

f32 CpuSpotShadowMap::samplePcf(const Vec3& worldPos, f32 bias, int kernelRadius) const {
    if (!valid) return kShadowLit;
    return sampleDepthPcf(lightVP, resolution, depth, worldPos, bias, kernelRadius);
}

bool meshCastsShadows(ECS::World& world, ECS::Entity entity) {
    if (auto* mr = world.get<ECS::MeshRendererComponent>(entity)) return mr->castShadows;
    return true;
}

bool meshReceivesShadows(ECS::World& world, ECS::Entity entity) {
    if (auto* mr = world.get<ECS::MeshRendererComponent>(entity)) return mr->receiveShadows;
    return true;
}

void rasterizeSceneShadowCasters(ECS::World& world, const Mat4& lightVP, int resolution,
                                 std::vector<f32>& depth, const std::string& projectRoot,
                                 ECS::Entity skipEntity) {
    auto& meshCache = Assets::MeshCache::getInstance();
    ECS::ComponentQuery meshQ;
    meshQ.with<ECS::MeshFilterComponent>();
    world.forEach<ECS::MeshFilterComponent>(
        meshQ, [&](ECS::Entity entity, ECS::MeshFilterComponent& meshFilter) {
            if (entity == skipEntity) return;
            if (isEffectivelyDisabled(world, entity)) return;
            if (!meshCastsShadows(world, entity)) return;

            const Mat4 worldMatrix = entityMatrix(world, entity);
            switch (meshFilter.primitive) {
                case ECS::MeshPrimitive::Cube:
                    rasterizeCube(lightVP, resolution, depth, worldMatrix);
                    break;
                case ECS::MeshPrimitive::Plane:
                    rasterizePlane(lightVP, resolution, depth, worldMatrix);
                    break;
                case ECS::MeshPrimitive::Sphere:
                    rasterizeSphere(lightVP, resolution, depth, worldMatrix);
                    break;
                case ECS::MeshPrimitive::Cylinder:
                case ECS::MeshPrimitive::Capsule:
                    rasterizeCylinder(lightVP, resolution, depth, worldMatrix);
                    break;
                case ECS::MeshPrimitive::Custom:
                    if (meshFilter.customMeshPath.empty()) return;
                    if (Assets::Mesh3D* loadedMesh =
                            meshCache.getMesh(meshFilter.customMeshPath, projectRoot)) {
                        if (!loadedMesh->vertices.empty() && !loadedMesh->indices.empty()) {
                            rasterizeMeshTriangles(lightVP, resolution, depth, *loadedMesh,
                                                   worldMatrix);
                        }
                    }
                    break;
            }
        });
}

void buildDirectionalShadowMap(ECS::World& world, CpuDirectionalShadowMap& out,
                               const DirectionalLightData& light, const Vec3& focus,
                               const std::string& projectRoot, ECS::Entity skipEntity) {
    out.reset();
    if (!light.castShadows) return;

    out.setup(light.direction, focus, light.shadowDistance);
    rasterizeSceneShadowCasters(world, out.lightVP, out.resolution, out.depth, projectRoot,
                                skipEntity);
}

void buildSpotShadowMap(ECS::World& world, CpuSpotShadowMap& out, const SpotLightData& light,
                        const std::string& projectRoot, ECS::Entity skipEntity) {
    out.reset();
    if (!light.castShadows) return;

    out.setup(light.position, light.direction, light.radius, light.angle);
    rasterizeSceneShadowCasters(world, out.lightVP, out.resolution, out.depth, projectRoot,
                                skipEntity);
}

}  // namespace Caffeine::Scene
