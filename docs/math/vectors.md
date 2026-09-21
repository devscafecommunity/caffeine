# Math Module

> Documentation for Caffeine Engine Math module - Phase 1: Atomic Foundation

## Overview

The Math module provides vector and matrix types optimized for game development. All types are designed with cache locality and SIMD potential in mind.

**Location:** `src/math/`

## Files

| File | Description | Status |
|------|-------------|--------|
| [`Vec2.hpp`](../../src/math/Vec2.hpp) | 2D vector (x, y) | ✅ Complete |
| [`Vec3.hpp`](../../src/math/Vec3.hpp) | 3D vector (x, y, z) + `cross()` | ✅ Complete |
| [`Vec4.hpp`](../../src/math/Vec4.hpp) | 4D vector (x, y, z, w) | ✅ Complete |
| [`Mat4.hpp`](../../src/math/Mat4.hpp) | 4x4 matrix (column-major) | ✅ Complete |
| [`Quat.hpp`](../../src/math/Quat.hpp) | Quaternion rotations | ✅ Complete — see [quaternions.md](quaternions.md) |
| [`Math.hpp`](../../src/math/Math.hpp) | Utility functions (`Caffeine::Math`) | ✅ Complete |

## Vector Types

### Vec2

2D vector for positions, velocities, UV coordinates.

```cpp
Caffeine::Vec2 position(10.0f, 20.0f);
Caffeine::Vec2 velocity(1.0f, -1.0f);
Caffeine::Vec2 normalized = velocity.normalized();
f32 length = velocity.length();
```

### Vec3

3D vector for positions, directions, colors (RGB).

```cpp
Caffeine::Vec3 pos(1.0f, 2.0f, 3.0f);
Caffeine::Vec3 up(0.0f, 1.0f, 0.0f);
Caffeine::Vec3 right(1.0f, 0.0f, 0.0f);
Caffeine::Vec3 cross = up.cross(right);
Caffeine::f32 dot = up.dot(right);
```

### Vec4

4D vector for homogeneous coordinates, colors (RGBA).

```cpp
Caffeine::Vec4 color(1.0f, 0.0f, 0.0f, 1.0f);  // RGBA red
```

## Mat4

Column-major 4x4 matrix for transformations. GPU-compatible layout.

```cpp
Caffeine::Mat4 identity = Caffeine::Mat4::identity();
Caffeine::Mat4 translation = Caffeine::Mat4::translation(10.0f, 20.0f, 30.0f);
Caffeine::Mat4 scale = Caffeine::Mat4::scale(2.0f);
Caffeine::Mat4 rotation = Caffeine::Mat4::rotationZ(0.5f);

Caffeine::Mat4 transform = translation * rotation * scale;
Caffeine::Vec3 transformed = transform.transformPoint(point);
```

### Key Methods

| Method | Description |
|--------|-------------|
| `identity()` / `zero()` | Identity / all-zero matrix |
| `translation(x, y, z)` / `translation(Vec3)` | Translation matrix |
| `scale(s)` or `scale(x, y, z)` | Scale matrix |
| `rotationX/Y/Z(radians)` | Axis rotations |
| `ortho(l, r, b, t, n, f)` | Orthographic projection |
| `perspective(fovY, aspect, near, far)` | Perspective projection (OpenGL RH) |
| `lookAt(eye, target, up)` | Right-handed view matrix |
| `transformPoint(p)` | Transform 3D point (w=1, perspective divide) |
| `transformVector(v)` | Transform direction (w=0, no translation) |
| `transformVec4(v)` | Raw 4D transform |
| `transposed()` | Transpose |
| `inverted()` | Inverse via cofactor expansion (returns identity on singular — see note) |
| `operator()(row, col)` | Element access over `m[col * 4 + row]` (column-major) |
| `operator*` | Matrix multiplication |

> ⚠️ `inverted()` retorna identidade em matriz singular (fallback silencioso);
> `lookAt()` degenera se `eye == target` (ver `AUDIT_REPORT.md` MA-2/MA-4).
> Não existe `Mat4::fromQuat` — use `Quat::toMatrix()` ([quaternions.md](quaternions.md)).

## Math Utilities (`Caffeine::Math`)

| Function | Description |
|----------|-------------|
| `PI` / `PI_HALF` / `TAU` / `E` | Constants |
| `degToRad(degrees)` / `radToDeg(radians)` | Angle conversion |
| `clamp(value, min, max)` | Clamp value |
| `saturate(value)` | Clamp to [0, 1] |
| `lerp(a, b, t)` / `inverseLerp(a, b, v)` | Interpolation and inverse |
| `smoothstep(edge0, edge1, x)` | Smooth Hermite step |
| `moveTowards(current, target, maxDelta)` | Stepped approach |
| `absf(value)` / `minf(a, b)` / `maxf(a, b)` | Scalar helpers |
| `approximatelyEqual(a, b, eps=1e-5)` | Epsilon comparison |
| `sqrtf_safe(value)` | `sqrt` guarded (≤0 → 0) |
| `isPowerOfTwo(usize)` | Check power of two |
| `nextPowerOfTwo(usize)` | Next power of two (32-bit lanes only) |

> ⚠️ `floorf()` / `ceilf()` / `roundf()` / `powf()` chamam a si mesmas por
> lookup não-qualificado (recursão infinita) — não usar até correção.
> `normalized()` de vetor zero retorna vetor zero (silencioso).

## DOD (Data-Oriented Design)

- **Contiguous storage**: Vec2/3/4 are plain structs with no virtual methods
- **Column-major**: Mat4 uses column-major for GPU compatibility
- **SIMD-ready**: Memory layout supports future SIMD optimization
- **Cache-friendly**: Small types that fit in CPU cache lines

## Agnosticism

All math types work for both 2D and 3D:

| Type | 2D Use | 3D Use |
|------|--------|--------|
| Vec2 | position, velocity, UV | — |
| Vec3 | — | position, direction, color RGB |
| Vec4 | — | homogeneous coords, color RGBA |
| Mat4 | 2D transforms | 3D transforms |

## See Also

### Related Documentation
- [HISTORY.md](../HISTORY.md) - Phase 1 requirements and development rules
- [architecture_specs.md](../architecture_specs.md) - Technical specifications
- [API Reference](../api/README.md) - Complete API documentation
- [Test Documentation](../../tests/test_math.cpp) - Math tests

### Related Modules
- [Core Module](../core/types.md) - Foundation types
- [Memory Module](../memory/allocators.md) - Allocators
