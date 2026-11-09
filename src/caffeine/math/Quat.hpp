#pragma once

#include "../core/Types.hpp"
#include "Vec3.hpp"
#include "Vec4.hpp"
#include "Mat4.hpp"
#include "Math.hpp"
#include <math.h>

namespace Caffeine {

struct Quat {
    // Quaternion stored as (x, y, z, w) where w is the scalar part.
    // q = w + xi + yj + zk  with  i^2 = j^2 = k^2 = ijk = -1 (Hamilton convention).
    // For a unit quaternion representing a rotation of θ around unit axis û:
    //   q = (û·sin(θ/2), cos(θ/2))
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 z = 0.0f;
    f32 w = 1.0f;

    Quat() = default;
    Quat(f32 x, f32 y, f32 z, f32 w) : x(x), y(y), z(z), w(w) {}

    /// Identity quaternion q = (0, 0, 0, 1) — represents zero rotation.
    static Quat identity() { return {0.0f, 0.0f, 0.0f, 1.0f}; }

    /// Construct a quaternion from a unit axis and an angle in radians.
    ///   q = (axis·sin(θ/2), cos(θ/2))
    /// The axis is normalized internally.
    static Quat fromAxisAngle(Vec3 axis, f32 angleRad);

    /// Construct a quaternion from Euler angles (pitch-X, yaw-Y, roll-Z)
    /// using the ZYX convention: q = q_yaw * q_pitch * q_roll,
    /// i.e. roll (Z) is applied first, then pitch (Y), then yaw (X) last.
    /// This matches the standard aerospace Tait-Bryan sequence.
    static Quat fromEuler(f32 pitchRad, f32 yawRad, f32 rollRad);

    /// Construct a quaternion from a look-at direction and up vector.
    /// Builds a 3×3 orthonormal basis and converts via fromMatrix().
    static Quat lookAt(Vec3 forward, Vec3 up);

    /// Convert a 3×3 rotation matrix (upper-left of Mat4) to a quaternion.
    /// Uses the standard trace-based algorithm by Ken Shoemake (1987).
    static Quat fromMatrix(const Mat4& m);

    /// Spherical linear interpolation (SLERP) between two quaternions.
    ///   q(t) = (sin((1-t)·Ω)/sinΩ)·a + (sin(t·Ω)/sinΩ)·b
    /// where Ω = acos(a·b). The shortest arc is ensured by negating b if
    /// a·b < 0. Falls back to linear interpolation when a·b > 0.9999.
    static Quat slerp(Quat a, Quat b, f32 t);

    /// Normalized linear interpolation (NLERP) between two quaternions.
    ///   q(t) = normalize(a + (b - a)·t)
    /// Faster than SLERP but does not maintain constant angular velocity.
    /// The shortest arc is ensured by negating b if a·b < 0.
    static Quat nlerp(Quat a, Quat b, f32 t);

    /// Extract Euler angles (pitch-X, yaw-Y, roll-Z) from the quaternion
    /// using the ZYX convention. This is the inverse of fromEuler().
    Vec3 toEuler() const;

    /// Convert the quaternion to a 4×4 rotation matrix (column-major).
    ///   R_ij = 2(q_i·q_j - δ_ij·|q|²/2)  for  i,j ∈ {x,y,z}
    /// Transforms vectors as: v' = q·v·q⁻¹  (when q is unit).
    Mat4 toMatrix() const;

    /// Rotate vector v by this quaternion.
    ///   v' = v + 2·q_v × (q_v × v + w·v)
    /// where q_v = (x, y, z). This is the efficient form of q·v·q⁻¹
    /// requiring only 2 cross products instead of full quaternion multiplication.
    Vec3 rotate(Vec3 v) const;

    /// Conjugate: q* = (-x, -y, -z, w).
    /// For a unit quaternion, q* = q⁻¹.
    Quat conjugate() const { return {-x, -y, -z, w}; }

    /// Inverse: q⁻¹ = q* / |q|².
    /// For unit quaternions this equals the conjugate.
    Quat inverse() const;

