# Math Module

> Documentation for Caffeine Engine Math module - Phase 1: Atomic Foundation

## Overview

The Math module provides vector and matrix types optimized for game development. All types are designed with cache locality and SIMD potential in mind.

**Location:** `src/math/`

## Files

| File | Description |
|------|-------------|
| [`Vec2.hpp`](../../src/math/Vec2.hpp) | 2D vector (x, y) |
| [`Vec3.hpp`](../../src/math/Vec3.hpp) | 3D vector (x, y, z) |
| [`Vec4.hpp`](../../src/math/Vec4.hpp) | 4D vector (x, y, z, w) |
| [`Mat4.hpp`](../../src/math/Mat4.hpp) | 4x4 matrix (column-major) |
| [`Math.hpp`](../../src/math/Math.hpp) | Utility functions |

## Vector Types

### Vec2
```cpp
Caffeine::Vec2 position(10.0f, 20.0f);
Caffeine::Vec2 velocity(1.0f, -1.0f);
Caffeine::Vec2 normalized = velocity.normalized();
f32 length = velocity.length();
```

### Vec3
```cpp
Caffeine::Vec3 pos(1.0f, 2.0f, 3.0f);
Caffeine::Vec3 up(0.0f, 1.0f, 0.0f);
Caffeine::Vec3 right(1.0f, 0.0f, 0.0f);
Caffeine::Vec3 cross = up.cross(right);
Caffeine::f32 dot = up.dot(right);
```

### Vec4
```cpp
Caffeine::Vec4 color(1.0f, 0.0f, 0.0f, 1.0f);  // RGBA
```

## Mat4

Column-major 4x4 matrix for transformations.

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
| `transformPoint(p)` | Transform 3D point |
| `transformVector(v)` | Transform 3D vector (w=0) |
| `transposed()` | Transpose matrix |
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

## DOD (Data-Oriented Design)

- **Contiguous storage**: Vec2/3/4 are plain structs with no virtual methods
- **Column-major**: Mat4 uses column-major for GPU compatibility
- **SIMD-ready**: Memory layout supports future SIMD optimization
- **Cache-friendly**: Small types that fit in CPU cache lines

## Agnosticism

All math types work for both 2D and 3D:
- Vec2: 2D position, UV coordinates
- Vec3: 3D position, direction, color RGB
- Vec4: Homogeneous coordinates, color RGBA
- Mat4: Works with both 2D and 3D transformations

## See Also

- [Core Module](../architecture/core.md) - Foundation types
- [Memory Module](../architecture/memory.md) - Allocators

## Related Documentation

- [ROADMAP.md](../ROADMAP.md) - Phase 1 requirements
- [Test Documentation](../../tests/test_math.cpp) - Math tests
