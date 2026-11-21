#include "scatter/TerrainScatter.hpp"

#include <algorithm>
#include <cmath>

namespace Caffeine::Scatter {

TerrainScatterResult scatterTerrain(const TerrainScatterRequest& request) {
    TerrainScatterResult result;
    const Terrain::TerrainFrame& frame = request.frame;
    if (!frame.heightmap || frame.heightmap->empty() || request.classes.empty()) return result;

    const f32 sizeX = std::max(frame.sizeX, 1e-3f), sizeZ = std::max(frame.sizeZ, 1e-3f);
    f32 x0 = request.x0, z0 = request.z0, x1 = request.x1, z1 = request.z1;
    if (x1 <= x0 || z1 <= z0) {
        x0 = 0.0f;
        z0 = 0.0f;
        x1 = sizeX;
        z1 = sizeZ;
    }
    x0 = std::max(x0, 0.0f);
    z0 = std::max(z0, 0.0f);
    x1 = std::min(x1, sizeX);
    z1 = std::min(z1, sizeZ);
    if (x1 <= x0 || z1 <= z0) return result;

    static const Terrain::TerrainMasks kNoMasks;
    const Terrain::TerrainMasks& masks = request.masks ? *request.masks : kNoMasks;

    ScatterField field;
    field.height = [&frame, sizeX, sizeZ](f32 x, f32 z) {
        const f32 u = std::clamp(x / sizeX, 0.0f, 1.0f), v = std::clamp(z / sizeZ, 0.0f, 1.0f);
        return frame.heightmap->sampleBilinear(u, v) * frame.maxHeight;
    };
    field.normal = [&frame](f32 x, f32 z) { return Terrain::TerrainMasks::normalAt(frame, x, z); };
    if (request.biome) field.density = request.biome->densityFunction(masks, frame);

    const f32 chunk = std::max(request.chunkSize, 8.0f);
    const i32 cx0 = static_cast<i32>(std::floor(x0 / chunk)), cx1 = static_cast<i32>(std::floor((x1 - 1e-4f) / chunk));
    const i32 cz0 = static_cast<i32>(std::floor(z0 / chunk)), cz1 = static_cast<i32>(std::floor((z1 - 1e-4f) / chunk));
    for (i32 cz = cz0; cz <= cz1; ++cz) {
        for (i32 cx = cx0; cx <= cx1; ++cx) {
            for (const Instance& instance : scatterChunk(request.seed, cx, cz, chunk, request.classes, field)) {
                const Vec3& p = instance.position;
                if (p.x < x0 || p.x >= x1 || p.z < z0 || p.z >= z1) continue;
                ++result.total;
                if (request.maxInstances == 0 || result.instances.size() < request.maxInstances) {
                    result.instances.push_back(instance);
                } else {
                    result.truncated = true;
                }
            }
        }
    }
    return result;
}

}  // namespace Caffeine::Scatter
