#pragma once

#include "core/Types.hpp"

namespace Caffeine::ECS {

/// The radiance Photon uses where a ray finds no surface (linear RGB, before `skyIntensity`).
/// The compute passes, the screen-space fallback and the weather coupling share these three numbers.
inline constexpr f32 kPhotonSkyRadiance[3] = {0.48f, 0.60f, 0.82f};

/// Photon quality tiers (spec §13 / §14.4).
///
/// The component is serialised as raw POD bytes, so the legacy numbering
/// (1 = Low, 2 = Balanced, 3 = High) is preserved verbatim and the new Eco
/// tier is appended as 4. Existing scenes therefore keep their exact tier.
/// Iterate `kPhotonQualityOrder` when building UI lists (tier order, not the
/// numeric order).
enum class PhotonQuality : u8 {
    Off      = 0,
    Low      = 1,
    Balanced = 2,
    High     = 3,
    Eco      = 4,
};

/// UI / preset order: Off, Eco, Low, Balanced, High.
inline constexpr PhotonQuality kPhotonQualityOrder[] = {
    PhotonQuality::Off,
    PhotonQuality::Eco,
    PhotonQuality::Low,
    PhotonQuality::Balanced,
    PhotonQuality::High,
};
inline constexpr u32 kPhotonQualityCount =
    static_cast<u32>(sizeof(kPhotonQualityOrder) / sizeof(kPhotonQualityOrder[0]));

inline constexpr const char* photonQualityName(PhotonQuality quality) {
    switch (quality) {
        case PhotonQuality::Off:      return "Off";
        case PhotonQuality::Eco:      return "Eco";
        case PhotonQuality::Low:      return "Low";
        case PhotonQuality::Balanced: return "Balanced";
        case PhotonQuality::High:     return "High";
    }
    return "Off";
}

/// Budget of the current screen-space fallback for one tier. The compute
/// radiance-cascade path (P2+) uses the spec's s0 / K / N0 table instead; this
/// helper keeps the editor and the fallback renderer from drifting apart in
/// the meantime.
struct PhotonFallbackBudget {
    u8 rays  = 0;
    u8 steps = 0;
};

inline constexpr PhotonFallbackBudget photonFallbackBudget(PhotonQuality quality) {
    switch (quality) {
        case PhotonQuality::Eco:      return {3, 1};
        case PhotonQuality::Low:      return {4, 2};
        case PhotonQuality::Balanced: return {8, 2};
        case PhotonQuality::High:     return {12, 3};
        case PhotonQuality::Off:
        default:                      return {0, 0};
    }
}

/// Which Photon implementation renders this view.
///
///   ScreenFallback: the v0.1 per-pixel gather (always available).
///   ComputeC0:      P2 milestone — one compute pass tracing 16 world-aligned
///                   octahedral directions per probe, SH-L1 resolve, far field
///                   limited to the sky. Cascades k >= 1 and the merge arrive
///                   in P3; the far field arrives in P4.
///
/// Appended as a raw byte like `PhotonQuality`, so old scenes keep working.
enum class PhotonPath : u8 {
    ScreenFallback = 0,
    ComputeC0      = 1,
};

inline constexpr const char* photonPathName(PhotonPath path) {
    switch (path) {
        case PhotonPath::ComputeC0:      return "Compute C0 (experimental)";
        case PhotonPath::ScreenFallback:
        default:                          return "Screen fallback";
    }
}

/// Photon — hybrid GI settings.
///
/// `path` selects the implementation:
///   * ScreenFallback — the v0.1 per-pixel gather (depth-only, near field).
///   * ComputeC0      — P3/P4/P5: K cascades with fused trace+merge, an SH-L1
///                      C0 resolve, a four-level voxel/probe far field with one
///                      injection bounce, and S(t-1) multi-bounce feedback.
///
/// The quality tiers are the single source of truth shared by the component,
/// the editor panels, the serializer and the renderer.
struct PhotonGIComponent {
    bool enabled = false;
    PhotonQuality quality = PhotonQuality::Balanced;
    /// Number of cascades K (1..6). C0 plus K-1 stored M̄k levels.
    u8 cascades = 5;
    /// Indirect-light multiplier.
    f32 intensity = 1.0f;
    /// Near-field reach in world units at the anchor depth.
    f32 radius = 14.0f;
    /// Relative depth thickness used to accept a screen hit.
    f32 thickness = 0.08f;
    /// Sky/ambient fallback strength when a ray leaves the screen.
    f32 skyIntensity = 0.65f;
    /// Extra ambient bounce added to the fallback.
    f32 ambientIntensity = 0.20f;
    /// Albedo approximation applied to the gathered radiance.
    f32 tintR = 0.82f;
    f32 tintG = 0.82f;
    f32 tintB = 0.82f;
    /// 0 = lit scene, 1 = GI only, 2 = fallback only.
    u8 debugView = 0;
    u8 castShadows = 1;
    u8 _pad[2] = {};
    /// Implementation used by this view (see PhotonPath).
    PhotonPath path = PhotonPath::ScreenFallback;
    /// C0 probe spacing in pixels (spec presets: Eco 16, Low 8, Balanced 4, High 2).
    u8 s0 = 4;
    /// March steps used by the interim C0 kernel (1..32).
    u8 computeSteps = 16;
    u8 _pad2[1] = {};
    // ---- Appended after the first scenes were written: a scene without them keeps these defaults.
    /// Scales the light the voxels hold (the sun, the lights and the sky ambient injected into them), so
    /// the bounce between surfaces can be strengthened without touching the direct light or `intensity`.
    f32 bounceGain = 1.0f;
    /// Which geometry the triangle voxeliser puts in the material volume (opaque meshes always are).
    bool voxelSkinned = true;   ///< skinned meshes, in their current pose (revoxelised where they move)
    bool voxelCutout = false;   ///< cut-out meshes (foliage cards) as solid: they then shade like solid blocks
    bool voxelTerrain = true;   ///< terrain, from its mesh
    /// Rebuild every level whenever anything changes, instead of only the voxels around what moved
    /// (a diagnostic: the result is the same, the cost is not).
    bool fullRebuild = false;
};

}  // namespace Caffeine::ECS
