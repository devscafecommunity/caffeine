#include "assets/TextureMapGenerator.hpp"

#include "core/io/Crc32.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

#include <stb/stb_image.h>
#include <stb/stb_image_write.h>

namespace Caffeine::Assets {
namespace {

u8 toByte(f32 value) {
    return static_cast<u8>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
}

f32 luminance(const u8* pixel) {
    if (!pixel) return 0.0f;
    return (0.2126f * pixel[0] + 0.7152f * pixel[1] + 0.0722f * pixel[2]) / 255.0f;
}

u32 wrapIndex(i32 value, u32 size, bool wrap) {
    if (size == 0) return 0;
    if (wrap) {
        i32 wrapped = value % static_cast<i32>(size);
        return static_cast<u32>(wrapped < 0 ? wrapped + static_cast<i32>(size) : wrapped);
    }
    return static_cast<u32>(std::clamp(value, 0, static_cast<i32>(size) - 1));
}

f32 heightAt(const TextureImage& image, i32 x, i32 y, bool wrap) {
    const u32 px = wrapIndex(x, image.width, wrap);
    const u32 py = wrapIndex(y, image.height, wrap);
    const u8* pixel = image.pixel(px, py);
    return pixel ? pixel[0] / 255.0f : 0.0f;
}

f32 scalarAt(const TextureImage* image, u32 x, u32 y, f32 fallback) {
    const u8* pixel = image ? image->pixel(x, y) : nullptr;
    return pixel ? pixel[0] / 255.0f : fallback;
}

std::vector<u8> generateOcclusionPixels(const TextureImage& height, f32 strength, u32 radius,
                                        bool wrap) {
    std::vector<u8> pixels(static_cast<size_t>(height.width) * height.height * 4);
    const i32 maxRadius = static_cast<i32>(std::clamp(radius, 1u, 64u));
    const i32 innerRadius = std::max(1, maxRadius / 2);
    static constexpr i32 kDirections[8][2] = {
        {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1},
    };

    for (u32 y = 0; y < height.height; ++y) {
        for (u32 x = 0; x < height.width; ++x) {
            const f32 center = heightAt(height, static_cast<i32>(x), static_cast<i32>(y), wrap);
            f32 average = 0.0f;
            for (const auto& direction : kDirections) {
                average += heightAt(height,
                                    static_cast<i32>(x) + direction[0] * innerRadius,
                                    static_cast<i32>(y) + direction[1] * innerRadius, wrap);
            }
            average /= 8.0f;
            f32 outer = 0.0f;
            for (const auto& direction : kDirections) {
                outer += heightAt(height, static_cast<i32>(x) + direction[0] * maxRadius,
                                  static_cast<i32>(y) + direction[1] * maxRadius, wrap);
            }
            outer /= 8.0f;
            // Cavities are where the centre sits below the surrounding height
            // field. Comparing ring averages avoids the 1/distance explosion of
            // a raw horizon slope on noisy photo-derived heights.
            const f32 cavity = std::max(0.0f, std::max(average, outer) - center);
            const f32 visibility =
                1.0f - std::clamp(cavity * strength * 8.0f, 0.0f, 1.0f);
            const size_t index = (static_cast<size_t>(y) * height.width + x) * 4;
            pixels[index + 0] = toByte(visibility);
            pixels[index + 1] = toByte(visibility);
            pixels[index + 2] = toByte(visibility);
            pixels[index + 3] = 255;
        }
    }
    return pixels;
}

}  // namespace

bool loadTextureImage(const std::filesystem::path& path, TextureImage& out) {
    out = {};
    if (path.empty()) return false;
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
    if (!pixels || width < 1 || height < 1) {
        if (pixels) stbi_image_free(pixels);
        return false;
    }
    out.width = static_cast<u32>(width);
    out.height = static_cast<u32>(height);
    out.rgba.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
    stbi_image_free(pixels);
    out.valid = true;
    return true;
}

bool saveTextureImage(const std::filesystem::path& path, const TextureImage& image) {
    if (!image.valid || image.width < 1 || image.height < 1 || image.rgba.empty()) return false;
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    return stbi_write_png(path.string().c_str(), static_cast<int>(image.width),
                          static_cast<int>(image.height), 4, image.rgba.data(),
                          static_cast<int>(image.width) * 4) != 0;
}

bool saveTextureImage16(const std::filesystem::path& path, u32 width, u32 height,
                        const u16* rgba) {
    if (width < 1 || height < 1 || !rgba) return false;
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }

