#pragma once

#include "../core/Types.hpp"
#include "Vec3.hpp"
#include "Vec4.hpp"
#include <math.h>

namespace Caffeine {

class Mat4 {
public:
    Mat4() { setIdentity(); }

    void setIdentity() {
        for (usize i = 0; i < 16; ++i) m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    }

    void setZero() {
        for (usize i = 0; i < 16; ++i) m[i] = 0.0f;
    }

    static Mat4 identity() {
        Mat4 result;
        result.setIdentity();
        return result;
    }

    static Mat4 zero() {
        Mat4 result;
        result.setZero();
        return result;
    }

    f32* data() { return m; }
    const f32* data() const { return m; }

    f32& operator()(usize row, usize col) { return m[col * 4 + row]; }
    f32 operator()(usize row, usize col) const { return m[col * 4 + row]; }

    Mat4 operator*(const Mat4& rhs) const {
        Mat4 result;
        for (usize col = 0; col < 4; ++col) {
            for (usize row = 0; row < 4; ++row) {
                f32 sum = 0.0f;
                for (usize k = 0; k < 4; ++k) {
                    sum += (*this)(row, k) * rhs(k, col);
                }
                result(row, col) = sum;
            }
        }
        return result;
    }

    Vec3 transformPoint(const Vec3& p) const {
        Vec4 p4(p.x, p.y, p.z, 1.0f);
        Vec4 result(0, 0, 0, 0);
        for (usize row = 0; row < 4; ++row) {
            result.x += (*this)(row, 0) * p4.x;
            result.y += (*this)(row, 1) * p4.y;
            result.z += (*this)(row, 2) * p4.z;
            result.w += (*this)(row, 3) * p4.w;
        }
        if (result.w != 0.0f) {
            return Vec3(result.x / result.w, result.y / result.w, result.z / result.w);
        }
        return Vec3(result.x, result.y, result.z);
    }

    Vec3 transformVector(const Vec3& v) const {
        Vec4 v4(v.x, v.y, v.z, 0.0f);
        Vec4 result(0, 0, 0, 0);
        for (usize row = 0; row < 4; ++row) {
            result.x += (*this)(row, 0) * v4.x;
            result.y += (*this)(row, 1) * v4.y;
            result.z += (*this)(row, 2) * v4.z;
            result.w += (*this)(row, 3) * v4.w;
        }
        return Vec3(result.x, result.y, result.z);
    }

    Mat4 transposed() const {
        Mat4 result;
        for (usize row = 0; row < 4; ++row) {
            for (usize col = 0; col < 4; ++col) {
                result(row, col) = (*this)(col, row);
            }
        }
        return result;
    }

    static Mat4 translation(f32 x, f32 y, f32 z) {
        Mat4 result = identity();
        result(0, 3) = x;
        result(1, 3) = y;
        result(2, 3) = z;
        return result;
    }

    static Mat4 translation(const Vec3& v) { return translation(v.x, v.y, v.z); }

    static Mat4 scale(f32 s) { return scale(s, s, s); }
    static Mat4 scale(f32 x, f32 y, f32 z) {
        Mat4 result = identity();
        result(0, 0) = x;
        result(1, 1) = y;
        result(2, 2) = z;
        return result;
    }

    static Mat4 rotationZ(f32 radians) {
        Mat4 result = identity();
        f32 c = cosf(radians);
        f32 s = sinf(radians);
        result(0, 0) = c;
        result(0, 1) = -s;
        result(1, 0) = s;
        result(1, 1) = c;
        return result;
    }

    static Mat4 rotationY(f32 radians) {
        Mat4 result = identity();
        f32 c = cosf(radians);
        f32 s = sinf(radians);
        result(0, 0) = c;
        result(0, 2) = s;
        result(2, 0) = -s;
        result(2, 2) = c;
        return result;
    }

    static Mat4 rotationX(f32 radians) {
        Mat4 result = identity();
        f32 c = cosf(radians);
        f32 s = sinf(radians);
        result(1, 1) = c;
        result(1, 2) = -s;
        result(2, 1) = s;
        result(2, 2) = c;
        return result;
    }

    static Mat4 ortho(f32 left, f32 right, f32 bottom, f32 top, f32 near, f32 far) {
        Mat4 result;
        result.setZero();
        result(0, 0) = 2.0f / (right - left);
        result(1, 1) = 2.0f / (top - bottom);
        result(2, 2) = -2.0f / (far - near);
        result(0, 3) = -(right + left) / (right - left);
        result(1, 3) = -(top + bottom) / (top - bottom);
        result(2, 3) = -(far + near) / (far - near);
        result(3, 3) = 1.0f;
        return result;
    }

    static Mat4 perspective(f32 fovY, f32 aspect, f32 near, f32 far) {
        f32 tanHalfFov = tanf(fovY / 2.0f);
        Mat4 result;
        result.setZero();
        result(0, 0) = 1.0f / (aspect * tanHalfFov);
        result(1, 1) = 1.0f / tanHalfFov;
        result(2, 2) = -(far + near) / (far - near);
        result(2, 3) = -1.0f;
        result(3, 2) = -(2.0f * far * near) / (far - near);
        return result;
    }

    static Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
        Vec3 forward = (target - eye).normalized();
        Vec3 right = forward.cross(up).normalized();
        Vec3 newUp = right.cross(forward);

        Mat4 result = identity();
        result(0, 0) = right.x;
        result(1, 0) = right.y;
        result(2, 0) = right.z;
        result(0, 1) = newUp.x;
        result(1, 1) = newUp.y;
        result(2, 1) = newUp.z;
        result(0, 2) = -forward.x;
        result(1, 2) = -forward.y;
        result(2, 2) = -forward.z;
        result(0, 3) = -right.dot(eye);
        result(1, 3) = -newUp.dot(eye);
        result(2, 3) = forward.dot(eye);
        return result;
    }

private:
    f32 m[16];
};

} // namespace Caffeine