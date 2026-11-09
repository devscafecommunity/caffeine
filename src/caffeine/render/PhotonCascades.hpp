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
//  P4 far field: four voxel clipmap levels (0.25/0.5/1/2 m cells), 16³ probes
//  per level, six-face radiance injection with probe SH, Chebyshev queries and
//  the l → l+1 blend. The material volume is filled by the screened G-buffer
//  fallback (§8.2); the conservative triangle voxeliser is an accuracy upgrade.
//  Dispatch order per frame: Min-Z, voxel fill, injection, probe trace, then
//  k = K-1 … 1, then C0, then the diffuse resolve.
//
//  KNOWN GAPS (tracked here because the engine tree has no Markdown docs):
//   - Conservative triangle voxeliser + dirty-slab re-voxelisation (§8.2
//     reference path). The current fill is the depth-accumulation fallback.
//   - E_dir from the host lights/shadows. Injection currently uses a
//     hemispherical sky ambient (π·0.5·sky).
//   - S4/S5 GPU validation. The CPU seam metric lives in
//     include/caffeine/photon/PhotonFarField.hpp.
//
//  P5 multi-bounce: the resolve writes S(t) = indirect irradiance/π (× the
//  intensity knob) into a per-view RGBA16F buffer; the cascade/C0 hit shading
//  adds rho_fb·min(S_prev, S_cap) with S_cap = 4 and rho_fb <= 0.95. History is
//  reprojected with the previous view-projection (no motion vectors yet) and
//  invalidated on camera cuts, resize and the S validity channel.
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
                 const ECS::PhotonGIComponent& settings, u32 frameIndex,
                 RHI::Texture* sPrev, const Mat4& prevViewProj, bool sValid);

    /// 2×2 RGBA16F block per C0 probe; sampled by post_photon_resolve.frag.
    RHI::Texture* shAtlas() const { return m_shAtlas; }
    u32 gridWidth() const { return m_gridW; }
    u32 gridHeight() const { return m_gridH; }
    u32 probeSpacing() const { return m_s0; }
    u32 cascadeCount() const { return m_cascadeCount; }

private:
    bool ensureTargets(RHI::RenderDevice* device, u32 width, u32 height, u32 s0, u32 cascadeCount);
    void releaseTargets(RHI::RenderDevice* device);
    bool ensureFarTargets(RHI::RenderDevice* device);
    void releaseFarTargets(RHI::RenderDevice* device);
    /// Screened G-buffer voxel fill (level 0, spec §8.2 fallback).
    bool dispatchVoxelize(RHI::CommandBuffer* cmd, RHI::Texture* depth, RHI::Texture* albedo,
                          const Mat4& view, const Mat4& proj, u32 width, u32 height);
    /// Level-0 probe trace into the radiance/moment atlases.
    bool dispatchProbes(RHI::CommandBuffer* cmd, u32 frameIndex, f32 skyIntensity);
    bool dispatchInject(RHI::CommandBuffer* cmd, f32 skyIntensity);
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
    RHI::ComputePipeline* m_voxelizePipeline = nullptr;
    RHI::ComputePipeline* m_probePipeline    = nullptr;
    RHI::ComputePipeline* m_injectPipeline   = nullptr;
    RHI::Sampler*         m_point           = nullptr;
    RHI::Sampler*         m_linear          = nullptr;
    RHI::Texture*         m_shAtlas         = nullptr;
    RHI::Texture*         m_dummyUpper      = nullptr;
    /// Level-0 far-field resources.
    RHI::Texture*         m_voxelAlbedo     = nullptr;  // 512² RGBA8 (rgb albedo, a coverage)
    RHI::Texture*         m_probeRadiance   = nullptr;  // current 160×2560 RGBA16F
    RHI::Texture*         m_probeMoments    = nullptr;  // current 160×2560 RGBA16F (rg = moments)
    RHI::Texture*         m_probeRadiancePrev = nullptr;
    RHI::Texture*         m_probeMomentsPrev  = nullptr;
    RHI::Texture*         m_probeSH         = nullptr;  // 32×512 RGBA16F (2×2 per probe)
    /// Six voxel faces stored as two texels per voxel along each axis.
    RHI::Texture*         m_voxelRadiance[3] = {nullptr, nullptr, nullptr};
    bool                  m_farCleared      = false;
    bool                  m_farEnabled      = false;
    Vec3                  m_farClipOrigin[4]{};
    f32                   m_farCellSize[4]     = {0.25f, 0.5f, 1.0f, 2.0f};
    Vec3                  m_farProbeOrigin[4]{};
    f32                   m_farProbeSpacing[4] = {1.0f, 2.0f, 4.0f, 8.0f};
    /// Per-frame G-buffer inputs (valid only inside execute()).
    RHI::Texture*         m_direct          = nullptr;
    RHI::Texture*         m_depth           = nullptr;
    RHI::Texture*         m_albedo          = nullptr;
    RHI::Texture*         m_normalMaterial  = nullptr;
    /// P5 feedback S(t-1) and the previous frame's view-projection.
    RHI::Texture*         m_sPrev           = nullptr;
    Mat4                  m_prevViewProj    = Mat4::identity();
    bool                  m_sValid          = false;
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
