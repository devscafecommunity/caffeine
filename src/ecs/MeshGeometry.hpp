#pragma once

#include "assets/MeshTypes.hpp"
#include "ecs/World.hpp"
#include "math/Vec3.hpp"

#include <vector>

namespace Caffeine::ECS {

/// Per-entity mesh. Present only after the viewport edits vertices, edges or faces.
struct MeshGeometryComponent {
    std::vector<Vec3> positions;
    std::vector<u32> indices;
    u32 revision = 1;
};

/// Copies the current primitive (or custom mesh) into an editable component.
bool bakeMeshGeometry(World& world, Entity entity, const std::string& projectRoot = {});

/// Cached mesh for a baked component. Null when the entity still uses the shared primitive.
Assets::Mesh3D* editedMesh(World& world, Entity entity);

/// Triangles that form the clicked face, including a quad split into two triangles.
std::vector<u32> faceVertexIndices(const MeshGeometryComponent& geometry, u32 triangle);

}  // namespace Caffeine::ECS