    auto put32 = [](std::vector<u8>& out, u32 value) {
        out.push_back(static_cast<u8>(value >> 24));
        out.push_back(static_cast<u8>(value >> 16));
        out.push_back(static_cast<u8>(value >> 8));
        out.push_back(static_cast<u8>(value));
    };
    auto put16 = [](std::vector<u8>& out, u16 value) {
        out.push_back(static_cast<u8>(value >> 8));
        out.push_back(static_cast<u8>(value));
    };
    auto chunk = [&](const char type[4], const std::vector<u8>& data) {
        std::vector<u8> result;
        put32(result, static_cast<u32>(data.size()));
        result.insert(result.end(), type, type + 4);
        result.insert(result.end(), data.begin(), data.end());
        u32 crc = Caffeine::IO::crc32(type, 4);
        crc = Caffeine::IO::crc32Continue(crc, data.data(), data.size());
        put32(result, crc);
        return result;
    };

    std::vector<u8> raw;
    const size_t rowBytes = static_cast<size_t>(width) * 8u;
    raw.reserve((rowBytes + 1) * height);
    for (u32 y = 0; y < height; ++y) {
        raw.push_back(0); // filter: none
        for (u32 x = 0; x < width; ++x) {
            const u16* pixel = rgba + (static_cast<size_t>(y) * width + x) * 4;
            for (int channel = 0; channel < 4; ++channel) {
                put16(raw, pixel[channel]);
            }
        }
    }

    // zlib stream with uncompressed deflate blocks; avoids a zlib dependency
    // while still producing a valid 16-bit PNG.
    std::vector<u8> zlib;
    zlib.push_back(0x78);
    zlib.push_back(0x01);
    size_t offset = 0;
    while (offset < raw.size()) {
        const size_t block = std::min<size_t>(65535, raw.size() - offset);
        const bool final = offset + block >= raw.size();
        zlib.push_back(final ? 1 : 0);
        // Stored-block LEN/NLEN are little-endian in the zlib stream.
        zlib.push_back(static_cast<u8>(block & 0xff));
        zlib.push_back(static_cast<u8>(block >> 8));
        const u16 inverse = static_cast<u16>(~block & 0xffff);
        zlib.push_back(static_cast<u8>(inverse & 0xff));
        zlib.push_back(static_cast<u8>(inverse >> 8));
        zlib.insert(zlib.end(), raw.begin() + offset, raw.begin() + offset + block);
        offset += block;
    }
    u32 a = 1;
    u32 b = 0;
    for (u8 byte : raw) {
        a = (a + byte) % 65521u;
        b = (b + a) % 65521u;
    }
    put32(zlib, (b << 16) | a);

    std::vector<u8> ihdr;
    put32(ihdr, width);
    put32(ihdr, height);
    ihdr.push_back(16); // bit depth
    ihdr.push_back(6);  // RGBA
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);

    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) return false;
    static const u8 signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    out.write(reinterpret_cast<const char*>(signature), 8);
    for (const auto& part : {chunk("IHDR", ihdr), chunk("IDAT", zlib), chunk("IEND", {})}) {
        out.write(reinterpret_cast<const char*>(part.data()),
                  static_cast<std::streamsize>(part.size()));
    }
    return static_cast<bool>(out);
}

TextureImage generateNormalFromHeight(const TextureImage& height, f32 strength, bool directX,
                                      bool wrap) {
    TextureImage out;
    if (!height.valid) return out;
    out.width = height.width;
    out.height = height.height;
    out.rgba.resize(static_cast<size_t>(out.width) * out.height * 4);
    out.valid = true;
    const f32 scale = std::max(strength, 0.0f);

    for (u32 y = 0; y < out.height; ++y) {
        for (u32 x = 0; x < out.width; ++x) {
            const i32 sx = static_cast<i32>(x);
            const i32 sy = static_cast<i32>(y);
            const f32 left =
                heightAt(height, sx - 1, sy - 1, wrap) + 2.0f * heightAt(height, sx - 1, sy, wrap) +
                heightAt(height, sx - 1, sy + 1, wrap);
            const f32 right =
                heightAt(height, sx + 1, sy - 1, wrap) + 2.0f * heightAt(height, sx + 1, sy, wrap) +
                heightAt(height, sx + 1, sy + 1, wrap);
            const f32 top =
                heightAt(height, sx - 1, sy - 1, wrap) + 2.0f * heightAt(height, sx, sy - 1, wrap) +
                heightAt(height, sx + 1, sy - 1, wrap);
            const f32 bottom =
                heightAt(height, sx - 1, sy + 1, wrap) + 2.0f * heightAt(height, sx, sy + 1, wrap) +
                heightAt(height, sx + 1, sy + 1, wrap);
            const f32 dx = (right - left) / 8.0f;
            const f32 dy = (bottom - top) / 8.0f;
            f32 nx = -dx * scale;
            f32 ny = -dy * scale;
            const f32 nz = 1.0f;
            const f32 length = std::sqrt(nx * nx + ny * ny + nz * nz);
            nx /= length;
            ny /= length;
            if (directX) ny = -ny;
            const size_t index = (static_cast<size_t>(y) * out.width + x) * 4;
            out.rgba[index + 0] = toByte(nx * 0.5f + 0.5f);
            out.rgba[index + 1] = toByte(ny * 0.5f + 0.5f);
            out.rgba[index + 2] = toByte(nz / length * 0.5f + 0.5f);
            out.rgba[index + 3] = 255;
        }
    }
    return out;
}

