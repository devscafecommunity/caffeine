#pragma once

#include "../core/Types.hpp"
#include <math.h>

namespace Caffeine {

struct Vec4 {
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 z = 0.0f;
    f32 w = 0.0f;

    Vec4() = default;
    Vec4(f32 x, f32 y, f32 z, f32 w) : x(x), y(y), z(z), w(w) {}
    Vec4(f32 val) : x(val), y(val), z(val), w(val) {}

    Vec4 operator+(const Vec4& v) const { return Vec4(x + v.x, y + v.y, z + v.z, w + v.w); }
    Vec4 operator-(const Vec4& v) const { return Vec4(x - v.x, y - v.y, z - v.z, w - v.w); }
    Vec4 operator*(f32 s) const { return Vec4(x * s, y * s, z * s, w * s); }
    Vec4 operator/(f32 s) const { return Vec4(x / s, y / s, z / s, w / s); }

    Vec4& operator+=(const Vec4& v) { x += v.x; y += v.y; z += v.z; w += v.w; return *this; }
    Vec4& operator-=(const Vec4& v) { x -= v.x; y -= v.y; z -= v.z; w -= v.w; return *this; }
    Vec4& operator*=(f32 s) { x *= s; y *= s; z *= s; w *= s; return *this; }
    Vec4& operator/=(f32 s) { x /= s; y /= s; z /= s; w /= s; return *this; }

    f32 dot(const Vec4& v) const { return x * v.x + y * v.y + z * v.z + w * v.w; }
    f32 lengthSquared() const { return dot(*this); }
    f32 length() const {
        f32 len = lengthSquared();
        return (len > 0.0f) ? sqrtf(len) : 0.0f;
    }
    Vec4 normalized() const {
        f32 len = length();
        return (len > 0.0f) ? *this / len : Vec4(0, 0, 0, 0);
    }

    static const Vec4& zero() { static Vec4 v(0, 0, 0, 0); return v; }
    static const Vec4& one() { static Vec4 v(1, 1, 1, 1); return v; }
};

inline Vec4 operator*(f32 s, const Vec4& v) { return v * s; }

} // namespace Caffeine