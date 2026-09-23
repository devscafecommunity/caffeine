#include "render/GpuSpotShadowMap.hpp"

namespace Caffeine::Render {

bool GpuSpotShadowMap::init(RHI::RenderDevice* device) {
    if (!device || !device->isInitialized()) {
        return false;
    }

    shutdown();
    m_device = device;

    for (u32 i = 0; i < kMaxSpotShadowLights; ++i) {
        RHI::TextureDesc desc;
        desc.width = kResolution;
        desc.height = kResolution;
        desc.format = RHI::TextureFormat::R16_FLOAT;
        desc.usage = RHI::TextureUsage::Sampler | RHI::TextureUsage::ColorTarget;
        desc.mipLevels = 1;
        m_textures[i] = m_device->createTexture(desc);
        if (!m_textures[i]) {
            shutdown();
            return false;
        }
        m_valid[i] = false;
        m_lightVP[i] = Mat4::identity();
    }

    m_initialized = true;
    return true;
}

void GpuSpotShadowMap::shutdown() {
    if (m_device) {
        for (u32 i = 0; i < kMaxSpotShadowLights; ++i) {
            if (m_textures[i]) {
                m_device->destroyTexture(m_textures[i]);
                m_textures[i] = nullptr;
            }
            m_valid[i] = false;
        }
    }
    m_device = nullptr;
    m_initialized = false;
}

RHI::Texture* GpuSpotShadowMap::texture(u32 slot) const {
    if (slot >= kMaxSpotShadowLights) return nullptr;
    return m_textures[slot];
}

void GpuSpotShadowMap::setSlot(u32 slot, const Mat4& lightVP, const Vec3& position, f32 radius,
                               f32 cosHalfAngle, bool isValid) {
    if (slot >= kMaxSpotShadowLights) return;
    m_lightVP[slot] = lightVP;
    m_lightPos[slot] = position;
    m_radius[slot] = radius;
    m_cosHalfAngle[slot] = cosHalfAngle;
    m_valid[slot] = isValid;
}

void GpuSpotShadowMap::clearSlot(u32 slot) {
    if (slot >= kMaxSpotShadowLights) return;
    m_valid[slot] = false;
}

}  // namespace Caffeine::Render
