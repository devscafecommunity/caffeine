#pragma once

#include "math/Vec3.hpp"

#include <cmath>

namespace Caffeine::Editor {

/// Offset from orbit focus to camera position (positive pitch = camera above focus).
inline Vec3 editorOrbitOffset(f32 yaw, f32 pitch, f32 distance) {
    const f32 sinY = std::sin(yaw);
    const f32 cosY = std::cos(yaw);
    const f32 sinP = std::sin(pitch);
    const f32 cosP = std::cos(pitch);
    return Vec3(sinY * cosP, sinP, -cosY * cosP) * distance;
}

/// Direction from camera toward orbit focus.
inline Vec3 editorLookDirection(f32 yaw, f32 pitch) {
    const f32 sinY = std::sin(yaw);
    const f32 cosY = std::cos(yaw);
    const f32 sinP = std::sin(pitch);
    const f32 cosP = std::cos(pitch);
    return Vec3(-sinY * cosP, -sinP, cosY * cosP);
}

inline Vec3 editorCameraPosition(f32 yaw, f32 pitch, f32 distance, const Vec3& focus) {
    return focus + editorOrbitOffset(yaw, pitch, distance);
}

}  // namespace Caffeine::Editor
