# Math Module

> Documentation for Caffeine Engine Math module - Phase 1: Atomic Foundation

## Overview

The Math module provides vector and matrix types optimized for game development. All types are designed with cache locality and SIMD potential in mind.

**Location:** `src/math/`

## Files

| File | Description | Status |
|------|-------------|--------|
| [`Vec2.hpp`](../../src/math/Vec2.hpp) | 2D vector (x, y) | ✅ Complete |
| [`Vec3.hpp`](../../src/math/Vec3.hpp) | 3D vector (x, y, z) | ✅ Complete |
| [`Vec4.hpp`](../../src/math/Vec4.hpp) | 4D vector (x, y, z, w) | ✅ Complete |
| [`Mat4.hpp`](../../src/math/Mat4.hpp) | 4x4 matrix (column-major) | ✅ Complete |
| [`Math.hpp`](../../src/math/Math.hpp) | Utility functions | ✅ Complete |

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
| `identity()` | Create identity matrix |
| `translation(x, y, z)` | Translation matrix |
| `scale(s)` or `scale(x, y, z)` | Scale matrix |
| `rotationZ(radians)` | Z-axis rotation |
| `rotationY(radians)` | Y-axis rotation |
| `rotationX(radians)` | X-axis rotation |
| `transformPoint(p)` | Transform 3D point |
| `transformVector(v)` | Transform 3D vector (w=0) |
| `transposed()` | Transpose matrix |
| `inverse()` | Matrix inverse |
| `operator*` | Matrix multiplication |

## Math Utilities

| Function | Description |
|----------|-------------|
| `Math::lerp(a, b, t)` | Linear interpolation |
| `Math::clamp(value, min, max)` | Clamp value |
| `Math::saturate(value)` | Clamp to [0, 1] |
| `Math::degToRad(degrees)` | Degrees to radians |
| `Math::radToDeg(radians)` | Radians to degrees |
| `Math::isPowerOfTwo(value)` | Check power of two |
| `Math::nextPowerOfTwo(value)` | Next power of two |
| `Math::absf(value)` | Absolute value (float) |
| `Math::sqrtf(value)` | Square root |
| `Math::sinf(value)` | Sine |
| `Math::cosf(value)` | Cosine |

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
- [ROADMAP.md](../ROADMAP.md) - Phase 1 requirements
- [SPECS.md](../SPECS.md) - Development rules
- [architecture_specs.md](../architecture_specs.md) - Technical specifications
- [API Reference](../api/README.md) - Complete API documentation
- [Test Documentation](../../tests/test_math.cpp) - Math tests

### Related Modules
- [Core Module](../architecture/core.md) - Foundation types
- [Memory Module](../architecture/memory.md) - Allocators
