#pragma once

#include "../core/Types.hpp"
#include "Vec3.hpp"
#include "Vec4.hpp"
#include "Mat4.hpp"
#include "Math.hpp"
#include <math.h>

namespace Caffeine {

struct Quat {
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 z = 0.0f;
    f32 w = 1.0f;

    Quat() = default;
    Quat(f32 x, f32 y, f32 z, f32 w) : x(x), y(y), z(z), w(w) {}

    static Quat identity() { return {0.0f, 0.0f, 0.0f, 1.0f}; }

    static Quat fromAxisAngle(Vec3 axis, f32 angleRad);
    static Quat fromEuler(f32 pitchRad, f32 yawRad, f32 rollRad);
    static Quat lookAt(Vec3 forward, Vec3 up);
    static Quat fromMatrix(const Mat4& m);

    static Quat slerp(Quat a, Quat b, f32 t);
    static Quat nlerp(Quat a, Quat b, f32 t);

    Vec3 toEuler() const;
    Mat4 toMatrix() const;

    Vec3 rotate(Vec3 v) const;
    Quat conjugate() const { return {-x, -y, -z, w}; }
    Quat inverse() const;
    Quat normalized() const;
    f32  length() const;
    f32  lengthSquared() const { return x*x + y*y + z*z + w*w; }
    f32  dot(Quat o) const { return x*o.x + y*o.y + z*o.z + w*o.w; }

