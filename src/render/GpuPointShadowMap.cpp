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
        m_valid[i] = false;
        m_lightPos[i] = Vec3(0.0f, 0.0f, 0.0f);
        m_radius[i] = 0.0f;
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
            m_valid[i] = false;
        }
    }
    m_device = nullptr;
    m_initialized = false;
}

RHI::Texture* GpuPointShadowMap::cubemap(u32 slot) const {
    if (slot >= kMaxPointShadowLights) return nullptr;
    return m_cubemaps[slot];
}

void GpuPointShadowMap::setSlot(u32 slot, const Vec3& position, f32 radius, bool isValid) {
    if (slot >= kMaxPointShadowLights) return;
    m_lightPos[slot] = position;
    m_radius[slot] = radius;
    m_valid[slot] = isValid;
}

void GpuPointShadowMap::clearSlot(u32 slot) {
    setSlot(slot, Vec3(0.0f, 0.0f, 0.0f), 0.0f, false);
}

}  // namespace Caffeine::Render