TextureImage generateAmbientOcclusion(const TextureImage& height, f32 strength, u32 radius,
                                      bool wrap) {
    TextureImage out;
    if (!height.valid) return out;
    out.width = height.width;
    out.height = height.height;
    out.rgba = generateOcclusionPixels(height, std::max(strength, 0.0f), radius, wrap);
    out.valid = true;
    return out;
}

TextureImage generateScalarFromAlbedo(const TextureImage& albedo, TextureMapKind kind,
                                      f32 minValue, f32 maxValue, bool invert) {
    TextureImage out;
    if (!albedo.valid) return out;
    out.width = albedo.width;
    out.height = albedo.height;
    out.rgba.resize(static_cast<size_t>(out.width) * out.height * 4);
    out.valid = true;
    const f32 low = std::min(minValue, maxValue);
    const f32 high = std::max(minValue, maxValue);
    const f32 span = std::max(high - low, 1e-5f);

    for (u32 y = 0; y < out.height; ++y) {
        for (u32 x = 0; x < out.width; ++x) {
            const u8* pixel = albedo.pixel(x, y);
            f32 value = luminance(pixel);
            if (kind == TextureMapKind::Height) {
                value = std::clamp(value, 0.0f, 1.0f);
            } else {
                value = std::clamp((value - low) / span, 0.0f, 1.0f);
                if (invert) value = 1.0f - value;
                if (kind == TextureMapKind::Metallic) {
                    // Albedo luminance is only a rough metal mask heuristic:
                    // keep dark organic surfaces dielectric and bright metallic
                    // paint near 1. Artists can replace it with a real mask.
                    value = std::clamp((value - 0.55f) / 0.4f, 0.0f, 1.0f);
                    value = value * value * (3.0f - 2.0f * value);
                }
            }
            const u8 encoded = toByte(value);
            const size_t index = (static_cast<size_t>(y) * out.width + x) * 4;
            out.rgba[index + 0] = encoded;
            out.rgba[index + 1] = encoded;
            out.rgba[index + 2] = encoded;
            out.rgba[index + 3] = 255;
        }
    }
    return out;
}

TextureImage packMaterialMap(const TextureImage* ao, const TextureImage* roughness,
                             const TextureImage* metallic, TexturePackKind kind) {
    const TextureImage* reference = ao ? ao : (roughness ? roughness : metallic);
    TextureImage out;
    if (!reference || !reference->valid) return out;
    out.width = reference->width;
    out.height = reference->height;
    out.rgba.resize(static_cast<size_t>(out.width) * out.height * 4);
    out.valid = true;

    for (u32 y = 0; y < out.height; ++y) {
        for (u32 x = 0; x < out.width; ++x) {
            const f32 aoValue = scalarAt(ao, x, y, 1.0f);
            const f32 roughnessValue = scalarAt(roughness, x, y, 0.5f);
            const f32 metallicValue = scalarAt(metallic, x, y, 0.0f);
            const f32 smoothness = 1.0f - roughnessValue;
            u8 r = 0;
            u8 g = 0;
            u8 b = 0;
            u8 a = 255;
            switch (kind) {
                case TexturePackKind::Orm:
                    r = toByte(aoValue);
                    g = toByte(roughnessValue);
                    b = toByte(metallicValue);
                    break;
                case TexturePackKind::UnityHdrpMask:
                    r = toByte(metallicValue);
                    g = toByte(aoValue);
                    b = 0;
                    a = toByte(smoothness);
                    break;
                case TexturePackKind::UnityMetallicSmoothness:
                    r = toByte(metallicValue);
                    g = 0;
                    b = 0;
                    a = toByte(smoothness);
                    break;
            }
            const size_t index = (static_cast<size_t>(y) * out.width + x) * 4;
            out.rgba[index + 0] = r;
            out.rgba[index + 1] = g;
            out.rgba[index + 2] = b;
            out.rgba[index + 3] = a;
        }
    }
    return out;
}

}  // namespace Caffeine::Assets
