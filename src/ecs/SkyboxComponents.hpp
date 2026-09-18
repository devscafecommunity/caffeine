#pragma once

#include "core/Types.hpp"

namespace Caffeine::ECS {

inline constexpr const char* kSkyboxPresetFiles[] = {
    "skybox-day.png",
    "skybox-night.png",
    "skybox-morning.png",
    "skybox-space.png",
    "skybox-alien.png",
};
inline constexpr int kSkyboxPresetCount =
    static_cast<int>(sizeof(kSkyboxPresetFiles) / sizeof(kSkyboxPresetFiles[0]));

inline constexpr const char* kSkyboxPresetLabels[] = {
    "Day", "Night", "Morning", "Space", "Alien",
};

struct SkyboxComponent {
    bool enabled = true;
    int  presetIndex = 0;
    f32  exposure = 1.0f;
    char customTexturePath[256] = {};
};

}  // namespace Caffeine::ECS
