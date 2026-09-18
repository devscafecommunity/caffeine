#pragma once

#include "core/Types.hpp"
#include "math/Mat4.hpp"
#include "math/Vec3.hpp"
#include "rhi/RenderDevice.hpp"

namespace Caffeine::Render {

class GpuDirectionalShadowMap {
public:
    static constexpr u32 kMaxDirectionalShadowLights = 2;
    static constexpr u32 kResolution = 512;

    bool init(RHI::RenderDevice* device);
    void shutdown();

    bool isInitialized() const { return m_initialized; }

    RHI::Texture* texture(u32 slot) const;
    const Mat4& lightVP(u32 slot) const { return m_lightVP[slot]; }
    bool valid(u32 slot) const { return slot < kMaxDirectionalShadowLights && m_valid[slot]; }

    void setSlot(u32 slot, const Mat4& lightVP, bool isValid);

private:
    RHI::RenderDevice* m_device = nullptr;
    RHI::Texture* m_textures[kMaxDirectionalShadowLights]{};
    Mat4 m_lightVP[kMaxDirectionalShadowLights]{};
    bool m_valid[kMaxDirectionalShadowLights]{};
    bool m_initialized = false;
};

}  // namespace Caffeine::Render
