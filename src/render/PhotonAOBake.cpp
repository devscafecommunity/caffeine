#include "caffeine/photon/PhotonBake.hpp"
#include "render/PhotonGpuBake.hpp"

#include "assets/MaterialCache.hpp"
#include "assets/MaterialFile.hpp"
#include "assets/MeshCache.hpp"
#include "ecs/ComponentQuery.hpp"
#include "ecs/MeshComponents.hpp"
#include "render/GpuProceduralMeshes.hpp"
#include "scene/HierarchySystem.hpp"
#include "scene/SceneComponents.hpp"

#include <stb/stb_image.h>
#include <stb/stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <vector>

namespace Caffeine::Photon {
namespace {

namespace fs = std::filesystem;

struct Tri {
    Vec3 a;
    Vec3 b;
    Vec3 c;
};

struct BvhNode {
    Vec3 bmin{1e30f, 1e30f, 1e30f};
    Vec3 bmax{-1e30f, -1e30f, -1e30f};
    int left = -1;
    int right = -1;
    u32 first = 0;
    u32 count = 0;
    bool leaf() const { return left < 0 && right < 0; }
};

f32 component(const Vec3& v, int axis) {
    return axis == 0 ? v.x : axis == 1 ? v.y : v.z;
}

Vec3 centroid(const Tri& t) {
    return (t.a + t.b + t.c) * (1.0f / 3.0f);
}

float hash01(u32 x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return static_cast<float>(x) * (1.0f / 4294967295.0f);
}

bool rayAabb(const Vec3& origin, const Vec3& dir, const Vec3& bmin, const Vec3& bmax,
             f32 maxT) {
    f32 tmin = 0.0f;
    f32 tmax = maxT;
    for (int axis = 0; axis < 3; ++axis) {
        const f32 o = component(origin, axis);
        const f32 d = component(dir, axis);
        const f32 mn = component(bmin, axis);
        const f32 mx = component(bmax, axis);
        if (std::abs(d) < 1e-8f) {
            if (o < mn || o > mx) return false;
            continue;
        }
        f32 t1 = (mn - o) / d;
        f32 t2 = (mx - o) / d;
        if (t1 > t2) std::swap(t1, t2);
        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);
        if (tmin > tmax) return false;
    }
    return tmax >= 0.0f;
}

bool rayTriangle(const Vec3& origin, const Vec3& dir, const Tri& tri, f32 maxT) {
    const Vec3 e1 = tri.b - tri.a;
    const Vec3 e2 = tri.c - tri.a;
    const Vec3 p = dir.cross(e2);
    const f32 det = e1.dot(p);
    if (std::abs(det) < 1e-8f) return false;
    const f32 inv = 1.0f / det;
    const Vec3 tvec = origin - tri.a;
    const f32 u = tvec.dot(p) * inv;
    if (u < 0.0f || u > 1.0f) return false;
    const Vec3 q = tvec.cross(e1);
    const f32 v = dir.dot(q) * inv;
    if (v < 0.0f || u + v > 1.0f) return false;
    const f32 t = e2.dot(q) * inv;
    return t > 1e-5f && t < maxT;
}

class SceneBvh {
public:
    void build(std::vector<Tri> triangles) {
        m_tris = std::move(triangles);
        m_order.resize(m_tris.size());
        for (u32 i = 0; i < m_order.size(); ++i) m_order[i] = i;
        m_nodes.clear();
        if (!m_tris.empty()) buildNode(0, static_cast<u32>(m_tris.size()), 0);
    }

    bool anyHit(const Vec3& origin, const Vec3& dir, f32 maxT) const {
        return !m_nodes.empty() && anyHitNode(0, origin, dir, maxT);
    }

