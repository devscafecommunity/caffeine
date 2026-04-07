#pragma once

#include "../core/Types.hpp"
#include "Vec2.hpp"
#include "Vec3.hpp"
#include "Vec4.hpp"
#include "Mat4.hpp"
#include <math.h>

namespace Caffeine {

namespace Math {

constexpr f32 PI = 3.14159265358979323846f;
constexpr f32 TAU = 2.0f * PI;
constexpr f32 E = 2.71828182845904523536f;

constexpr f32 degToRad(f32 degrees) { return degrees * (PI / 180.0f); }
constexpr f32 radToDeg(f32 radians) { return radians * (180.0f / PI); }

inline f32 clamp(f32 value, f32 min, f32 max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

inline f32 saturate(f32 value) { return clamp(value, 0.0f, 1.0f); }

inline f32 lerp(f32 a, f32 b, f32 t) { return a + (b - a) * t; }

inline f32 inverseLerp(f32 a, f32 b, f32 value) {
    return (a != b) ? (value - a) / (b - a) : 0.0f;
}

inline f32 smoothstep(f32 edge0, f32 edge1, f32 x) {
    f32 t = saturate(inverseLerp(edge0, edge1, x));
    return t * t * (3.0f - 2.0f * t);
}

inline f32 moveTowards(f32 current, f32 target, f32 maxDelta) {
    f32 diff = target - current;
    if (abs(diff) <= maxDelta) return target;
    return current + (diff > 0 ? maxDelta : -maxDelta);
}

inline f32 absf(f32 value) { return value < 0 ? -value : value; }
inline f32 minf(f32 a, f32 b) { return a < b ? a : b; }
inline f32 maxf(f32 a, f32 b) { return a > b ? a : b; }

inline f32 floorf(f32 value) { return floorf(value); }
inline f32 ceilf(f32 value) { return ceilf(value); }
inline f32 roundf(f32 value) { return roundf(value); }

inline f32 sqrtf_safe(f32 value) {
    return (value > 0.0f) ? sqrtf(value) : 0.0f;
}

inline f32 powf(f32 base, f32 exp) { return powf(base, exp); }

inline bool approximatelyEqual(f32 a, f32 b, f32 epsilon = 0.00001f) {
    return absf(a - b) < epsilon;
}

inline bool isPowerOfTwo(usize value) {
    return (value & (value - 1)) == 0;
}

inline usize nextPowerOfTwo(usize value) {
    if (value == 0) return 1;
    --value;
    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    value |= value >> 8;
    value |= value >> 16;
    return value + 1;
}

} // namespace Math

} // namespace Caffeine