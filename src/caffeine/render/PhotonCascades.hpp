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
//   - Dirty-slab re-voxelisation (§8.2 reference path). The conservative triangle
//     voxeliser exists (PhotonTriangle, updateGeometryVoxels) and rebuilds a level
//     when its clipmap origin moves or the scene's geometry hash changes; the
//     screened fill still adds what the camera sees (terrain, skinned meshes).
//   - E_dir is injected from the host lights (sun, point, spot) with a shadow
//     marched through the material volume, and the sky ambient is scaled by the
//     sky visibility of each face; the lights' own shadow maps are not used, so a
//     thin occluder smaller than a voxel does not shade.
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

#include <array>
#include <vector>

namespace Caffeine::RHI {
class RenderDevice;
class CommandBuffer;
struct Texture;
struct Buffer;
struct ComputePipeline;
struct Sampler;
}  // namespace Caffeine::RHI

namespace Caffeine::Render {

/// One world-space triangle of the geometry the far-field material volume is built from, as the
/// triangle voxeliser reads it (`photon_voxelize_tri.comp`): the three corners, then the albedo.
struct PhotonTriangle {
    f32 p0[4];      ///< xyz, unused
    f32 p1[4];
    f32 p2[4];
    f32 albedo[4];  ///< linear rgb, unused
};
static_assert(sizeof(PhotonTriangle) == 64, "PhotonTriangle must be four vec4");

/// One object of the geometry set: the triangles of one entity (or one material slot of it), with a hash of
/// everything that shapes them, so an object that did not change is not voxelised again.
struct PhotonObject {
    u64  key = 0;    ///< entity id and slot: stable while the object lives
    u64  hash = 0;   ///< mesh, transform, pose, colour: changes when its triangles do
    u32  first = 0;  ///< range in the triangle list
    u32  count = 0;
    Vec3 lo{};       ///< world bounds
    Vec3 hi{};
};

/// The scene geometry the triangle voxeliser reads.
struct PhotonGeometry {
    std::vector<PhotonObject>   objects;
    std::vector<PhotonTriangle> triangles;
    /// More triangles than the voxeliser takes: the list is empty and the screened fill works alone.
    bool overBudget = false;
};

/// A world box to revoxelise (where an object was, where it is).
struct PhotonBox {
    Vec3 lo{};
    Vec3 hi{};
};

/// What the voxeliser did, for the Photon panel and the tests (all counters are totals since init).
struct PhotonStats {
    u32 objects = 0;
    u32 triangles = 0;
    u32 overBudget = 0;
    u32 farEnabled = 0;
    u32 fullRebuilds = 0;     ///< levels rebuilt whole (the clipmap moved, or the scene changed a lot)
    u32 regionRebuilds = 0;   ///< boxes revoxelised (an object moved)
    u32 lastLevelMask = 0;    ///< levels rebuilt whole in the latest update
    u32 lastRegions = 0;      ///< boxes revoxelised in the latest update
    f32 lastUpdateMs = 0.0f;  ///< CPU time to encode the latest update
    u32 targetRebuilds = 0;   ///< times the per-size targets (atlases, Min-Z chain) were created again
    u32 farSkipped = 0;       ///< frames the far field was left as it was: nothing it depends on had changed
    u32 farHalfRate = 0;      ///< frames skipped because the camera only turned and the scene was calm
};

/// The scene's direct lights as the radiance injection reads them: each light's radiance is its colour
/// (linear) times its intensity, the same product the forward shader multiplies by N.L.
struct PhotonLights {
    static constexpr u32 kMaxDirectional = 2;
    static constexpr u32 kMaxPoint = 8;
    static constexpr u32 kMaxSpot = 4;
    u32 directionalCount = 0;
    u32 pointCount = 0;
    u32 spotCount = 0;
    Vec4 directionalDir[kMaxDirectional];    ///< xyz: the way the light travels
    Vec4 directionalColor[kMaxDirectional];  ///< rgb radiance
    Vec4 pointPosition[kMaxPoint];           ///< xyz position, w radius
    Vec4 pointColor[kMaxPoint];              ///< rgb radiance
    Vec4 spotPosition[kMaxSpot];             ///< xyz position, w radius
    Vec4 spotDirection[kMaxSpot];            ///< xyz the way it points, w cos(half angle)
    Vec4 spotColor[kMaxSpot];                ///< rgb radiance
    /// The clouds' shadow on the first directional light (the sun), traced analytically per voxel like the forward
    /// shaders do: x strength (0 = none), y coverage, z density / xyz wind offset of the noise, w base altitude /
    /// xyz direction toward the sun, w top altitude. All zero = the sun is not shaded.
    Vec4 cloudShadow[3];
};

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