    Quat operator*(Quat o) const;
    Vec3 operator*(Vec3 v) const { return rotate(v); }
    bool operator==(Quat o) const;
};

inline Quat Quat::fromAxisAngle(Vec3 axis, f32 angleRad) {
    Vec3 normalized = axis.normalized();
    f32 halfAngle = angleRad * 0.5f;
    f32 s = sinf(halfAngle);
    return {normalized.x * s, normalized.y * s, normalized.z * s, cosf(halfAngle)};
}

inline Quat Quat::fromEuler(f32 pitchRad, f32 yawRad, f32 rollRad) {
    f32 cp = cosf(pitchRad * 0.5f);
    f32 sp = sinf(pitchRad * 0.5f);
    f32 cy = cosf(yawRad * 0.5f);
    f32 sy = sinf(yawRad * 0.5f);
    f32 cr = cosf(rollRad * 0.5f);
    f32 sr = sinf(rollRad * 0.5f);

    Quat q;
    q.w = cr * cy * cp + sr * sy * sp;
    q.x = cr * cy * sp - sr * sy * cp;
    q.y = cr * sy * cp + sr * cy * sp;
    q.z = sr * cy * cp - cr * sy * sp;
    return q;
}

inline Quat Quat::lookAt(Vec3 forward, Vec3 up) {
    Vec3 f = forward.normalized();
    Vec3 r = up.cross(f).normalized();
    Vec3 u = f.cross(r);

    Mat4 m = Mat4::identity();
    m(0, 0) = r.x; m(0, 1) = u.x; m(0, 2) = f.x;
    m(1, 0) = r.y; m(1, 1) = u.y; m(1, 2) = f.y;
    m(2, 0) = r.z; m(2, 1) = u.z; m(2, 2) = f.z;

    return fromMatrix(m);
}

inline Quat Quat::fromMatrix(const Mat4& m) {
    f32 trace = m(0, 0) + m(1, 1) + m(2, 2);
    Quat q;

    if (trace > 0.0f) {
        f32 s = sqrtf(trace + 1.0f) * 2.0f;
        q.w = 0.25f * s;
        q.x = (m(2, 1) - m(1, 2)) / s;
        q.y = (m(0, 2) - m(2, 0)) / s;
        q.z = (m(1, 0) - m(0, 1)) / s;
    } else if (m(0, 0) > m(1, 1) && m(0, 0) > m(2, 2)) {
        f32 s = sqrtf(1.0f + m(0, 0) - m(1, 1) - m(2, 2)) * 2.0f;
        q.w = (m(2, 1) - m(1, 2)) / s;
        q.x = 0.25f * s;
        q.y = (m(0, 1) + m(1, 0)) / s;
        q.z = (m(0, 2) + m(2, 0)) / s;
    } else if (m(1, 1) > m(2, 2)) {
        f32 s = sqrtf(1.0f + m(1, 1) - m(0, 0) - m(2, 2)) * 2.0f;
        q.w = (m(0, 2) - m(2, 0)) / s;
        q.x = (m(0, 1) + m(1, 0)) / s;
        q.y = 0.25f * s;
        q.z = (m(1, 2) + m(2, 1)) / s;
    } else {
        f32 s = sqrtf(1.0f + m(2, 2) - m(0, 0) - m(1, 1)) * 2.0f;
        q.w = (m(1, 0) - m(0, 1)) / s;
        q.x = (m(0, 2) + m(2, 0)) / s;
        q.y = (m(1, 2) + m(2, 1)) / s;
        q.z = 0.25f * s;
    }

    return q;
}

inline f32 Quat::length() const {
    return sqrtf(lengthSquared());
}

inline Quat Quat::normalized() const {
    f32 len = length();
    if (len > 0.0f) {
        f32 invLen = 1.0f / len;
        return {x * invLen, y * invLen, z * invLen, w * invLen};
    }
    return identity();
}

inline Quat Quat::inverse() const {
    f32 lenSq = lengthSquared();
    if (lenSq > 0.0f) {
        f32 invLenSq = 1.0f / lenSq;
        return {-x * invLenSq, -y * invLenSq, -z * invLenSq, w * invLenSq};
    }
    return *this;
}

inline Quat Quat::operator*(Quat o) const {
    return {
        w * o.x + x * o.w + y * o.z - z * o.y,
        w * o.y - x * o.z + y * o.w + z * o.x,
        w * o.z + x * o.y - y * o.x + z * o.w,
        w * o.w - x * o.x - y * o.y - z * o.z
    };
}

inline Vec3 Quat::rotate(Vec3 v) const {
    Vec3 qv(x, y, z);
    Vec3 cross1 = qv.cross(v);
    Vec3 cross2 = qv.cross(cross1 + v * w);
    return v + cross2 * 2.0f;
}

inline Mat4 Quat::toMatrix() const {
    f32 xx = x * x, yy = y * y, zz = z * z;
    f32 xy = x * y, xz = x * z, xw = x * w;
    f32 yz = y * z, yw = y * w, zw = z * w;

    Mat4 result = Mat4::identity();
    result(0, 0) = 1.0f - 2.0f * (yy + zz);
    result(1, 0) = 2.0f * (xy + zw);
    result(2, 0) = 2.0f * (xz - yw);

    result(0, 1) = 2.0f * (xy - zw);
    result(1, 1) = 1.0f - 2.0f * (xx + zz);
    result(2, 1) = 2.0f * (yz + xw);

    result(0, 2) = 2.0f * (xz + yw);
    result(1, 2) = 2.0f * (yz - xw);
    result(2, 2) = 1.0f - 2.0f * (xx + yy);

    return result;
}

inline Vec3 Quat::toEuler() const {
    f32 sinPitch = Math::clamp(-2.0f * (x * z - w * y), -1.0f, 1.0f);
    f32 pitch = asinf(sinPitch);
    f32 yaw = atan2f(2.0f * (y * z + w * x), 1.0f - 2.0f * (x * x + y * y));
    f32 roll = atan2f(2.0f * (x * y + w * z), 1.0f - 2.0f * (y * y + z * z));
    return {pitch, yaw, roll};
}

inline bool Quat::operator==(Quat o) const {
    return x == o.x && y == o.y && z == o.z && w == o.w;
}

inline Quat Quat::slerp(Quat a, Quat b, f32 t) {
    f32 cosOmega = a.dot(b);
    
    if (cosOmega < 0.0f) {
        b = {-b.x, -b.y, -b.z, -b.w};
        cosOmega = -cosOmega;
    }

    f32 k0, k1;
    if (cosOmega > 0.9999f) {
        k0 = 1.0f - t;
        k1 = t;
    } else {
        f32 sinOmega = sqrtf(1.0f - cosOmega * cosOmega);
        f32 omega = atan2f(sinOmega, cosOmega);
        f32 invSinOmega = 1.0f / sinOmega;
        k0 = sinf((1.0f - t) * omega) * invSinOmega;
        k1 = sinf(t * omega) * invSinOmega;
    }

    return {
        a.x * k0 + b.x * k1,
        a.y * k0 + b.y * k1,
        a.z * k0 + b.z * k1,
        a.w * k0 + b.w * k1
    };
}

inline Quat Quat::nlerp(Quat a, Quat b, f32 t) {
    if (a.dot(b) < 0.0f) {
        b = {-b.x, -b.y, -b.z, -b.w};
    }
    
    Quat result = {
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
        a.w + (b.w - a.w) * t
    };
    
    return result.normalized();
}

} // namespace Caffeine
