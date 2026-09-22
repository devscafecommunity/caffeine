#pragma once

#include "../core/Types.hpp"
#include "../math/Mat4.hpp"
#include "../math/Vec3.hpp"
#include "../rhi/RenderDevice.hpp"
#include "../rhi/CommandBuffer.hpp"

namespace Caffeine::Render {

class GpuPointShadowMap {
public:
    static constexpr u32 kFaceSize = 256;
    static constexpr u32 kMaxPointShadowLights = 2;

    bool init(RHI::RenderDevice* device);
    void shutdown();

    bool isReady() const { return m_initialized; }
    RHI::Texture* cubemap(u32 slot) const;
    const Vec3& lightPosition(u32 slot) const { return m_lightPos[slot]; }
    f32 radius(u32 slot) const { return m_radius[slot]; }
    bool valid(u32 slot) const { return slot < kMaxPointShadowLights && m_valid[slot]; }

    void setSlot(u32 slot, const Vec3& position, f32 radius, bool isValid);
    void clearSlot(u32 slot);

private:
    RHI::RenderDevice* m_device = nullptr;
    RHI::Texture* m_cubemaps[kMaxPointShadowLights] = {};
    Vec3 m_lightPos[kMaxPointShadowLights]{};
    f32 m_radius[kMaxPointShadowLights]{};
    bool m_valid[kMaxPointShadowLights]{};
    bool m_initialized = false;
};

struct GpuPointShadowPass {
    Vec3 lightPosition;
    f32  radius = 10.0f;
    u32  shadowSlot = 0;
};

}  // namespace Caffeine::Render
