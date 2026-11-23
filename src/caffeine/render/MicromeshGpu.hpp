#pragma once

// Micromeshes on the GPU (phase M-2): the cluster hierarchy lives in GPU buffers and a compute pass
// selects this frame's cut (error test, frustum, back-face cone). Two ways to draw the cut:
//
//  * The per-cluster path (`cull` + `draw*`): one indirect indexed draw per cluster and instance, culled
//    clusters with indexCount 0, one `drawIndexedIndirect` over all of them. Simple and read-back free, used by
//    the instanced field and the unit tests; its CPU cost grows with the cluster count.
//  * The scene path (`setSceneInstances` + `cullScene` + `drawSceneSlot`): every instance's cut is compacted
//    into a shared index ARENA (`MicromeshArena`), so every instance is ONE indexed draw and the memory is the
//    visible cut's, not the whole mesh's per object. The same dispatch handles every instance of the mesh, for
//    one view (the camera or a shadow cascade) at a time.
//
// The renderer binds its own pipeline and uniforms; the vertex streams are positions (slot 0), normals (1),
// UVs (2) and tangents (3), and the index buffer the clusters' global vertex indices.

#include "math/Mat4.hpp"
#include "math/Vec3.hpp"
#include "micromesh/Micromesh.hpp"
#include "rhi/CommandBuffer.hpp"
#include "rhi/RenderDevice.hpp"

#include <array>
#include <vector>

namespace Caffeine::Render {

/// The farthest-depth pyramid (HZB) of a depth buffer, in one storage buffer, for occlusion culling. Build it from
/// the PREVIOUS frame's depth, outside a render pass; the scene cull tests Micromesh clusters against it (a
/// single-phase test: a cluster hidden in last frame's picture may show for one frame at a new silhouette edge).
class MicromeshHzb {
public:
    MicromeshHzb() = default;
    ~MicromeshHzb() { shutdown(); }
    MicromeshHzb(const MicromeshHzb&) = delete;
    MicromeshHzb& operator=(const MicromeshHzb&) = delete;

    bool init(RHI::RenderDevice* device, u32 width, u32 height);
    void shutdown();
    bool ready() const { return m_buffer != nullptr && m_pipeline != nullptr; }
    u32 width() const { return m_width; }
    u32 height() const { return m_height; }
    u32 levels() const { return m_levels; }
    u32 levelOffset(u32 level) const { return level < m_levels ? m_offsets[level] : 0u; }
    RHI::Buffer* buffer() const { return m_buffer; }
    /// Builds every level from `depth` (a D32 texture of exactly width x height, sampled as 0 near .. 1 far).
    /// `sampler` is any sampler (the shader fetches texels). Outside a render pass.
    bool build(RHI::CommandBuffer* cmd, RHI::Texture* depth, RHI::Sampler* sampler);
    /// Test and tooling helper: the farthest depth of a level's texel (blocking read-back).
    f32 readTexel(u32 level, u32 x, u32 y);

private:
    RHI::RenderDevice* m_device = nullptr;
    RHI::ComputePipeline* m_pipeline = nullptr;
    RHI::Buffer* m_buffer = nullptr;
    u32 m_width = 0;
    u32 m_height = 0;
    u32 m_levels = 0;
    u32 m_offsets[16] = {};
};

/// What the occlusion test needs about the previous frame.
struct MicromeshOcclusion {
    const MicromeshHzb* hzb = nullptr;
    Mat4 prevView = Mat4::identity();
    f32 p00 = 1.0f, p11 = 1.0f, p22 = 0.0f, p23 = 0.0f;  ///< the previous projection's elements
};

struct MicromeshCullParams {
    /// Inside is dot(n, p) + w >= 0. Use `planesFromViewProj`.
    std::array<Vec4, 6> planes{};
    Vec3 camera;
    /// Error threshold in world units, or world units per metre of distance when `viewDependent`.
    f32 threshold = 0.0f;
    bool frustum = true;
    bool cone = true;
    bool viewDependent = true;
    /// Scene path, camera view only: test against the previous frame's HZB (instances opt in through
    /// `setSceneInstances`). Ignored when null.
    const MicromeshOcclusion* occlusion = nullptr;
};

/// Six frustum planes (left, right, bottom, top, near, far) of a clip-from-world matrix. The near
/// plane uses z >= -w, which is conservative for a 0..1 depth range too.
std::array<Vec4, 6> planesFromViewProj(const Mat4& viewProj);

/// The index buffer the scene path compacts every cut into. Index 0 holds the allocation counter, so draws
/// start at 1. One arena serves every mesh and every view of a frame; it must be `beginFrame`d once, before the
/// first cull of the frame, and is sized for the frame's total visible indices (a cut that does not fit is not
/// drawn that frame: `MicromeshGpu::readSceneView` reports it).
class MicromeshArena {
public:
    MicromeshArena() = default;
    ~MicromeshArena() { shutdown(); }
    MicromeshArena(const MicromeshArena&) = delete;
    MicromeshArena& operator=(const MicromeshArena&) = delete;

