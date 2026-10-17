#include "ecs/MeshGeometry.hpp"

#include "assets/MeshCache.hpp"
#include "ecs/ComponentQuery.hpp"
#include "ecs/MeshComponents.hpp"

#ifdef CF_HAS_SDL3
#include "render/GpuProceduralMeshes.hpp"
#endif

#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace Caffeine::ECS {
namespace {

struct MeshSlot {
    u32 revision = 0;
    Assets::Mesh3D mesh;
};

std::unordered_map<u32, MeshSlot> g_edited;

u32 nextRevision = 1;

Assets::Mesh3D* sourceMesh(World& world, Entity entity, const std::string& projectRoot) {
    const MeshFilterComponent* filter = world.get<MeshFilterComponent>(entity);
    if (!filter) return nullptr;
    if (filter->primitive == MeshPrimitive::Custom) {
        if (filter->customMeshPath.empty()) return nullptr;
        return Assets::MeshCache::getInstance().getMesh(filter->customMeshPath, projectRoot);
    }
#ifdef CF_HAS_SDL3
    return Render::GpuProceduralMeshes::get(filter->primitive);
#else
    return nullptr;
#endif
}

void rebuild(MeshSlot& slot, const MeshGeometryComponent& geometry) {
    Assets::Mesh3D& mesh = slot.mesh;
    mesh.vertices.clear();
    mesh.indices = geometry.indices;
    mesh.vertices.reserve(geometry.positions.size());
    Vec3 bmin(1.0e30f, 1.0e30f, 1.0e30f);
    Vec3 bmax(-1.0e30f, -1.0e30f, -1.0e30f);
    for (const Vec3& position : geometry.positions) {
        Assets::Vertex3D vertex{};
        vertex.position = position;
        vertex.normal = Vec3(0.0f, 1.0f, 0.0f);
        vertex.texcoord = {0.5f, 0.5f};
        vertex.tangent = Vec4(1.0f, 0.0f, 0.0f, 1.0f);
        mesh.vertices.push_back(vertex);
        bmin.x = std::min(bmin.x, position.x);
        bmin.y = std::min(bmin.y, position.y);
        bmin.z = std::min(bmin.z, position.z);
        bmax.x = std::max(bmax.x, position.x);
        bmax.y = std::max(bmax.y, position.y);
        bmax.z = std::max(bmax.z, position.z);
    }
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const u32 i0 = mesh.indices[i];
        const u32 i1 = mesh.indices[i + 1];
        const u32 i2 = mesh.indices[i + 2];
        if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size()) continue;
        const Vec3& a = mesh.vertices[i0].position;
        const Vec3& b = mesh.vertices[i1].position;
        const Vec3& c = mesh.vertices[i2].position;
        Vec3 normal = (b - a).cross(c - a);
        if (normal.lengthSquared() < 1.0e-10f) continue;
        normal = normal.normalized();
        mesh.vertices[i0].normal = normal;
        mesh.vertices[i1].normal = normal;
        mesh.vertices[i2].normal = normal;
    }
    if (mesh.vertices.empty()) {
        mesh.bounds = {};
    } else {
        mesh.bounds = {bmin, bmax};
    }
    slot.revision = geometry.revision;
}

bool samePoint(const Vec3& a, const Vec3& b) {
    return (a - b).lengthSquared() < 1.0e-8f;
}

}  // namespace

bool bakeMeshGeometry(World& world, Entity entity, const std::string& projectRoot) {
    if (world.has<MeshGeometryComponent>(entity)) return true;
    Assets::Mesh3D* source = sourceMesh(world, entity, projectRoot);
    if (!source || source->vertices.empty() || source->indices.empty()) return false;
    MeshGeometryComponent geometry;
    geometry.positions.reserve(source->vertices.size());
    for (const Assets::Vertex3D& vertex : source->vertices) {
        geometry.positions.push_back(vertex.position);
    }
    geometry.indices = source->indices;
    geometry.revision = ++nextRevision;
    world.add<MeshGeometryComponent>(entity, std::move(geometry));
    return true;
}

Assets::Mesh3D* editedMesh(World& world, Entity entity) {
    const MeshGeometryComponent* geometry = world.get<MeshGeometryComponent>(entity);
    if (!geometry || geometry->positions.empty() || geometry->indices.empty()) return nullptr;
    MeshSlot& slot = g_edited[entity.id()];
    bool current = slot.revision == geometry->revision && slot.mesh.vertices.size() == geometry->positions.size();
    if (current) {
        for (size_t i = 0; i < geometry->positions.size(); ++i) {
            if ((slot.mesh.vertices[i].position - geometry->positions[i]).lengthSquared() > 1.0e-8f) {
                current = false;
                break;
            }
        }
    }
    if (!current) rebuild(slot, *geometry);
    return slot.mesh.vertices.empty() ? nullptr : &slot.mesh;
}

std::vector<u32> faceVertexIndices(const MeshGeometryComponent& geometry, u32 triangle) {
    std::vector<u32> vertices;
    const u32 triangleCount = static_cast<u32>(geometry.indices.size() / 3);
    if (triangle >= triangleCount) return vertices;
    std::vector<u8> taken(triangleCount, 0);
    std::vector<u32> stack;
    stack.push_back(triangle);
    taken[triangle] = 1;
    const u32 base = triangle * 3;
    const Vec3& a = geometry.positions[geometry.indices[base]];
    const Vec3& b = geometry.positions[geometry.indices[base + 1]];
    const Vec3& c = geometry.positions[geometry.indices[base + 2]];
    Vec3 faceNormal = (b - a).cross(c - a);
    if (faceNormal.lengthSquared() < 1.0e-10f) {
        vertices.push_back(geometry.indices[base]);
        vertices.push_back(geometry.indices[base + 1]);
        vertices.push_back(geometry.indices[base + 2]);
        return vertices;
    }
    faceNormal = faceNormal.normalized();

    std::unordered_set<u32> unique;
    while (!stack.empty()) {
        const u32 current = stack.back();
        stack.pop_back();
        const u32 offset = current * 3;
        for (u32 corner = 0; corner < 3; ++corner) unique.insert(geometry.indices[offset + corner]);
        for (u32 other = 0; other < triangleCount; ++other) {
            if (taken[other]) continue;
            const u32 otherOffset = other * 3;
            const Vec3& oa = geometry.positions[geometry.indices[otherOffset]];
            const Vec3& ob = geometry.positions[geometry.indices[otherOffset + 1]];
            const Vec3& oc = geometry.positions[geometry.indices[otherOffset + 2]];
            Vec3 otherNormal = (ob - oa).cross(oc - oa);
            if (otherNormal.lengthSquared() < 1.0e-10f) continue;
            otherNormal = otherNormal.normalized();
            if (otherNormal.dot(faceNormal) < 0.98f) continue;
            bool shares = false;
            for (u32 corner = 0; corner < 3 && !shares; ++corner) {
                const Vec3& point = geometry.positions[geometry.indices[offset + corner]];
                for (u32 otherCorner = 0; otherCorner < 3; ++otherCorner) {
                    if (samePoint(point, geometry.positions[geometry.indices[otherOffset + otherCorner]])) {
                        shares = true;
                        break;
                    }
                }
            }
            if (!shares) continue;
            taken[other] = 1;
            stack.push_back(other);
        }
    }
    vertices.assign(unique.begin(), unique.end());
    return vertices;
}

}  // namespace Caffeine::ECS
