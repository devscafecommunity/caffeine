#pragma once
#include "assets/MeshTypes.hpp"

namespace Caffeine::Assets {

class MeshLOD {
public:
    static void generateLODs(Mesh3D* mesh, int lodCount = 3, f32 reductionRatio = 0.5f);

    // Rossignac vertex clustering. Writes a new mesh; does not modify source.
    // Returns false when the result is empty or not actually smaller.
    static bool buildClusteredLod(const Mesh3D& source, Mesh3D& target, f32 cellSize);

private:
    static void simplifyMesh(const Mesh3D& source, Mesh3D& target, f32 targetRatio);
};

}  // namespace Caffeine::Assets
