// ============================================================================
// @file    PhotonCascades.hpp
// @brief   Photon compute path — P3: cascades K-1..0 with fused trace+merge
// @note    Part of render/.
//
//  Cascade k (k = 0 .. K-1):
//    * n_k = 4·2^k world-aligned octahedral directions;
//    * probe spacing s0·2^k pixels, interval [t_k, t_{k+1}],
//      t_k = ℓ0(4^k − 1)/3, ℓ0 = κ·s0·u(z);
//    * k >= 1 stores the pre-averaged M̄k atlas: one thread per (probe, parent
//      direction), four child rays, geometry-aware upper lookup into M̄_{k+1};
//    * k == 0 projects its 16 directions to SH-L1.
//
//  The far field is still sky-only (the voxel clipmap + probe field arrive in
//  P4). Dispatch order per frame: Min-Z, then k = K-1 … 1, then C0, then the
//  diffuse resolve in post_photon_resolve.frag.
//
//  P3 keeps a single scratch instance (atlases + Min-Z chain), so two views
//  with different sizes would recreate it every frame. The main 3D view is the
//  intended consumer; P4 makes this per-view when the far field lands.
// ============================================================================
#pragma once

#include "core/Types.hpp"
#include "caffeine/photon/Photon.hpp"
#include "math/Mat4.hpp"
#include "render/MinZPyramid.hpp"

#include <vector>

namespace Caffeine::RHI {
class RenderDevice;
class CommandBuffer;
struct Texture;
struct ComputePipeline;
struct Sampler;
}  // namespace Caffeine::RHI

namespace Caffeine::Render {

class PhotonCascades {
public:
    PhotonCascades() = default;
    ~PhotonCascades() = default;

    PhotonCascades(const PhotonCascades&) = delete;
    PhotonCascades& operator=(const PhotonCascades&) = delete;

    /// Creates both compute pipelines and the sampler. Returns false when the
    /// shader bytecode is unavailable for this backend; the screen fallback
    /// stays usable.
    bool init(RHI::RenderDevice* device);
    void shutdown(RHI::RenderDevice* device);
    bool ready() const;

    /// Builds Min-Z, dispatches cascades K-1 … 1 and cascade 0. Must be called
    /// outside a render pass. Returns false when the path is unavailable.
    /// `direct` is D_t (direct diffuse + emissive) from the G-buffer.
    bool execute(RHI::CommandBuffer* cmd, RHI::Texture* depth, RHI::Texture* albedo,
                 RHI::Texture* normalMaterial, RHI::Texture* direct, const Mat4& view,
                 const Mat4& proj, u32 width, u32 height,
                 const ECS::PhotonGIComponent& settings, u32 frameIndex);

    /// 2×2 RGBA16F block per C0 probe; sampled by post_photon_resolve.frag.
    RHI::Texture* shAtlas() const { return m_shAtlas; }
    u32 gridWidth() const { return m_gridW; }
    u32 gridHeight() const { return m_gridH; }
    u32 probeSpacing() const { return m_s0; }
    u32 cascadeCount() const { return m_cascadeCount; }

private:
    bool ensureTargets(RHI::RenderDevice* device, u32 width, u32 height, u32 s0, u32 cascadeCount);
    void releaseTargets(RHI::RenderDevice* device);
    bool dispatchCascade(RHI::CommandBuffer* cmd, u32 cascade, RHI::Texture* upper,
                         RHI::Texture* target, const Mat4& view, const Mat4& proj,
                         u32 width, u32 height, const ECS::PhotonGIComponent& settings,
                         f32 uMin, const Vec4& jitter, f32 skyIntensity);
    bool dispatchC0(RHI::CommandBuffer* cmd, RHI::Texture* upper, const Mat4& view,
                    const Mat4& proj, u32 width, u32 height,
                    const ECS::PhotonGIComponent& settings, f32 uMin, const Vec4& jitter,
                    f32 skyIntensity);

    RHI::RenderDevice*    m_device          = nullptr;
    RHI::ComputePipeline* m_cascadePipeline = nullptr;
    RHI::ComputePipeline* m_c0Pipeline      = nullptr;
    RHI::Sampler*         m_point           = nullptr;
    RHI::Texture*         m_shAtlas         = nullptr;
    RHI::Texture*         m_dummyUpper      = nullptr;
    /// Per-frame G-buffer inputs (valid only inside execute()).
    RHI::Texture*         m_direct          = nullptr;
    RHI::Texture*         m_depth           = nullptr;
    RHI::Texture*         m_albedo          = nullptr;
    RHI::Texture*         m_normalMaterial  = nullptr;
    /// M̄k atlases indexed by cascade (1 .. K-1); index 0 is unused.
    std::vector<RHI::Texture*> m_atlases;
    MinZPyramid m_minZ;
    u32 m_width  = 0;
    u32 m_height = 0;
    u32 m_s0     = 0;
    u32 m_cascadeCount = 0;
    u32 m_gridW  = 0;   // G0
    u32 m_gridH  = 0;   // G0
    u32 m_atlasW = 0;   // 2·G0
    u32 m_atlasH = 0;   // 2·G0
};

}  // namespace Caffeine::Render
