#pragma once

#include "core/Types.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace Caffeine::Assets {

/// Scalar/colour maps the editor can derive from a source texture.
enum class TextureMapKind : u8 {
    Normal = 0,
    AmbientOcclusion = 1,
    Roughness = 2,
    Metallic = 3,
    Height = 4,
};

/// Engine-ready channel packings.
enum class TexturePackKind : u8 {
    /// glTF / Unreal / Godot: R = AO, G = roughness, B = metallic.
    Orm = 0,
    /// Unity HDRP Mask Map: R = metallic, G = AO, B = detail mask, A = smoothness.
    UnityHdrpMask = 1,
    /// Unity Standard: R = metallic, G/B = 0, A = smoothness.
    UnityMetallicSmoothness = 2,
};

struct TextureImage {
    u32 width = 0;
    u32 height = 0;
    /// RGBA8, row-major, top-left origin.
    std::vector<u8> rgba;
    bool valid = false;

    const u8* pixel(u32 x, u32 y) const {
        if (!valid || x >= width || y >= height) return nullptr;
        return rgba.data() + (static_cast<size_t>(y) * width + x) * 4;
    }
};

/// Loads PNG/JPEG/TGA/BMP/PSD/GIF/HDR through stb_image, expanded to RGBA8.
bool loadTextureImage(const std::filesystem::path& path, TextureImage& out);
/// Writes RGBA8 as PNG, creating missing parent directories.
bool saveTextureImage(const std::filesystem::path& path, const TextureImage& image);
/// Writes RGBA16 as a 16-bit PNG (big-endian channels on disk).
bool saveTextureImage16(const std::filesystem::path& path, u32 width, u32 height,
                        const u16* rgba);

/// Tangent-space normal from a height map (Sobel.) `directX` flips the green
/// channel; `wrap` samples the height field toroidally so it tiles.
TextureImage generateNormalFromHeight(const TextureImage& height, f32 strength, bool directX,
                                     bool wrap);
/// Multi-direction horizon occlusion from a height map. `radius` is in pixels
/// (1..64); higher radius catches broader cavities.
TextureImage generateAmbientOcclusion(const TextureImage& height, f32 strength, u32 radius,
                                      bool wrap);
/// Roughness/metallic/height convenience maps derived from a colour texture.
/// `minValue`..`maxValue` map the source luminance; `invert` flips the result.
TextureImage generateScalarFromAlbedo(const TextureImage& albedo, TextureMapKind kind,
                                      f32 minValue, f32 maxValue, bool invert);
/// Packs scalar or colour sources into an engine-ready map. Missing sources use
/// neutral values (AO 1, roughness 0.5, metallic 0).
TextureImage packMaterialMap(const TextureImage* ao, const TextureImage* roughness,
                             const TextureImage* metallic, TexturePackKind kind);

}  // namespace Caffeine::Assets
