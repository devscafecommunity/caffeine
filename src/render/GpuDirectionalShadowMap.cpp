#include "render/GpuDirectionalShadowMap.hpp"

namespace Caffeine::Render {

bool GpuDirectionalShadowMap::init(RHI::RenderDevice* device) {
    if (!device || !device->isInitialized()) {
        return false;
    }

    shutdown();
    m_device = device;

    for (u32 i = 0; i < kMaxDirectionalShadowLights; ++i) {
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

void GpuDirectionalShadowMap::shutdown() {
    if (m_device) {
        for (u32 i = 0; i < kMaxDirectionalShadowLights; ++i) {
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

RHI::Texture* GpuDirectionalShadowMap::texture(u32 slot) const {
    if (slot >= kMaxDirectionalShadowLights) return nullptr;
    return m_textures[slot];
}

void GpuDirectionalShadowMap::setSlot(u32 slot, const Mat4& lightVP, bool isValid) {
    if (slot >= kMaxDirectionalShadowLights) return;
    m_lightVP[slot] = lightVP;
    m_valid[slot] = isValid;
}

}  // namespace Caffeine::Render
