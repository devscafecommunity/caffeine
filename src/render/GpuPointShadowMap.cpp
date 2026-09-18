#include "render/GpuPointShadowMap.hpp"

namespace Caffeine::Render {

bool GpuPointShadowMap::init(RHI::RenderDevice* device) {
    if (!device || !device->isInitialized()) {
        return false;
    }

    shutdown();
    m_device = device;

    for (u32 i = 0; i < kMaxPointShadowLights; ++i) {
        RHI::TextureDesc desc;
        desc.width = kFaceSize;
        desc.height = kFaceSize;
        desc.format = RHI::TextureFormat::R16_FLOAT;
        desc.usage = RHI::TextureUsage::Sampler | RHI::TextureUsage::ColorTarget;
        desc.type = RHI::TextureType::Cube;
        desc.mipLevels = 1;
        m_cubemaps[i] = m_device->createTexture(desc);
        if (!m_cubemaps[i]) {
            shutdown();
            return false;
        }
    }

    m_initialized = true;
    return true;
}

void GpuPointShadowMap::shutdown() {
    if (m_device) {
        for (u32 i = 0; i < kMaxPointShadowLights; ++i) {
            if (m_cubemaps[i]) {
                m_device->destroyTexture(m_cubemaps[i]);
                m_cubemaps[i] = nullptr;
            }
        }
    }
    m_device = nullptr;
    m_initialized = false;
}

RHI::Texture* GpuPointShadowMap::cubemap(u32 slot) const {
    if (slot >= kMaxPointShadowLights) return nullptr;
    return m_cubemaps[slot];
}

void GpuPointShadowMap::clearSlot(u32 slot) {
    (void)slot;
}

}  // namespace Caffeine::Render