    u32 triangleCount() const { return static_cast<u32>(m_tris.size()); }

private:
    int buildNode(u32 first, u32 count, int depth) {
        BvhNode node;
        node.first = first;
        node.count = count;
        for (u32 i = first; i < first + count; ++i) {
            const Tri& tri = m_tris[m_order[i]];
            node.bmin.x = std::min({node.bmin.x, tri.a.x, tri.b.x, tri.c.x});
            node.bmin.y = std::min({node.bmin.y, tri.a.y, tri.b.y, tri.c.y});
            node.bmin.z = std::min({node.bmin.z, tri.a.z, tri.b.z, tri.c.z});
            node.bmax.x = std::max({node.bmax.x, tri.a.x, tri.b.x, tri.c.x});
            node.bmax.y = std::max({node.bmax.y, tri.a.y, tri.b.y, tri.c.y});
            node.bmax.z = std::max({node.bmax.z, tri.a.z, tri.b.z, tri.c.z});
        }

        const int index = static_cast<int>(m_nodes.size());
        m_nodes.push_back(node);
        if (count <= 4 || depth >= 24) return index;

        const Vec3 size = node.bmax - node.bmin;
        const int axis = (size.x > size.y && size.x > size.z) ? 0 : (size.y > size.z ? 1 : 2);
        const u32 mid = first + count / 2;
        std::nth_element(m_order.begin() + first, m_order.begin() + mid,
                         m_order.begin() + first + count, [&](u32 a, u32 b) {
                             return component(centroid(m_tris[a]), axis) <
                                    component(centroid(m_tris[b]), axis);
                         });
        const int left = buildNode(first, mid - first, depth + 1);
        const int right = buildNode(mid, first + count - mid, depth + 1);
        m_nodes[index].left = left;
        m_nodes[index].right = right;
        return index;
    }

    bool anyHitNode(int index, const Vec3& origin, const Vec3& dir, f32 maxT) const {
        const BvhNode& node = m_nodes[index];
        if (!rayAabb(origin, dir, node.bmin, node.bmax, maxT)) return false;
        if (node.leaf()) {
            for (u32 i = node.first; i < node.first + node.count; ++i) {
                if (rayTriangle(origin, dir, m_tris[m_order[i]], maxT)) return true;
            }
            return false;
        }
        return anyHitNode(node.left, origin, dir, maxT) ||
               anyHitNode(node.right, origin, dir, maxT);
    }