    bool init(RHI::RenderDevice* device, u32 capacityIndices);
    void shutdown();
    bool ready() const { return m_indices != nullptr; }
    u32 capacity() const { return m_capacity; }
    RHI::Buffer* indices() const { return m_indices; }
    /// Resets the allocation counter (a one-thread compute pass). Call outside a render pass.
    bool beginFrame(RHI::CommandBuffer* cmd);
    /// Test and tooling helper: indices allocated so far this frame (blocking read-back).
    u32 readUsed();

private:
    RHI::RenderDevice* m_device = nullptr;
    RHI::ComputePipeline* m_reset = nullptr;
    RHI::Buffer* m_indices = nullptr;
    u32 m_capacity = 0;
};

class MicromeshGpu {
public:
    MicromeshGpu() = default;
    ~MicromeshGpu() { shutdown(); }
    MicromeshGpu(const MicromeshGpu&) = delete;
    MicromeshGpu& operator=(const MicromeshGpu&) = delete;

    bool init(RHI::RenderDevice* device);
    void shutdown();
    bool ready() const { return m_pipeline != nullptr; }

    /// Uploads a hierarchy (replaces the previous one). Returns false when the device lacks what
    /// the path needs (indirect draw, storage buffers) or on an allocation failure.
    bool upload(const Micromesh::Data& data);
    void clear();
    bool hasMesh() const { return m_clusterCount > 0; }
    u32 clusterCount() const { return m_clusterCount; }
    u32 sourceTriangles() const { return m_sourceTriangles; }

    // ---- the per-cluster path

    /// Sets the instances (object-to-world, uniform scale). Without a call there is one instance with
    /// the identity transform. Returns false on an allocation failure or an empty list.
    bool setInstances(const std::vector<Mat4>& transforms);
    u32 instanceCount() const { return m_instanceCount; }
    /// Number of indirect draws (`instanceCount * clusterCount`).
    u32 drawCount() const { return m_clusterCount * m_instanceCount; }
    /// Graphics storage buffer for the vertex shader, four vec4 per instance: the first three rows
    /// of the matrix (`row.xyz` rotation and scale, `row.w` translation), then `(largest scale,
    /// 1 when the scale is uniform, 0, 0)`. With `g = gl_InstanceIndex`: instance `g / clusterCount`,
    /// cluster `g % clusterCount`.
    RHI::Buffer* instanceBuffer() const { return m_instancesGfx; }

    /// Dispatches the cull. Call outside a render pass, before `draw`.
    bool cull(RHI::CommandBuffer* cmd, const MicromeshCullParams& params);

    /// Binds the vertex and index buffers and issues the indirect draw (inside a render pass whose
    /// pipeline is already bound).
    void draw(RHI::CommandBuffer* cmd) const;
    /// The same, with the shading normals bound as a second vertex buffer (slot 1, location 1, Float3).
    void drawLit(RHI::CommandBuffer* cmd) const;
    /// The same, plus the texture coordinates as a third vertex buffer (slot 2, location 2, Float2).
    /// Needs a hierarchy built with UVs (`hasUvs`).
    void drawTextured(RHI::CommandBuffer* cmd) const;
    bool hasUvs() const { return m_uvs != nullptr; }
    bool hasSceneAttributes() const { return m_normals && m_uvs && m_tangents && m_colors; }

