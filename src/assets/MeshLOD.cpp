#include "assets/MeshLOD.hpp"
#include <algorithm>

namespace Caffeine::Assets {

void MeshLOD::generateLODs(Mesh3D* mesh, int lodCount, f32 reductionRatio) {
    if (!mesh || mesh->vertices.empty() || lodCount < 1) return;
    
    mesh->lodCount = lodCount;
}

void MeshLOD::simplifyMesh(const Mesh3D& source, Mesh3D& target, f32 targetRatio) {
    u32 targetVertexCount = (u32)(source.vertices.size() * targetRatio);
    if (targetVertexCount < 3) targetVertexCount = 3;
    
    target.vertices.resize(targetVertexCount);
    for (u32 i = 0; i < targetVertexCount; ++i) {
        target.vertices[i] = source.vertices[i * source.vertices.size() / targetVertexCount];
    }
    
    target.indices.clear();
    for (u32 i = 0; i + 2 < target.vertices.size(); i += 3) {
        target.indices.push_back(i);
        target.indices.push_back(i + 1);
        target.indices.push_back(i + 2);
    }
    
    target.bounds = source.bounds;
}

}  // namespace Caffeine::Assets
