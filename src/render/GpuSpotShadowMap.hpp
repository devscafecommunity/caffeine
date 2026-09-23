#pragma once

#include "core/Types.hpp"
#include "math/Mat4.hpp"
#include "math/Vec3.hpp"
#include "rhi/RenderDevice.hpp"

namespace Caffeine::Render {

class GpuSpotShadowMap {
public:
    static constexpr u32 kMaxSpotShadowLights = 2;
    static constexpr u32 kResolution = 512;

    bool init(RHI::RenderDevice* device);
    void shutdown();

    bool isInitialized() const { return m_initialized; }

    RHI::Texture* texture(u32 slot) const;
    const Mat4& lightVP(u32 slot) const { return m_lightVP[slot]; }
    const Vec3& lightPosition(u32 slot) const { return m_lightPos[slot]; }
    f32 radius(u32 slot) const { return m_radius[slot]; }
    f32 cosHalfAngle(u32 slot) const { return m_cosHalfAngle[slot]; }
    bool valid(u32 slot) const { return slot < kMaxSpotShadowLights && m_valid[slot]; }

    void setSlot(u32 slot, const Mat4& lightVP, const Vec3& position, f32 radius,
                 f32 cosHalfAngle, bool isValid);
    void clearSlot(u32 slot);

private:
    RHI::RenderDevice* m_device = nullptr;
    RHI::Texture* m_textures[kMaxSpotShadowLights]{};
    Mat4 m_lightVP[kMaxSpotShadowLights]{};
    Vec3 m_lightPos[kMaxSpotShadowLights]{};
    f32 m_radius[kMaxSpotShadowLights]{};
    f32 m_cosHalfAngle[kMaxSpotShadowLights]{};
    bool m_valid[kMaxSpotShadowLights]{};
    bool m_initialized = false;
};

}  // namespace Caffeine::Render
