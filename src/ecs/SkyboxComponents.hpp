#pragma once

#include "core/Types.hpp"

namespace Caffeine::ECS {

// Paths are relative to the engine assets root (e.g. assets/).
inline constexpr const char* kSkyboxPresetFiles[] = {
    "kenney_skyboxes/Skyboxes/skybox-day.png",
    "kenney_skyboxes/Skyboxes/skybox-night.png",
    "kenney_skyboxes/Skyboxes/skybox-morning.png",
    "kenney_skyboxes/Skyboxes/skybox-space.png",
    "kenney_skyboxes/Skyboxes/skybox-alien.png",
    "hdr-assets-texture/DaySkyHDRI027B_1K/DaySkyHDRI027B_1K_TONEMAPPED.jpg",
    "hdr-assets-texture/DaySkyHDRI067B_1K/DaySkyHDRI067B_1K_TONEMAPPED.jpg",
    "hdr-assets-texture/DaySkyHDRI068B_1K/DaySkyHDRI068B_1K_TONEMAPPED.jpg",
    "hdr-assets-texture/DaySkyHDRI069B_1K/DaySkyHDRI069B_1K_TONEMAPPED.jpg",
    "hdr-assets-texture/DaySkyHDRI070B_1K/DaySkyHDRI070B_1K_TONEMAPPED.jpg",
    "hdr-assets-texture/EveningSkyHDRI046B_1K/EveningSkyHDRI046B_1K_TONEMAPPED.jpg",
    "hdr-assets-texture/NightSkyHDRI003_1K/NightSkyHDRI003_1K_TONEMAPPED.jpg",
    "hdr-assets-texture/NightSkyHDRI008_1K/NightSkyHDRI008_1K_TONEMAPPED.jpg",
    "hdr-assets-texture/NightSkyHDRI014_1K/NightSkyHDRI014_1K_TONEMAPPED.jpg",
};
inline constexpr int kSkyboxPresetCount =
    static_cast<int>(sizeof(kSkyboxPresetFiles) / sizeof(kSkyboxPresetFiles[0]));

inline constexpr const char* kSkyboxPresetLabels[] = {
    "Kenney Day",
    "Kenney Night",
    "Kenney Morning",
    "Kenney Space",
    "Kenney Alien",
    "HDR Day 027",
    "HDR Day 067",
    "HDR Day 068",
    "HDR Day 069",
    "HDR Day 070",
    "HDR Evening 046",
    "HDR Night 003",
    "HDR Night 008",
    "HDR Night 014",
};

struct SkyboxComponent {
    bool enabled = true;
    int  presetIndex = 0;
    f32  exposure = 1.0f;
    char customTexturePath[256] = {};
};

}  // namespace Caffeine::ECS