    /// Test and tooling helper: the draws the last cull selected, as `instance * clusterCount +
    /// cluster` (reads the arguments back, blocking). Call after the command buffer was submitted.
    std::vector<u32> readSelected();

    /// What the last cull kept, summed over every instance (same blocking read-back).
    struct Stats {
        u32 visibleClusters = 0;
        u64 triangles = 0;
    };
    Stats readStats();

    // ---- the scene path (compacted, arena)

    /// Sets the instances of the scene path (object-to-world; a non-uniform scale is allowed) and sizes the
    /// draw slots for `viewCount` views (the camera, shadow cascades, ...). Call before the frame's first cull
    /// (it may reallocate). Cheap when nothing changed.
    /// `occlusion` (optional, one per instance) opts each instance into the occlusion test.
    bool setSceneInstances(const std::vector<Mat4>& transforms, u32 viewCount,
                           const std::vector<u8>& occlusion = {});
    u32 sceneInstanceCount() const { return m_sceneInstances; }
    u32 sceneViewCount() const { return m_sceneViews; }
    /// The compacted cut of every instance for one view: reset, count, reserve, copy (four compute passes).
    /// Outside a render pass.
    bool cullScene(RHI::CommandBuffer* cmd, const MicromeshCullParams& params, MicromeshArena& arena, u32 view);
    /// One instance of one view as a single indexed draw with the scene streams (positions, normals, UVs,
    /// tangents) bound (inside a render pass, pipeline bound). `instanceBuffer()` is the vertex shader's matrix
    /// source.
    void drawSceneSlot(RHI::CommandBuffer* cmd, const MicromeshArena& arena, u32 view, u32 instance) const;
    /// Positions only (shadow cascades).
    void drawSceneSlotPositions(RHI::CommandBuffer* cmd, const MicromeshArena& arena, u32 view, u32 instance) const;
    /// What one view's cut kept over every instance, and whether any instance did not fit in the arena
    /// (blocking read-back).
    struct SceneViewStats {
        u32 visibleClusters = 0;
        u64 triangles = 0;
        u32 overflowed = 0;
    };
    SceneViewStats readSceneView(u32 view);

    RHI::Buffer* positions() const { return m_positions; }
    RHI::Buffer* normals() const { return m_normals; }
    RHI::Buffer* indices() const { return m_indices; }
    RHI::Buffer* arguments() const { return m_args; }

private:
    static constexpr u32 kSlotUints = 8;
    RHI::RenderDevice* m_device = nullptr;
    RHI::ComputePipeline* m_pipeline = nullptr;         ///< micromesh_cull.comp (per-cluster path)
    RHI::ComputePipeline* m_compactPipeline = nullptr;  ///< micromesh_compact.comp (scene path)
    RHI::Buffer* m_clusters = nullptr;
    RHI::Buffer* m_args = nullptr;
    RHI::Buffer* m_positions = nullptr;
    RHI::Buffer* m_normals = nullptr;
    RHI::Buffer* m_uvs = nullptr;
    RHI::Buffer* m_tangents = nullptr;
    RHI::Buffer* m_colors = nullptr;
    RHI::Buffer* m_indices = nullptr;
    RHI::Buffer* m_indicesStorage = nullptr;  ///< the same indices, compute-readable (scene path)
    RHI::Buffer* m_instances = nullptr;       ///< compute copy of the instance rows
    RHI::Buffer* m_instancesGfx = nullptr;    ///< graphics copy, bound by the vertex shader
    RHI::Buffer* m_sceneArgs = nullptr;       ///< viewCount x instances slots of kSlotUints
    std::vector<float> m_sceneRows;           ///< the rows last uploaded (to skip identical uploads)
    u32 m_clusterCount = 0;
    u32 m_instanceCount = 0;
    u32 m_sourceTriangles = 0;
    u32 m_sceneInstances = 0;
    u32 m_sceneViews = 0;
};

}  // namespace Caffeine::Render
