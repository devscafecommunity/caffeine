#pragma once

#include "core/Types.hpp"
#include "math/Vec3.hpp"
#include "rhi/RenderDevice.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace Caffeine::Render {

struct EnvironmentMipLevel {
    u32 width = 0;
    u32 height = 0;
    std::vector<u8> rgba;
};

/// Box-filtered equirect mip chain. Level 0 is at most `maxWidth` wide; the chain stops once a
/// level is `minWidth` wide so the last level still carries a directional sky/ground gradient.
std::vector<EnvironmentMipLevel> buildEnvironmentMipChain(const u8* rgba, u32 width, u32 height,
                                                          u32 maxWidth = 1024, u32 minWidth = 4);

/// Mean RGB (0..1) of one mip level.
Vec3 averageEnvironmentColor(const EnvironmentMipLevel& level);

/// Equirect UV for a world direction; matches SkyboxRenderer's mapping.
inline void directionToEnvironmentUV(const Vec3& dir, f32& u, f32& v);

/// Sky texture used by the scene renderer for IBL and reflection fallback.
class GpuEnvironmentMap {
public:
    /// Loads `path` once; repeated calls with the same path are free. Empty path releases.
    bool load(RHI::RenderDevice* device, const std::string& path);
    void release(RHI::RenderDevice* device);

    RHI::Texture* texture() const { return m_texture; }
    u32 mipCount() const { return m_mipCount; }
    const Vec3& averageColor() const { return m_average; }
    const std::string& path() const { return m_path; }

private:
    RHI::Texture* m_texture = nullptr;
    u32 m_mipCount = 0;
    Vec3 m_average{};
    std::string m_path;
};

inline void directionToEnvironmentUV(const Vec3& dir, f32& u, f32& v) {
    constexpr f32 kPi = 3.14159265f;
    const f32 len = dir.length();
    const Vec3 d = len > 1e-6f ? dir / len : Vec3(0.0f, 0.0f, 1.0f);
    u = std::atan2(d.x, d.z) / (2.0f * kPi) + 0.5f;
    v = 0.5f - std::asin(std::clamp(d.y, -1.0f, 1.0f)) / kPi;
}

}  // namespace Caffeine::Render
