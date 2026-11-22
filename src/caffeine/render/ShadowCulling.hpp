#pragma once

#include "math/Vec3.hpp"
#include "math/Vec4.hpp"

#include <array>

namespace Caffeine::Render {

/// True when the box lies wholly outside one of the four side planes (left, right, bottom, top, in that order, as
/// `planesFromViewProj` builds them: normalised, inside is positive) of an orthographic shadow cascade. Only the
/// sides: the near and far planes depend on how the cascade pads its depth range toward the light, and a caster
/// outside the sides is clipped by the viewport anyway, so skipping it changes no pixel.
inline bool outsideCascadeSides(const std::array<Vec4, 6>& planes, const Vec3& boxMin, const Vec3& boxMax) {
    for (int i = 0; i < 4; ++i) {
        const Vec4& p = planes[static_cast<size_t>(i)];
        const Vec3 far(p.x >= 0.0f ? boxMax.x : boxMin.x, p.y >= 0.0f ? boxMax.y : boxMin.y,
                       p.z >= 0.0f ? boxMax.z : boxMin.z);
        if (p.x * far.x + p.y * far.y + p.z * far.z + p.w < 0.0f) return true;
    }
    return false;
}

}  // namespace Caffeine::Render
