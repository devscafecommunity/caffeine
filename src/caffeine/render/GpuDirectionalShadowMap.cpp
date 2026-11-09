#include "render/GpuDirectionalShadowMap.hpp"

#include <algorithm>

namespace Caffeine::Render {

bool GpuDirectionalShadowMap::init(RHI::RenderDevice* device) {
    if (!device || !device->isInitialized()) {
        return false;
    }

    shutdown();
    m_device = device;

    for (u32 i = 0; i < kMaxDirectionalShadowLights; ++i) {
        RHI::TextureDesc desc;
        desc.width = kAtlasResolution;
        desc.height = kAtlasResolution;
        desc.format = RHI::TextureFormat::R16_FLOAT;
        desc.usage = RHI::TextureUsage::Sampler | RHI::TextureUsage::ColorTarget;
        desc.mipLevels = 1;
        m_textures[i] = m_device->createTexture(desc);
        if (!m_textures[i]) {
            shutdown();
            return false;
        }
        m_valid[i] = false;
        m_cascadeCount[i] = 1;
        for (u32 c = 0; c < kMaxCascades; ++c) {
            m_cascadeVP[i][c] = Mat4::identity();
        }
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
            m_cascadeCount[i] = 1;
        }
    }
    m_device = nullptr;
    m_initialized = false;
}

RHI::Texture* GpuDirectionalShadowMap::texture(u32 slot) const {
    if (slot >= kMaxDirectionalShadowLights) return nullptr;
    return m_textures[slot];
}

const Mat4& GpuDirectionalShadowMap::cascadeVP(u32 slot, u32 cascade) const {
    if (slot >= kMaxDirectionalShadowLights || cascade >= kMaxCascades) {
        static const Mat4 kIdentity = Mat4::identity();
        return kIdentity;
    }
    return m_cascadeVP[slot][cascade];
}

u32 GpuDirectionalShadowMap::cascadeCount(u32 slot) const {
    if (slot >= kMaxDirectionalShadowLights) return 1;
    return std::max(1u, m_cascadeCount[slot]);
}

const f32* GpuDirectionalShadowMap::cascadeSplits(u32 slot) const {
    if (slot >= kMaxDirectionalShadowLights) return m_cascadeSplits[0];
    return m_cascadeSplits[slot];
}

void GpuDirectionalShadowMap::setCascadeData(u32 slot, u32 cascadeCount, const Mat4* vps,
                                             const f32* splits, bool isValid) {
    if (slot >= kMaxDirectionalShadowLights) return;
    m_cascadeCount[slot] = std::clamp(cascadeCount, 1u, kMaxCascades);
    for (u32 c = 0; c < kMaxCascades; ++c) {
        m_cascadeVP[slot][c] = (vps && c < m_cascadeCount[slot]) ? vps[c] : Mat4::identity();
    }
    if (splits) {
        for (u32 i = 0; i <= kMaxCascades; ++i) {
            m_cascadeSplits[slot][i] = splits[i];
        }
    }
    m_valid[slot] = isValid;
}

void GpuDirectionalShadowMap::clearSlot(u32 slot) {
    if (slot >= kMaxDirectionalShadowLights) return;
    m_valid[slot] = false;
    m_cascadeCount[slot] = 1;
}

}  // namespace Caffeine::Render
