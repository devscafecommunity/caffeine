#pragma once

// The GPU side of a terrain's vegetation (terrain editing roadmap, phase 3): one instance buffer per chunk and species
// variant, built on demand for what a camera can see, kept until an edit changes the chunk.
//
// Instances are never entities. A chunk of the terrain (32 m a side) is resolved on the CPU (`TerrainVegetation`), grouped
// by species and variant, and its model matrices go into one vertex buffer each; the renderer draws every buffer with one
// instanced call. Chunks are culled against the frustum and by distance, and only a few are built per frame, so a view that
// suddenly sees a forest fills in over a few frames instead of stalling.

#include "core/Types.hpp"
#include "ecs/Entity.hpp"
#include "ecs/TerrainComponents.hpp"
#include "math/Mat4.hpp"
#include "math/Vec3.hpp"
#include "spatial/Octree.hpp"

#ifdef CF_HAS_SDL3
#include "rhi/RenderDevice.hpp"

#include <unordered_map>
#include <vector>

namespace Caffeine::Terrain {
class TerrainCache;
}

namespace Caffeine::Render {

/// One instanced draw: the instances of a species variant in one chunk.
struct VegetationBatch {
    RHI::Buffer* buffer = nullptr;
    u32 count = 0;
    u32 species = 0;  ///< palette index
    u32 variant = 0;
    Mat4 first = Mat4::identity();  ///< instance 0 in world space (a batch of one is drawn as a plain mesh)
    f32 distance = 0.0f;
};

struct VegetationFrameStats {
    u32 chunksVisible = 0;
    u32 chunksBuilt = 0;      ///< built this frame
    u32 chunksWaiting = 0;    ///< visible but over the frame's build budget
    u32 batches = 0;
    u64 instances = 0;
};

class TerrainVegetationGpu {
public:
    static TerrainVegetationGpu& instance();

    /// Brings the entity's chunk buffers up to date and lists what to draw. At most `buildBudget` chunks are built.
    void collect(RHI::RenderDevice* device, ECS::Entity entity, const ECS::TerrainComponent& terrain, Terrain::TerrainCache& cache,
                 const Mat4& world, const Vec3& cameraPos, const Spatial::Frustum& frustum, std::vector<VegetationBatch>& out,
                 VegetationFrameStats* stats, u32 buildBudget = 6);

    /// The GPU memory the entity's instance buffers take.
    u64 bytesFor(ECS::Entity entity) const;
    void removeEntity(ECS::Entity entity, RHI::RenderDevice* device);
    void releaseAll(RHI::RenderDevice* device);

    /// The matrix of an instance standing at (`x`, `y`, `z`) with this up axis, yaw (radians) and scale, in the terrain's space.
    static Mat4 instanceMatrix(f32 x, f32 y, f32 z, const f32* up, f32 yaw, f32 scale);

private:
    struct Chunk {
        std::vector<VegetationBatch> batches;
        bool built = false;
    };
    struct Entry {
        std::unordered_map<u64, Chunk> chunks;
        Mat4 world = Mat4::identity();
        bool hasWorld = false;
    };
    void releaseChunk(RHI::RenderDevice* device, Chunk& chunk);

    std::unordered_map<u32, Entry> m_entries;
};

}  // namespace Caffeine::Render
#endif
