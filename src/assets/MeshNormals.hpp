#pragma once

#include "assets/MeshTypes.hpp"

#include <cmath>

namespace Caffeine::Assets {

/// Recomputes per-vertex normals by averaging adjacent face normals.
/// Shared vertices on curved surfaces become smooth; isolated vertices (cube faces,
/// pyramid facets, terrain-style hard edges) keep their facet shading.
inline void computeSmoothNormals(Mesh3D& mesh) {
    if (mesh.vertices.empty() || mesh.indices.empty()) return;

    for (auto& vertex : mesh.vertices) {
        vertex.normal = Vec3(0.0f, 0.0f, 0.0f);
    }

    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const u32 i0 = mesh.indices[i + 0];
        const u32 i1 = mesh.indices[i + 1];
        const u32 i2 = mesh.indices[i + 2];
        if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size()) {
            continue;
        }

        const Vec3& a = mesh.vertices[i0].position;
        const Vec3& b = mesh.vertices[i1].position;
        const Vec3& c = mesh.vertices[i2].position;
        Vec3 faceN = (b - a).cross(c - a);
        if (faceN.lengthSquared() < 1e-12f) continue;

        faceN = faceN.normalized();
        mesh.vertices[i0].normal += faceN;
        mesh.vertices[i1].normal += faceN;
        mesh.vertices[i2].normal += faceN;
    }

    for (auto& vertex : mesh.vertices) {
        if (vertex.normal.lengthSquared() > 1e-12f) {
            vertex.normal = vertex.normal.normalized();
        } else {
            vertex.normal = Vec3(0.0f, 1.0f, 0.0f);
        }
    }
}

}  // namespace Caffeine::Assets