    std::vector<Tri> m_tris;
    std::vector<u32> m_order;
    std::vector<BvhNode> m_nodes;
};

Assets::Mesh3D* meshFor(ECS::World& world, ECS::Entity entity, const std::string& projectRoot) {
    auto* filter = world.get<ECS::MeshFilterComponent>(entity);
    if (!filter) return nullptr;
    if (filter->primitive == ECS::MeshPrimitive::Custom) {
        if (filter->customMeshPath.empty()) return nullptr;
        return Assets::MeshCache::getInstance().getMesh(filter->customMeshPath, projectRoot);
    }
    return Render::GpuProceduralMeshes::get(filter->primitive);
}

fs::path resolve(const fs::path& root, const std::string& path) {
    if (path.empty()) return {};
    fs::path p(path);
    return p.is_absolute() ? p : root / p;
}

std::string relativeTo(const fs::path& root, const fs::path& path) {
    std::error_code ec;
    const fs::path rel = fs::relative(path, root, ec);
    return ec ? path.generic_string() : rel.generic_string();
}

struct MaterialTarget {
    fs::path path;
    Assets::MaterialSurface surface;
    bool useFilterPath = false;
    bool existed = false;
};

bool loadOrCreateMaterial(ECS::World& world, ECS::Entity entity, const fs::path& root,
                          const fs::path& outputDir, MaterialTarget& out,
                          std::string& error) {
    auto* filter = world.get<ECS::MeshFilterComponent>(entity);
    auto* renderer = world.get<ECS::MeshRendererComponent>(entity);
    if (!filter) {
        error = "Entity has no mesh filter";
        return false;
    }

    if (!filter->customMaterialPath.empty()) {
        out.useFilterPath = true;
        out.path = resolve(root, filter->customMaterialPath);
    } else if (renderer && !renderer->materialPath.empty()) {
        out.path = resolve(root, renderer->materialPath);
    } else {
        out.path = outputDir / ("ao_" + std::to_string(entity.id()) + ".mat");
        out.useFilterPath = true;
    }

    out.existed = fs::exists(out.path);
    if (out.existed) {
        if (!Assets::loadMaterialFile(out.path, out.surface)) {
            error = "Could not read material: " + out.path.string();
            return false;
        }
        return true;
    }

    out.surface = Assets::MaterialSurface{};
    out.surface.valid = true;
    out.surface.name = "Baked AO " + std::to_string(entity.id());
    return true;
}

Vec3 sampleCpuAO(const SceneBvh& bvh, const Vec3& position, const Vec3& normal, u32 rayCount,
                 f32 maxDistance, f32 bias, u32 seed) {
    const Vec3 rayOrigin = position + normal * bias;
    const Vec3 up = std::abs(normal.y) < 0.99f ? Vec3(0, 1, 0) : Vec3(1, 0, 0);
    const Vec3 tangent = up.cross(normal).normalized();
    const Vec3 bitangent = normal.cross(tangent);
    const f32 jitter = hash01(seed) * 6.2831853f;

    u32 visible = 0;
    for (u32 r = 0; r < rayCount; ++r) {
        const f32 fi = (static_cast<f32>(r) + 0.5f) / static_cast<f32>(rayCount);
        const f32 phi = static_cast<f32>(r) * 2.39996323f + jitter;
        const f32 cosTheta = std::sqrt(std::max(0.0f, 1.0f - fi));
        const f32 sinTheta = std::sqrt(fi);
        const Vec3 local(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
        const Vec3 dir =
            (tangent * local.x + bitangent * local.y + normal * local.z).normalized();
        if (!bvh.anyHit(rayOrigin, dir, maxDistance)) ++visible;
    }
    return Vec3(static_cast<f32>(visible) / static_cast<f32>(rayCount), 0.0f, 0.0f);
}

}  // namespace

BakeResult bakeAmbientOcclusion(ECS::World& world, ECS::Entity entity,
                                const BakeSettings& settings,
                                const std::string& projectRoot, void* renderDevice) {
    BakeResult result;
    const fs::path root = fs::path(projectRoot);
    if (root.empty()) {
        result.error = "No project root";
        return result;
    }

    Assets::Mesh3D* targetMesh = meshFor(world, entity, projectRoot);
    if (!targetMesh || targetMesh->vertices.empty() || targetMesh->indices.empty()) {
        result.error = "Selected entity has no mesh";
        return result;
    }

    std::vector<Tri> triangles;
    ECS::ComponentQuery query;
    query.with<ECS::MeshFilterComponent>();
    world.forEach<ECS::MeshFilterComponent>(query, [&](ECS::Entity e, ECS::MeshFilterComponent&) {
        if (Scene::isEffectivelyDisabled(world, e)) return;
        Assets::Mesh3D* mesh = meshFor(world, e, projectRoot);
        if (!mesh || mesh->vertices.empty() || mesh->indices.empty()) return;
        const Mat4 worldMatrix = Scene::computeWorldMatrix(world, e);
        for (u32 i = 0; i + 2 < mesh->indices.size(); i += 3) {
            const auto& v0 = mesh->vertices[mesh->indices[i + 0]].position;
            const auto& v1 = mesh->vertices[mesh->indices[i + 1]].position;
            const auto& v2 = mesh->vertices[mesh->indices[i + 2]].position;
            triangles.push_back({worldMatrix.transformPoint(v0), worldMatrix.transformPoint(v1),
                                 worldMatrix.transformPoint(v2)});
        }
    });

    if (triangles.empty()) {
        result.error = "No static geometry to occlude against";
        return result;
    }

    SceneBvh bvh;
    bvh.build(triangles);
    result.triangles = bvh.triangleCount();

    const u32 resolution = std::clamp(settings.resolution, 64u, 2048u);
    const u32 rayCount = std::clamp(settings.rays, 1u, 64u);
    const f32 maxDistance = std::max(settings.maxDistance, 0.01f);
    const f32 bias = std::max(settings.bias, 0.0f);
    const size_t texelCount = static_cast<size_t>(resolution) * resolution;

    std::vector<Vec4> positions(texelCount, Vec4(0.0f, 0.0f, 0.0f, 0.0f));
    std::vector<Vec4> normals(texelCount, Vec4(0.0f, 0.0f, 0.0f, 0.0f));
    std::vector<u8> covered(texelCount, 0);

    const Mat4 worldMatrix = Scene::computeWorldMatrix(world, entity);
    const Mat4 normalMatrix = worldMatrix.inverted().transposed();

    u32 coveredTexels = 0;
    for (u32 i = 0; i + 2 < targetMesh->indices.size(); i += 3) {
        const Assets::Vertex3D& v0 = targetMesh->vertices[targetMesh->indices[i + 0]];
        const Assets::Vertex3D& v1 = targetMesh->vertices[targetMesh->indices[i + 1]];
        const Assets::Vertex3D& v2 = targetMesh->vertices[targetMesh->indices[i + 2]];

        const Vec2 uv0 = v0.texcoord;
        const Vec2 uv1 = v1.texcoord;
        const Vec2 uv2 = v2.texcoord;

        const Vec3 p0 = worldMatrix.transformPoint(v0.position);
        const Vec3 p1 = worldMatrix.transformPoint(v1.position);
        const Vec3 p2 = worldMatrix.transformPoint(v2.position);
        const Vec3 n0 = normalMatrix.transformVector(v0.normal).normalized();
        const Vec3 n1 = normalMatrix.transformVector(v1.normal).normalized();
        const Vec3 n2 = normalMatrix.transformVector(v2.normal).normalized();

        const f32 minU = std::min({uv0.x, uv1.x, uv2.x});
        const f32 maxU = std::max({uv0.x, uv1.x, uv2.x});
        const f32 minV = std::min({uv0.y, uv1.y, uv2.y});
        const f32 maxV = std::max({uv0.y, uv1.y, uv2.y});
        if (maxU - minU < 1e-6f || maxV - minV < 1e-6f) continue;

        const int x0 = std::max(0, static_cast<int>(std::floor(minU * resolution)) - 1);
        const int x1 = std::min(static_cast<int>(resolution) - 1,
                                static_cast<int>(std::ceil(maxU * resolution)) + 1);
        const int y0 = std::max(0, static_cast<int>(std::floor(minV * resolution)) - 1);
        const int y1 = std::min(static_cast<int>(resolution) - 1,
                                static_cast<int>(std::ceil(maxV * resolution)) + 1);

        const f32 area = (uv1.x - uv0.x) * (uv2.y - uv0.y) -
                         (uv2.x - uv0.x) * (uv1.y - uv0.y);
        if (std::abs(area) < 1e-9f) continue;
        const f32 invArea = 1.0f / area;

        for (int y = y0; y <= y1; ++y) {
            const f32 py = (static_cast<f32>(y) + 0.5f) / static_cast<f32>(resolution);
            for (int x = x0; x <= x1; ++x) {
                const f32 px = (static_cast<f32>(x) + 0.5f) / static_cast<f32>(resolution);
                const f32 w1 = ((px - uv0.x) * (uv2.y - uv0.y) -
                                (uv2.x - uv0.x) * (py - uv0.y)) * invArea;
                const f32 w2 = ((uv1.x - uv0.x) * (py - uv0.y) -
                                (px - uv0.x) * (uv1.y - uv0.y)) * invArea;
                const f32 w0 = 1.0f - w1 - w2;
                if (w0 < -1e-4f || w1 < -1e-4f || w2 < -1e-4f) continue;

                const size_t index = static_cast<size_t>(y) * resolution + x;
                const Vec3 position = p0 * w0 + p1 * w1 + p2 * w2;
                positions[index] = Vec4(position.x, position.y, position.z, 1.0f);
                Vec3 normal = n0 * w0 + n1 * w1 + n2 * w2;
                if (normal.lengthSquared() < 1e-8f) continue;
                normal = normal.normalized();
                normals[index] = Vec4(normal.x, normal.y, normal.z, 0.0f);
                if (!covered[index]) {
                    covered[index] = 1;
                    ++coveredTexels;
                }
            }
        }
    }

    if (coveredTexels == 0) {
        result.error = "Mesh has no usable UV coverage";
        return result;
    }

    std::vector<f32> ao(texelCount, 1.0f);
    bool usedGpu = false;
    constexpr u32 kGpuTriangleLimit = 8192;

    if (settings.useGpu && renderDevice && bvh.triangleCount() <= kGpuTriangleLimit) {
        std::vector<float> triangleFloats(static_cast<size_t>(bvh.triangleCount()) * 9, 0.0f);
        for (u32 t = 0; t < bvh.triangleCount(); ++t) {
            // Keep the same triangle order as bvh's source list.
            const Tri& tri = triangles[t];
            float* dst = triangleFloats.data() + static_cast<size_t>(t) * 9;
            dst[0] = tri.a.x; dst[1] = tri.a.y; dst[2] = tri.a.z;
            dst[3] = tri.b.x; dst[4] = tri.b.y; dst[5] = tri.b.z;
            dst[6] = tri.c.x; dst[7] = tri.c.y; dst[8] = tri.c.z;
        }

        std::vector<float> gpuAO(texelCount, 1.0f);
        Render::AOBakeDesc desc{};
        desc.triangles = triangleFloats.data();
        desc.triangleCount = bvh.triangleCount();
        desc.positions = reinterpret_cast<const float*>(positions.data());
        desc.normals = reinterpret_cast<const float*>(normals.data());
        desc.texelCount = static_cast<unsigned int>(texelCount);
        desc.resolution = resolution;
        desc.rays = rayCount;
        desc.maxDistance = maxDistance;
        desc.bias = bias;
        usedGpu = Render::gpuBakeAmbientOcclusion(
            static_cast<RHI::RenderDevice*>(renderDevice), desc, gpuAO);
        if (usedGpu) {
            for (size_t i = 0; i < texelCount; ++i) {
                ao[i] = covered[i] ? gpuAO[i] : 1.0f;
            }
        }
    }

    if (!usedGpu) {
        for (u32 y = 0; y < resolution; ++y) {
            for (u32 x = 0; x < resolution; ++x) {
                const size_t index = static_cast<size_t>(y) * resolution + x;
                if (!covered[index]) continue;
                const Vec3 position(positions[index].x, positions[index].y, positions[index].z);
                const Vec3 normal(normals[index].x, normals[index].y, normals[index].z);
                const Vec3 sample = sampleCpuAO(bvh, position, normal, rayCount, maxDistance,
                                                bias,
                                                static_cast<u32>(x * 1973 + y * 9277));
                ao[index] = sample.x;
            }
        }
    }

    const fs::path outputDir = resolve(root, settings.outputDirectory);
    MaterialTarget material;
    std::string error;
    if (!loadOrCreateMaterial(world, entity, root, outputDir, material, error)) {
        result.error = error;
        return result;
    }

    const fs::path existingOrm = resolve(root, material.surface.ormMap);
    int ormW = 0;
    int ormH = 0;
    int ormC = 0;
    stbi_uc* ormPixels = existingOrm.empty() || !fs::exists(existingOrm)
                             ? nullptr
                             : stbi_load(existingOrm.string().c_str(), &ormW, &ormH, &ormC, 4);

    std::vector<u8> pixels(texelCount * 4);
    for (u32 y = 0; y < resolution; ++y) {
        for (u32 x = 0; x < resolution; ++x) {
            const size_t index = static_cast<size_t>(y) * resolution + x;
            f32 g = std::clamp(material.surface.roughness, 0.0f, 1.0f);
            f32 b = std::clamp(material.surface.metallic, 0.0f, 1.0f);
            if (ormPixels && ormW > 0 && ormH > 0) {
                const int sx = static_cast<int>((static_cast<f32>(x) + 0.5f) /
                                                static_cast<f32>(resolution) * ormW) % ormW;
                const int sy = static_cast<int>((static_cast<f32>(y) + 0.5f) /
                                                static_cast<f32>(resolution) * ormH) % ormH;
                const stbi_uc* src = ormPixels + (static_cast<size_t>(sy) * ormW + sx) * 4;
                g = static_cast<f32>(src[1]) / 255.0f;
                b = static_cast<f32>(src[2]) / 255.0f;
            }
            const f32 aoValue = covered[index] ? ao[index] : 1.0f;
            pixels[index * 4 + 0] =
                static_cast<u8>(std::clamp(aoValue, 0.0f, 1.0f) * 255.0f + 0.5f);
            pixels[index * 4 + 1] = static_cast<u8>(std::clamp(g, 0.0f, 1.0f) * 255.0f + 0.5f);
            pixels[index * 4 + 2] = static_cast<u8>(std::clamp(b, 0.0f, 1.0f) * 255.0f + 0.5f);
            pixels[index * 4 + 3] = 255;
        }
    }
    if (ormPixels) stbi_image_free(ormPixels);

    const fs::path ormPath = outputDir / ("ao_" + std::to_string(entity.id()) + ".png");
    std::error_code ec;
    fs::create_directories(ormPath.parent_path(), ec);
    if (!stbi_write_png(ormPath.string().c_str(), static_cast<int>(resolution),
                        static_cast<int>(resolution), 4, pixels.data(),
                        static_cast<int>(resolution) * 4)) {
        result.error = "Could not write " + ormPath.string();
        return result;
    }

    const std::string ormRelative = relativeTo(root, ormPath);
    material.surface.ormMap = ormRelative;
    material.surface.aoStrength = 1.0f;
    material.surface.valid = true;
    fs::create_directories(material.path.parent_path(), ec);
    if (!Assets::saveMaterialFile(material.path, material.surface)) {
        result.error = "Could not write material " + material.path.string();
        return result;
    }

    const std::string materialRelative = relativeTo(root, material.path);
    if (auto* filter = world.get<ECS::MeshFilterComponent>(entity)) {
        if (material.useFilterPath) filter->customMaterialPath = materialRelative;
    }
    if (auto* renderer = world.get<ECS::MeshRendererComponent>(entity)) {
        if (!material.useFilterPath) renderer->materialPath = materialRelative;
    }
    Assets::MaterialCache::instance().invalidate(materialRelative);

    result.ok = true;
    result.usedGpu = usedGpu;
    result.ormPath = ormRelative;
    result.materialPath = materialRelative;
    result.coveredTexels = coveredTexels;
    return result;
}

}  // namespace Caffeine::Photon
