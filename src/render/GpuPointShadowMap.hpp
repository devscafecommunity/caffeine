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

    void clearSlot(u32 slot);

private:
    RHI::RenderDevice* m_device = nullptr;
    RHI::Texture* m_cubemaps[kMaxPointShadowLights] = {};
    bool m_initialized = false;
};

struct GpuPointShadowPass {
    Vec3 lightPosition;
    f32  radius = 10.0f;
    u32  shadowSlot = 0;
};

}  // namespace Caffeine::Render
