#pragma once

#include "core/Types.hpp"
#include "math/Mat4.hpp"
#include "math/Vec3.hpp"
#include "rhi/RenderDevice.hpp"

namespace Caffeine::Render {

class GpuDirectionalShadowMap {
public:
    static constexpr u32 kMaxDirectionalShadowLights = 2;
    static constexpr u32 kMaxCascades = 4;
    static constexpr u32 kCascadeResolution = 512;
    static constexpr u32 kAtlasResolution = 1024;

    bool init(RHI::RenderDevice* device);
    void shutdown();

    bool isInitialized() const { return m_initialized; }

    RHI::Texture* texture(u32 slot) const;
    const Mat4& cascadeVP(u32 slot, u32 cascade) const;
    u32 cascadeCount(u32 slot) const;
    const f32* cascadeSplits(u32 slot) const;
    bool valid(u32 slot) const { return slot < kMaxDirectionalShadowLights && m_valid[slot]; }

    void setCascadeData(u32 slot, u32 cascadeCount, const Mat4* vps, const f32* splits,
                        bool isValid);
    void clearSlot(u32 slot);

private:
    RHI::RenderDevice* m_device = nullptr;
    RHI::Texture* m_textures[kMaxDirectionalShadowLights]{};
    Mat4 m_cascadeVP[kMaxDirectionalShadowLights][kMaxCascades]{};
    f32 m_cascadeSplits[kMaxDirectionalShadowLights][kMaxCascades + 1]{};
    u32 m_cascadeCount[kMaxDirectionalShadowLights]{};
    bool m_valid[kMaxDirectionalShadowLights]{};
    bool m_initialized = false;
};

}  // namespace Caffeine::Render