    /// Normalize the quaternion to unit length: q̂ = q / |q|.
    /// Returns identity if the length is zero.
    Quat normalized() const;

    /// Magnitude: |q| = √(x² + y² + z² + w²).
    f32  length() const;

    /// Squared magnitude: |q|² = x² + y² + z² + w².
    f32  lengthSquared() const { return x*x + y*y + z*z + w*w; }

    /// Dot product: a·b = a.x·b.x + a.y·b.y + a.z·b.z + a.w·b.w.
    f32  dot(Quat o) const { return x*o.x + y*o.y + z*o.z + w*o.w; }

    /// Compose two quaternions (Hamilton product).
    /// Corresponds to applying q₂ first, then this quaternion:
    ///   q₁·q₂ = (q₁w·q₂ + q₁v·q₂v + q₁v × q₂v,
    ///            q₁w·q₂w - q₁v·q₂v)
    /// For rotating a vector: v' = (q₁·q₂)·v·(q₁·q₂)⁻¹
    Quat operator*(Quat o) const;

    /// Rotate a vector by this quaternion. Equivalent to rotate(v).
    Vec3 operator*(Vec3 v) const { return rotate(v); }

    /// Component-wise equality comparison (exact floating-point).
    bool operator==(Quat o) const;
};

/// q = (axis·sin(θ/2), cos(θ/2))
/// Given axis â (normalized internally) and angle θ in radians,
/// the half-angle formula directly produces the unit quaternion.
inline Quat Quat::fromAxisAngle(Vec3 axis, f32 angleRad) {
    Vec3 normalized = axis.normalized();
    f32 halfAngle = angleRad * 0.5f;
    f32 s = sinf(halfAngle);
    return {normalized.x * s, normalized.y * s, normalized.z * s, cosf(halfAngle)};
}

/// ZYX Convention (Tait-Bryan angles):
///   q = q_yaw(Y) · q_pitch(Y) · q_roll(X)
///   = [cos(ψ/2) + k·sin(ψ/2)] · [cos(θ/2) + j·sin(θ/2)] · [cos(φ/2) + i·sin(φ/2)]
///
/// Expanding the Hamilton product with:
///   cp = cos(pitch/2), sp = sin(pitch/2)   (X-axis rotation)
///   cy = cos(yaw/2),   sy = sin(yaw/2)     (Y-axis rotation)
///   cr = cos(roll/2),  sr = sin(roll/2)    (Z-axis rotation)
///
///   q.w = cr·cy·cp + sr·sy·sp
///   q.x = cr·cy·sp - sr·sy·cp
///   q.y = cr·sy·cp + sr·cy·sp
///   q.z = sr·cy·cp - cr·sy·sp
///
/// Naming: pitch around X, yaw around Y, roll around Z.
/// Applied as ZYX = roll first, then pitch, then yaw last.
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

/// Builds the rotation matrix R = [r | u | f] where:
///   f = normalize(forward)
///   r = normalize(up × f)     (right vector)
///   u = f × r                 (corrected up)
/// The matrix R transforms from world to view space.
/// The quaternion is extracted via fromMatrix().
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

/// Algorithm by Ken Shoemake (1987) — converts a 3×3 rotation matrix
/// to a quaternion using the numerically-stable trace method.
///
/// Given the rotation matrix R (column-major, upper-left 3×3 of Mat4):
///   The quaternion q = (x, y, z, w) is computed from R such that
///   R_ij = 2·q_i·q_j - δ_ij·|q|²  for i,j ∈ {1,2,3}
///
/// The largest diagonal element determines which set of equations
/// is used, avoiding division by small numbers for numerical stability.
///
/// For trace = R_00 + R_11 + R_22 > 0:
///   s   = 2·√(trace + 1)      → largest magnitude
///   q.w = s / 4
///   q.x = (R_21 - R_12) / s
///   q.y = (R_02 - R_20) / s
///   q.z = (R_10 - R_01) / s
///
/// If R_00 is the largest diagonal element:
///   s   = 2·√(1 + R_00 - R_11 - R_22)
///   q.x = s / 4
///   q.y = (R_01 + R_10) / s
///   q.z = (R_02 + R_20) / s
///   q.w = (R_21 - R_12) / s
///
/// If R_11 is the largest diagonal element:
///   s   = 2·√(1 + R_11 - R_00 - R_22)
///   q.y = s / 4
///   q.z = (R_12 + R_21) / s
///   q.x = (R_01 + R_10) / s
///   q.w = (R_02 - R_20) / s
///
/// Otherwise (R_22 is the largest):
///   s   = 2·√(1 + R_22 - R_00 - R_11)
///   q.z = s / 4
///   q.x = (R_02 + R_20) / s
///   q.y = (R_12 + R_21) / s
///   q.w = (R_10 - R_01) / s
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

/// Hamilton product of two quaternions.
/// Given q₁ = w₁ + x₁i + y₁j + z₁k and q₂ = w₂ + x₂i + y₂j + z₂k:
///   q₁·q₂ = (w₁w₂ - x₁x₂ - y₁y₂ - z₁z₂)
///          + (w₁x₂ + x₁w₂ + y₁z₂ - z₁y₂)i
///          + (w₁y₂ - x₁z₂ + y₁w₂ + z₁x₂)j
///          + (w₁z₂ + x₁y₂ - y₁x₂ + z₁w₂)k
///
/// The expanded component form (where each return value is ordered x,y,z,w):
///   x = w₁·x₂ + x₁·w₂ + y₁·z₂ - z₁·y₂
///   y = w₁·y₂ - x₁·z₂ + y₁·w₂ + z₁·x₂
///   z = w₁·z₂ + x₁·y₂ - y₁·x₂ + z₁·w₂
///   w = w₁·w₂ - x₁·x₂ - y₁·y₂ - z₁·z₂
///
/// Composition order: this·o corresponds to applying o first, then this.
/// For vectors: (this·o)·v·(this·o)⁻¹ = this·(o·v·o⁻¹)·this⁻¹
inline Quat Quat::operator*(Quat o) const {
    return {
        w * o.x + x * o.w + y * o.z - z * o.y,
        w * o.y - x * o.z + y * o.w + z * o.x,
        w * o.z + x * o.y - y * o.x + z * o.w,
        w * o.w - x * o.x - y * o.y - z * o.z
    };
}

/// Efficient vector rotation using the identity:
///   q·v·q⁻¹ = v + 2·q_v × (q_v × v + w·v)
/// where q_v = (x, y, z) is the vector part of the quaternion.
///
/// Derivation from the Hamilton product:
///   Let q = (q_v, w). Then:
///   q·v·q⁻¹ = v + 2w·(q_v × v) + 2·q_v × (q_v × v)
///           = v + 2·q_v × (q_v × v + w·v)
///
///
/// This uses only 2 cross products and 1 scalar-vector multiply,
/// avoiding the full quaternion multiplication (which would require
/// computing q·(v,0)·q⁻¹ with 4 multiplications and cross terms).
inline Vec3 Quat::rotate(Vec3 v) const {
    Vec3 qv(x, y, z);
    Vec3 cross1 = qv.cross(v);
    Vec3 cross2 = qv.cross(cross1 + v * w);
    return v + cross2 * 2.0f;
}

/// Converts the quaternion to a column-major 3×3 rotation matrix.
///
/// For a unit quaternion q = (x, y, z, w), the rotation matrix is:
///
///       | 1-2(y²+z²)   2(xy-zw)     2(xz+yw)   |
///   R = | 2(xy+zw)     1-2(x²+z²)   2(yz-xw)   |
///       | 2(xz-yw)     2(yz+xw)     1-2(x²+y²) |
///
/// This matrix satisfies R·v = q·v·q⁻¹ for any vector v,
/// and R·Rᵀ = I for unit quaternions.
///
/// The matrix is stored column-major in Mat4:
///   column j = (R_0j, R_1j, R_2j, 0)  for j = 0,1,2
///   column 3 = (0, 0, 0, 1)
inline Mat4 Quat::toMatrix() const {
    f32 xx = x * x, yy = y * y, zz = z * z;
    f32 xy = x * y, xz = x * z, xw = x * w;
    f32 yz = y * z, yw = y * w, zw = z * w;

    Mat4 result = Mat4::identity();
    // Column 0
    result(0, 0) = 1.0f - 2.0f * (yy + zz);
    result(1, 0) = 2.0f * (xy + zw);
    result(2, 0) = 2.0f * (xz - yw);
    // Column 1
    result(0, 1) = 2.0f * (xy - zw);
    result(1, 1) = 1.0f - 2.0f * (xx + zz);
    result(2, 1) = 2.0f * (yz + xw);
    // Column 2
    result(0, 2) = 2.0f * (xz + yw);
    result(1, 2) = 2.0f * (yz - xw);
    result(2, 2) = 1.0f - 2.0f * (xx + yy);

    return result;
}

/// Extract Euler angles (pitch-X, yaw-Y, roll-Z) using the ZYX convention.
///
/// From the rotation matrix R = toMatrix(), the ZYX Euler angles are:
///   pitch (X) = atan2(R_21, R_22) = atan2(2(yz + wx), 1 - 2(x² + y²))
///   yaw   (Y) = asin(-R_20)       = asin(-2(xz - wy))
///   roll  (Z) = atan2(R_10, R_00) = atan2(2(xy + wz), 1 - 2(y² + z²))
///
/// with singularities at yaw = ±π/2 (gimbal lock).
///
/// The naming matches fromEuler(): pitch~X, yaw~Y, roll~Z.
/// Returns {pitch, yaw, roll} in radians, each in [-π, π].
inline Vec3 Quat::toEuler() const {
    // ZYX convention extracts:
    //   sin(yaw) = -R_20 = -2(xz - wy)
    //   tan(pitch) = R_21 / R_22 = 2(yz + wx) / (1 - 2(x² + y²))
    //   tan(roll)  = R_10 / R_00 = 2(xy + wz) / (1 - 2(y² + z²))
    f32 yaw   = asinf(Math::clamp(-2.0f * (x * z - w * y), -1.0f, 1.0f));
    f32 pitch = atan2f(2.0f * (y * z + w * x), 1.0f - 2.0f * (x * x + y * y));
    f32 roll  = atan2f(2.0f * (x * y + w * z), 1.0f - 2.0f * (y * y + z * z));
    return {pitch, yaw, roll};
}

inline bool Quat::operator==(Quat o) const {
    return x == o.x && y == o.y && z == o.z && w == o.w;
}

/// Spherical linear interpolation (SLERP) between a and b at parameter t ∈ [0,1].
///
///   SLERP(a, b, t) = a·(sin((1-t)Ω) / sinΩ) + b·(sin(tΩ) / sinΩ)
///
/// where Ω = arccos(|a·b|) is the angle between the two quaternions
/// on the 4D unit hypersphere.
///
/// 1. If a·b < 0, negate b to interpolate along the shortest arc
///    (the antipodal quaternion b' = -b represents the same rotation).
/// 2. If cosΩ > 0.9999 (Ω ≈ 0), the quaternions are nearly identical,
///    so linear interpolation is used to avoid division by sinΩ ≈ 0.
/// 3. Otherwise, the spherical interpolation coefficients are computed
///    using the half-angle formula.
///
/// SLERP guarantees constant angular velocity (constant-speed rotation
/// along the shortest arc), at the cost of sinf/atan2f/sqrtf.
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

/// Normalized linear interpolation (NLERP) between a and b at parameter t ∈ [0,1].
///
///   NLERP(a, b, t) = normalize(a + (b - a)·t)
///                   = normalize((1-t)·a + t·b)
///
/// Unlike SLERP, NLERP does NOT maintain constant angular velocity —
/// the interpolation speed varies with t, being faster near the middle.
/// However, it is significantly faster (only lerp + normalize, no trig).
///
/// The shortest arc is ensured by negating b if a·b < 0.
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
