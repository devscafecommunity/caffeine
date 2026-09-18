#pragma once

namespace Caffeine::Assets {

// Paths are relative to the engine assets root (e.g. assets/).
inline constexpr const char* kHdrGroundPresetPaths[] = {
    "hdr-assets-texture/Ground051_1K-JPG/Ground051_1K-JPG_Color.jpg",
    "hdr-assets-texture/Ground055L_1K-JPG/Ground055L_1K-JPG_Color.jpg",
    "hdr-assets-texture/Ground058_1K-JPG/Ground058_1K-JPG_Color.jpg",
    "hdr-assets-texture/Ground068_1K-JPG/Ground068_1K-JPG_Color.jpg",
    "hdr-assets-texture/Ground079L_1K-JPG/Ground079L_1K-JPG_Color.jpg",
    "hdr-assets-texture/Ground108_1K-JPG/Ground108_1K-JPG_Color.jpg",
};

inline constexpr const char* kHdrGroundPresetLabels[] = {
    "Ground 051 (Dirt)",
    "Ground 055L (Dry)",
    "Ground 058 (Moss)",
    "Ground 068 (Gravel)",
    "Ground 079L (Sand)",
    "Ground 108 (Rock)",
};

inline constexpr int kHdrGroundPresetCount =
    static_cast<int>(sizeof(kHdrGroundPresetPaths) / sizeof(kHdrGroundPresetPaths[0]));

}  // namespace Caffeine::Assets