    /// The scene geometry for the triangle voxeliser. `dirty` are the world boxes the change touched (where
    /// the objects that changed were and are): only the voxels inside them are cleared and revoxelised, so a
    /// moving object costs its own neighbourhood and not the volume. `full` (the first set, a changed
    /// object list the caller cannot box, or too many boxes) rebuilds every level. An empty list is a valid
    /// (empty) scene.
    bool setGeometry(const PhotonGeometry& geometry, const std::vector<PhotonBox>& dirty, bool full);
    /// A whole set as one object, rebuilt in full (tests and tools).
    bool setGeometry(u64 hash, const std::vector<PhotonTriangle>& triangles);
    /// Rebuilds every level at the next update (the Photon panel's *Rebuild voxels*).
    void requestFullRebuild() { ++m_geometryRevision; m_pendingBoxes.clear(); }
    const PhotonStats& stats() const { return m_stats; }
    /// True once a geometry set was given (and until the device is shut down): the caller resends it when not.
    bool hasGeometry() const { return m_geometryValid; }
    /// Brings the voxel volume up to date with the geometry for a camera at `cameraPos`: every level
    /// whose clipmap origin moved (or whose geometry changed) is cleared and voxelised again. Called by
    /// execute(); public so the voxeliser can be tested on its own. Must be outside a render pass.
    bool updateGeometryVoxels(RHI::CommandBuffer* cmd, const Vec3& cameraPos);
    /// The scene's direct lights, injected into the voxels as E_dir with a shadow marched through the
    /// material volume (so the light a wall gets from the sun bounces onto what the camera sees).
    void setLights(const PhotonLights& lights) { m_lights = lights; }
    /// The weight the present frame gets in the temporal averages (the probes' hysteresis follows it): high while
    /// the scene changes, low when it is still.
    void setTemporalWeight(f32 weight) { m_temporalWeight = weight; }
    /// Colour the sky radiance is multiplied by (weather: overcast greys it, fog tints it). 1,1,1 = unchanged.
    void setSkyTint(const Vec3& tint) { m_skyTint = tint; }
    /// Whether probes outside the camera's view are left untraced (on by default; off for diagnostics and tests).
    void setProbeCulling(bool on) { m_probeCulling = on; }
    /// Empties the far field once (voxels, radiance, probes). Called by execute(); public for tests.
    void clearFarTargets(RHI::CommandBuffer* cmd);
    /// Injects the lights and sky into the voxels' faces (the pass execute() runs before the probes);
    /// public so the injection can be tested on its own.
    bool injectRadiance(RHI::CommandBuffer* cmd, f32 skyIntensity) { return dispatchInject(cmd, skyIntensity); }
    /// Traces the far-field probes of every level once (public for tests; execute() runs it per frame).
    bool traceProbes(RHI::CommandBuffer* cmd, u32 frameIndex, f32 skyIntensity) {
        return dispatchProbes(cmd, frameIndex, skyIntensity);
    }
    /// The probe radiance atlas written by the latest traceProbes() (RGBA16F, 640 x 2560).
    RHI::Texture* probeRadiance() const { return m_probeRadiancePrev; }
    /// The probe lattice origin and spacing of a level.
    Vec3 farProbeOrigin(u32 level) const { return m_farProbeOrigin[level & 3u]; }
    f32 farProbeSpacing(u32 level) const { return m_farProbeSpacing[level & 3u]; }
    /// The three face-radiance volumes (x, y, z axis), RGBA16F: two texels per voxel along the axis.
    RHI::Texture* voxelRadiance(u32 axis) const { return m_voxelRadiance[axis % 3u]; }
    /// True when the triangle voxeliser's pipeline exists.
    bool hasTriangleVoxelizer() const { return m_triPipeline != nullptr; }
    /// The material volume (RGBA8 atlas, four levels side by side); for tests and tools.
    RHI::Texture* voxelAlbedo() const { return m_voxelAlbedo; }
    /// The clipmap origin and cell size a level currently uses.
    Vec3 farClipOrigin(u32 level) const { return m_farClipOrigin[level & 3u]; }
    f32 farCellSize(u32 level) const { return m_farCellSize[level & 3u]; }

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
    RHI::ComputePipeline* m_triPipeline      = nullptr;
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
    PhotonLights          m_lights;
    /// Triangle voxeliser: the scene's triangles on the GPU and what each level was built from.
    RHI::Buffer*          m_triBuffer       = nullptr;
    u32                   m_triCapacity     = 0;
    u32                   m_triCount        = 0;
    bool                  m_geometryValid   = false;
    u32                   m_geometryRevision = 1;
    std::vector<PhotonBox> m_pendingBoxes;
    PhotonStats           m_stats;
    f32                   m_temporalWeight = 0.15f;
    Vec3                  m_skyTint{1.0f, 1.0f, 1.0f};
    Mat4                  m_viewProj = Mat4::identity();  ///< of the frame being built (probe culling)
    bool                  m_probeCulling = true;          ///< skip far probes outside the camera's view
    Mat4                  m_lastView = Mat4::identity();  ///< the previous frame's view matrix
    bool                  m_cameraMoved = true;           ///< the view changed since the previous frame
    /// A cascade's atlas holds a frame's result (so a frame that skips the cascade may keep reading it).
    std::array<bool, 8>   m_atlasesValid{};
    f32                   m_bounceGain = 1.0f;  ///< PhotonGIComponent::bounceGain of the last frame
    u32                   m_levelRevision[4]{};
    Vec3                  m_levelOrigin[4]{};
    bool                  m_levelBuilt[4]{false, false, false, false};
    bool                  m_farCleared      = false;
    /// What the far field was last built from, and for how many frames in a row it was the same: once the temporal
    /// averages have converged a still scene has nothing left to compute, so the three far-field passes are skipped.
    u64                   m_farSignature    = 0;
    u32                   m_farStill        = 0;
    u32                   m_farRebuildsSeen = 0;
    bool                  m_farFieldValid   = false;
    /// The clipmaps' origins last frame and whether the previous frame traced: while the camera only turns (the
    /// origins stay put) and the scene is calm, the far field is traced every other frame.
    u64                   m_farInputSignature = 0;
    u64                   m_farPlacementSignature = 0;
    u32                   m_farInputStill   = 0;
    bool                  m_farTracedLast   = false;
    u32                   m_farHalfRate     = 0;
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
