#pragma once

#include "assets/MeshTypes.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Caffeine::Assets {

/// Statistics the displacement shaders need to rebuild normals in world units.
struct MeshDensity {
    /// Object-space metres per UV unit: sqrt(sum of triangle areas / sum of UV areas).
    f32 metersPerUv = 1.0f;
    /// Average edge length in UV units (the vertex spacing of the displaced surface).
    f32 averageEdgeUv = 1.0f;
};

inline MeshDensity measureMeshDensity(const Mesh3D& mesh) {
    f64 area3d = 0.0;
    f64 areaUv = 0.0;
    f64 edgeUv = 0.0;
    size_t edges = 0;
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const Vertex3D& a = mesh.vertices[mesh.indices[i]];
        const Vertex3D& b = mesh.vertices[mesh.indices[i + 1]];
        const Vertex3D& c = mesh.vertices[mesh.indices[i + 2]];
        area3d += 0.5 * static_cast<f64>((b.position - a.position).cross(c.position - a.position).length());
        const Vec2 e1 = b.texcoord - a.texcoord;
        const Vec2 e2 = c.texcoord - a.texcoord;
        areaUv += 0.5 * std::abs(static_cast<f64>(e1.x) * e2.y - static_cast<f64>(e2.x) * e1.y);
        edgeUv += std::sqrt(static_cast<f64>(e1.x) * e1.x + static_cast<f64>(e1.y) * e1.y);
        edgeUv += std::sqrt(static_cast<f64>(e2.x) * e2.x + static_cast<f64>(e2.y) * e2.y);
        edges += 2;
    }
    MeshDensity density;
    if (areaUv > 1e-12 && area3d > 1e-12) {
        density.metersPerUv = static_cast<f32>(std::sqrt(area3d / areaUv));
    }
    if (edges > 0) density.averageEdgeUv = static_cast<f32>(edgeUv / static_cast<f64>(edges));
    return density;
}

/// Midpoint subdivision: every level splits each triangle into four. Vertices on shared edges
/// are shared, so a closed surface stays welded. Normals and tangents are renormalised and the
/// UVs interpolated linearly. Sub-meshes (and therefore materials) are preserved.
/// Returns false (leaving `out` untouched) when the result would exceed `maxIndices`.
inline bool subdivideMesh(const Mesh3D& source, u32 levels, Mesh3D& out,
                          size_t maxIndices = 6'000'000) {
    if (source.vertices.empty() || source.indices.size() < 3 || levels == 0) return false;
    size_t projected = source.indices.size();
    for (u32 i = 0; i < levels; ++i) {
        projected *= 4;
        if (projected > maxIndices) return false;
    }

    std::vector<Vertex3D> vertices = source.vertices;
    std::vector<u32> indices = source.indices;
    std::vector<SubMesh> subMeshes = source.subMeshes;
    if (subMeshes.empty()) {
        SubMesh whole;
        whole.indexOffset = 0;
        whole.indexCount = static_cast<u32>(indices.size());
        subMeshes.push_back(whole);
    }

    for (u32 level = 0; level < levels; ++level) {
        std::unordered_map<std::uint64_t, u32> midpoints;
        midpoints.reserve(indices.size());
        auto midpoint = [&](u32 a, u32 b) -> u32 {
            const std::uint64_t key = (static_cast<std::uint64_t>(std::min(a, b)) << 32) | std::max(a, b);
            const auto found = midpoints.find(key);
            if (found != midpoints.end()) return found->second;
            const Vertex3D& va = vertices[a];
            const Vertex3D& vb = vertices[b];
            Vertex3D v;
            v.position = (va.position + vb.position) * 0.5f;
            Vec3 n = va.normal + vb.normal;
            v.normal = n.lengthSquared() > 1e-12f ? n.normalized() : va.normal;
            v.texcoord = (va.texcoord + vb.texcoord) * 0.5f;
            Vec3 t = Vec3(va.tangent.x + vb.tangent.x, va.tangent.y + vb.tangent.y,
                          va.tangent.z + vb.tangent.z);
            t = t.lengthSquared() > 1e-12f ? t.normalized() : Vec3(va.tangent.x, va.tangent.y, va.tangent.z);
            v.tangent = Vec4(t.x, t.y, t.z, va.tangent.w);
            const u32 index = static_cast<u32>(vertices.size());
            vertices.push_back(v);
            midpoints.emplace(key, index);
            return index;
        };

        std::vector<u32> next;
        next.reserve(indices.size() * 4);
        std::vector<SubMesh> nextSub;
        nextSub.reserve(subMeshes.size());
        for (const SubMesh& sub : subMeshes) {
            SubMesh out2 = sub;
            out2.indexOffset = static_cast<u32>(next.size());
            const size_t end = static_cast<size_t>(sub.indexOffset) + sub.indexCount;
            for (size_t i = sub.indexOffset; i + 2 < end; i += 3) {
                const u32 a = indices[i];
                const u32 b = indices[i + 1];
                const u32 c = indices[i + 2];
                const u32 ab = midpoint(a, b);
                const u32 bc = midpoint(b, c);
                const u32 ca = midpoint(c, a);
                const u32 tris[12] = {a, ab, ca, ab, b, bc, ca, bc, c, ab, bc, ca};
                next.insert(next.end(), tris, tris + 12);
            }
            out2.indexCount = static_cast<u32>(next.size()) - out2.indexOffset;
            nextSub.push_back(out2);
        }
        indices.swap(next);
        subMeshes.swap(nextSub);
    }

    out = source;
    out.vertices = std::move(vertices);
    out.indices = std::move(indices);
    out.subMeshes = std::move(subMeshes);
    out.skin.clear();
    out.lodCount = 1;
    out.geometryRevision = 1;
    out.uploadedRevision = 0;
#ifdef CF_HAS_SDL3
    out.vertexBuffer = nullptr; // the copy owns no GPU buffers yet
    out.indexBuffer = nullptr;
    out.colorBuffer = nullptr;
    out.vertexColors.clear(); // subdivision adds vertices: the colours no longer line up
#endif
    return true;
}

}  // namespace Caffeine::Assets
