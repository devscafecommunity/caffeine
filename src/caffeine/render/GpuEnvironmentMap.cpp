#include "render/GpuEnvironmentMap.hpp"

#include <stb/stb_image.h>

#include <algorithm>
#include <cmath>

namespace Caffeine::Render {
namespace {

EnvironmentMipLevel halveLevel(const EnvironmentMipLevel& src) {
    EnvironmentMipLevel dst;
    dst.width = std::max(1u, src.width / 2);
    dst.height = std::max(1u, src.height / 2);
    dst.rgba.resize(static_cast<size_t>(dst.width) * dst.height * 4);
    for (u32 y = 0; y < dst.height; ++y) {
        const u32 y0 = std::min(y * 2, src.height - 1);
        const u32 y1 = std::min(y * 2 + 1, src.height - 1);
        for (u32 x = 0; x < dst.width; ++x) {
            // Equirect wraps horizontally, so odd widths borrow the first column.
            const u32 x0 = (x * 2) % src.width;
            const u32 x1 = (x * 2 + 1) % src.width;
            const u8* p00 = &src.rgba[(static_cast<size_t>(y0) * src.width + x0) * 4];
            const u8* p01 = &src.rgba[(static_cast<size_t>(y0) * src.width + x1) * 4];
            const u8* p10 = &src.rgba[(static_cast<size_t>(y1) * src.width + x0) * 4];
            const u8* p11 = &src.rgba[(static_cast<size_t>(y1) * src.width + x1) * 4];
            u8* out = &dst.rgba[(static_cast<size_t>(y) * dst.width + x) * 4];
            for (int c = 0; c < 4; ++c) {
                const u32 sum = static_cast<u32>(p00[c]) + p01[c] + p10[c] + p11[c];
                out[c] = static_cast<u8>((sum + 2) / 4);
            }
        }
    }
    return dst;
}

}  // namespace

std::vector<EnvironmentMipLevel> buildEnvironmentMipChain(const u8* rgba, u32 width, u32 height,
                                                          u32 maxWidth, u32 minWidth) {
    std::vector<EnvironmentMipLevel> chain;
    if (!rgba || width == 0 || height == 0) return chain;

    EnvironmentMipLevel level;
    level.width = width;
    level.height = height;
    level.rgba.assign(rgba, rgba + static_cast<size_t>(width) * height * 4);
    while (level.width > std::max(maxWidth, 1u)) {
        level = halveLevel(level);
    }

    chain.push_back(std::move(level));
    while (chain.back().width > std::max(minWidth, 1u) && chain.back().height > 1) {
        chain.push_back(halveLevel(chain.back()));
    }
    return chain;
}

Vec3 averageEnvironmentColor(const EnvironmentMipLevel& level) {
    const size_t pixels = static_cast<size_t>(level.width) * level.height;
    if (pixels == 0 || level.rgba.size() < pixels * 4) return Vec3(0.0f, 0.0f, 0.0f);
    f64 sum[3] = {0.0, 0.0, 0.0};
    for (size_t i = 0; i < pixels; ++i) {
        for (int c = 0; c < 3; ++c) sum[c] += level.rgba[i * 4 + c];
    }
    const f64 scale = 1.0 / (255.0 * static_cast<f64>(pixels));
    return Vec3(static_cast<f32>(sum[0] * scale), static_cast<f32>(sum[1] * scale),
                static_cast<f32>(sum[2] * scale));
}

bool GpuEnvironmentMap::load(RHI::RenderDevice* device, const std::string& path) {
    if (path == m_path) return m_texture != nullptr;
    release(device);
    m_path = path;
    if (!device || path.empty()) return false;

    int width = 0;
    int height = 0;
    int channels = 0;
    u8* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!pixels || width <= 0 || height <= 0) {
        if (pixels) stbi_image_free(pixels);
        return false;
    }
    const std::vector<EnvironmentMipLevel> chain =
        buildEnvironmentMipChain(pixels, static_cast<u32>(width), static_cast<u32>(height), 4096);
    stbi_image_free(pixels);
    if (chain.empty()) return false;

    RHI::TextureDesc desc;
    desc.width = chain.front().width;
    desc.height = chain.front().height;
    // Sky images are sRGB; the sampler decodes them so lighting happens in linear space.
    desc.format = RHI::TextureFormat::R8G8B8A8_UNORM_SRGB;
    desc.usage = RHI::TextureUsage::Sampler;
    desc.mipLevels = static_cast<u32>(chain.size());
    m_texture = device->createTexture(desc);
    if (!m_texture) return false;

    for (u32 mip = 0; mip < chain.size(); ++mip) {
        const EnvironmentMipLevel& level = chain[mip];
        if (!device->uploadTexture(m_texture, level.rgba.data(), level.width, level.height, 4, mip)) {
            device->destroyTexture(m_texture);
            m_texture = nullptr;
            return false;
        }
    }
    m_mipCount = static_cast<u32>(chain.size());
    const Vec3 average = averageEnvironmentColor(chain.back());
    auto toLinear = [](f32 c) {
        return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
    };
    m_average = Vec3(toLinear(average.x), toLinear(average.y), toLinear(average.z));
    return true;
}

void GpuEnvironmentMap::release(RHI::RenderDevice* device) {
    if (m_texture && device) device->destroyTexture(m_texture);
    m_texture = nullptr;
    m_mipCount = 0;
    m_average = Vec3(0.0f, 0.0f, 0.0f);
    m_path.clear();
}

}  // namespace Caffeine::Render
