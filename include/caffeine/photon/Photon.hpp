#pragma once

#include "core/Types.hpp"

namespace Caffeine::ECS {

/// Photon v0.1 — hybrid screen-space radiance-cascade GI.
///
/// The engine traces a small number of exponentially growing distance
/// intervals against the depth buffer, merges them front-to-back and falls
/// back to a sky/ambient term when a ray leaves the screen. This is the
/// near-field half of the phot specification; the far-field probe field is
/// approximated by the sky/ambient fallback until a compute backend exists.
struct PhotonGIComponent {
    bool enabled = false;
    /// 0 = off, 1 = low, 2 = balanced, 3 = high.
    u8 quality = 2;
    /// Number of exponentially growing distance intervals (1..6).
    u8 cascades = 4;
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
};

}  // namespace Caffeine::ECS
