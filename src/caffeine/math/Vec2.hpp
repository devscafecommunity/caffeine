#pragma once

#include "../core/Types.hpp"
#include <math.h>

namespace Caffeine {

struct Vec2 {
    f32 x = 0.0f;
    f32 y = 0.0f;

    Vec2() = default;
    Vec2(f32 x, f32 y) : x(x), y(y) {}

    Vec2 operator+(const Vec2& v) const { return Vec2(x + v.x, y + v.y); }
    Vec2 operator-(const Vec2& v) const { return Vec2(x - v.x, y - v.y); }
    Vec2 operator*(f32 s) const { return Vec2(x * s, y * s); }
    Vec2 operator/(f32 s) const { return Vec2(x / s, y / s); }

    Vec2& operator+=(const Vec2& v) { x += v.x; y += v.y; return *this; }
    Vec2& operator-=(const Vec2& v) { x -= v.x; y -= v.y; return *this; }
    Vec2& operator*=(f32 s) { x *= s; y *= s; return *this; }
    Vec2& operator/=(f32 s) { x /= s; y /= s; return *this; }

    bool operator==(const Vec2& v) const { return x == v.x && y == v.y; }
    bool operator!=(const Vec2& v) const { return !(*this == v); }

    f32 dot(const Vec2& v) const { return x * v.x + y * v.y; }
    f32 lengthSquared() const { return dot(*this); }
    f32 length() const {
        f32 len = lengthSquared();
        return (len > 0.0f) ? sqrtf(len) : 0.0f;
    }

    Vec2 normalized() const {
        f32 len = length();
        return (len > 0.0f) ? *this / len : Vec2(0, 0);
    }

    static const Vec2& zero() { static Vec2 v(0, 0); return v; }
    static const Vec2& one() { static Vec2 v(1, 1); return v; }
};

inline Vec2 operator*(f32 s, const Vec2& v) { return v * s; }

} // namespace Caffeine