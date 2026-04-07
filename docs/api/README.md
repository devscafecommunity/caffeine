# API Reference

> Complete API documentation for Caffeine Engine - Phase 1: Atomic Foundation

## Module Index

### Core Module
| Type/Function | Description |
|---------------|-------------|
| `u8`, `u16`, `u32`, `u64` | Unsigned integers |
| `i8`, `i16`, `i32`, `i64` | Signed integers |
| `f32`, `f64` | Floating point |
| `CF_ASSERT(condition, msg)` | Debug assertion |
| `CF_PLATFORM_WINDOWS` | Windows platform macro |
| `CF_PLATFORM_LINUX` | Linux platform macro |
| `CF_PLATFORM_MACOS` | macOS platform macro |

### Memory Module
| Class | Description |
|-------|-------------|
| `IAllocator` | Base allocator interface |
| `LinearAllocator` | O(1) allocation, reset() clears all |
| `PoolAllocator` | Fixed-size slot allocator |
| `StackAllocator` | Marker-based allocator |

### Containers Module
| Class | Description |
|-------|-------------|
| `Vector<T>` | Dynamic array |
| `HashMap<K, V>` | Hash table |
| `StringView` | Non-owning string |
| `FixedString<T, N>` | Fixed-size string |

### Math Module
| Class/Function | Description |
|----------------|-------------|
| `Vec2` | 2D vector |
| `Vec3` | 3D vector |
| `Vec4` | 4D vector |
| `Mat4` | 4x4 matrix |
| `Math::lerp()` | Linear interpolation |
| `Math::clamp()` | Clamp value |
| `Math::isPowerOfTwo()` | Power of two check |

## Quick Start

```cpp
#include <Caffeine.hpp>

// Using types
Caffeine::u32 count = 42;
Caffeine::f32 health = 100.0f;

// Using containers
Caffeine::Vector<Caffeine::f32> positions;
positions.pushBack(1.0f);
positions.pushBack(2.0f);

// Using math
Caffeine::Vec3 pos(1.0f, 2.0f, 3.0f);
Caffeine::Mat4 transform = Caffeine::Mat4::translation(pos);
```

## Cross-Module References

| From | To | Reference |
|------|-----|-----------|
| Vector | Memory | Uses IAllocator for allocation |
| All | Core | Uses types (u32, f32, etc.) |
| Math | Core | Uses types in vector/matrix operations |

## See Also

- [Core Architecture](../architecture/core.md)
- [Memory Architecture](../architecture/memory.md)
- [Containers Architecture](../containers/vector.md)
- [Math Architecture](../math/vectors.md)
- [ROADMAP.md](../ROADMAP.md) - Phase 1 requirements
