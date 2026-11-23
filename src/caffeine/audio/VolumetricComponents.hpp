#pragma once

#include "core/Types.hpp"

namespace Caffeine::Audio {

/// Marks an entity as something sound cannot simply pass through: its box (the entity's transform applied to a unit cube,
/// so a scaled and turned cube is a wall) blocks, muffles and reflects sound for the volumetric audio.
struct AcousticObstacle {
    bool enabled = true;
    /// 0 Concrete, 1 Brick, 2 Wood, 3 Glass, 4 Foliage, 5 Curtain. Ignored when `custom` is on.
    u8 material = 0;
    bool custom = false;
    u8 _pad = 0;
    /// Custom material: the share of the lows and of the highs that gets through 20 cm, and how much it reflects.
    f32 transmissionLow = 0.1f;
    f32 transmissionHigh = 0.01f;
    f32 reflectivity = 0.85f;
};

/// The scene's volumetric audio settings (the first enabled one is used). Without the component, or switched off, a scene
/// keeps the plain audio.
struct VolumetricAudioComponent {
    bool enabled = true;
    bool occlusion = true;
    bool diffraction = true;
    bool reverb = true;
    bool airAbsorption = true;
    bool propagationDelay = true;
    u8 _pad[2] = {};
    f32 masterVolume = 1.0f;
    f32 worldVolume = 1.0f;    ///< layer volumes
    f32 ambientVolume = 1.0f;
    f32 musicVolume = 1.0f;
    f32 dopplerScale = 1.0f;   ///< 0 = no Doppler, 1 = real
    f32 speedOfSound = 343.0f;
};

}  // namespace Caffeine::Audio
