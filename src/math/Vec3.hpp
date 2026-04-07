#pragma once

#include "../core/Types.hpp"
#include <math.h>

namespace Caffeine {

struct Vec3 {
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 z = 0.0f;

    Vec3() = default;
    Vec3(f32 x, f32 y, f32 z) : x(x), y(y), z(z) {}
    Vec3(f32 val) : x(val), y(val), z(val) {}

    Vec3 operator+(const Vec3& v) const { return Vec3(x + v.x, y + v.y, z + v.z); }
    Vec3 operator-(const Vec3& v) const { return Vec3(x - v.x, y - v.y, z - v.z); }
    Vec3 operator*(f32 s) const { return Vec3(x * s, y * s, z * s); }
    Vec3 operator/(f32 s) const { return Vec3(x / s, y / s, z / s); }

    Vec3& operator+=(const Vec3& v) { x += v.x; y += v.y; z += v.z; return *this; }
    Vec3& operator-=(const Vec3& v) { x -= v.x; y -= v.y; z -= v.z; return *this; }
    Vec3& operator*=(f32 s) { x *= s; y *= s; z *= s; return *this; }
    Vec3& operator/=(f32 s) { x /= s; y /= s; z /= s; return *this; }

    f32 dot(const Vec3& v) const { return x * v.x + y * v.y + z * v.z; }
    Vec3 cross(const Vec3& v) const {
        return Vec3(y * v.z - z * v.y, z * v.x - x * v.z, x * v.y - y * v.x);
    }
    f32 lengthSquared() const { return dot(*this); }
    f32 length() const {
        f32 len = lengthSquared();
        return (len > 0.0f) ? sqrtf(len) : 0.0f;
    }
    Vec3 normalized() const {
        f32 len = length();
        return (len > 0.0f) ? *this / len : Vec3(0, 0, 0);
    }

    static const Vec3& zero() { static Vec3 v(0, 0, 0); return v; }
    static const Vec3& one() { static Vec3 v(1, 1, 1); return v; }
    static const Vec3& forward() { static Vec3 v(0, 0, 1); return v; }
    static const Vec3& up() { static Vec3 v(0, 1, 0); return v; }
    static const Vec3& right() { static Vec3 v(1, 0, 0); return v; }
};

inline Vec3 operator*(f32 s, const Vec3& v) { return v * s; }

} // namespace Caffeine