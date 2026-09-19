#pragma once

namespace Caffeine::Assets {

// Paths are relative to the engine assets root (e.g. assets/).
inline constexpr const char* kHdrGroundPresetPaths[] = {
    "hdr-assets-texture/Ground051_1K-JPG-mud/Ground051_1K-JPG_Color.jpg",
    "hdr-assets-texture/Ground055L_1K-JPG-sand/Ground055L_1K-JPG_Color.jpg",
    "hdr-assets-texture/Ground058_1K-JPG-rocks/Ground058_1K-JPG_Color.jpg",
    "hdr-assets-texture/Ground068_1K-JPG-dirt-rock-moss/Ground068_1K-JPG_Color.jpg",
    "hdr-assets-texture/Ground079L_1K-JPG-dirt/Ground079L_1K-JPG_Color.jpg",
    "hdr-assets-texture/Ground108_1K-JPG-dirt-with-rocks/Ground108_1K-JPG_Color.jpg",
};

inline constexpr const char* kHdrGroundPresetLabels[] = {
    "Ground 051 (Mud)",
    "Ground 055L (Sand)",
    "Ground 058 (Rocks)",
    "Ground 068 (Dirt / rock / moss)",
    "Ground 079L (Dirt)",
    "Ground 108 (Dirt with rocks)",
};

inline constexpr int kHdrGroundPresetCount =
    static_cast<int>(sizeof(kHdrGroundPresetPaths) / sizeof(kHdrGroundPresetPaths[0]));

inline constexpr const char* kUltraRealisticSplatLayers[] = {
    kHdrGroundPresetPaths[3], // moss / vegetated
    kHdrGroundPresetPaths[5], // dirt with rocks
    kHdrGroundPresetPaths[1], // sand
    kHdrGroundPresetPaths[2], // rocks
};

inline constexpr const char* kUltraRealisticAlbedo = kHdrGroundPresetPaths[4]; // dirt
inline constexpr const char* kUltraRealisticMud = kHdrGroundPresetPaths[0];

}  // namespace Caffeine::Assets
