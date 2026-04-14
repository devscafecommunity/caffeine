# API Reference

> Complete API documentation for Caffeine Engine - Phase 1: Atomic Foundation

## Module Index

### Core Module

| Type/Function | Description | Header |
|---------------|-------------|--------|
| `u8`, `u16`, `u32`, `u64` | Unsigned integers | [`src/core/Types.hpp`](../../src/core/Types.hpp) |
| `i8`, `i16`, `i32`, `i64` | Signed integers | [`src/core/Types.hpp`](../../src/core/Types.hpp) |
| `f32`, `f64` | Floating point | [`src/core/Types.hpp`](../../src/core/Types.hpp) |
| `CF_ASSERT(condition, msg)` | Debug assertion | [`src/core/Assertions.hpp`](../../src/core/Assertions.hpp) |
| `CF_PLATFORM_WINDOWS` | Windows platform macro | [`src/core/Platform.hpp`](../../src/core/Platform.hpp) |
| `CF_PLATFORM_LINUX` | Linux platform macro | [`src/core/Platform.hpp`](../../src/core/Platform.hpp) |
| `CF_PLATFORM_MACOS` | macOS platform macro | [`src/core/Platform.hpp`](../../src/core/Platform.hpp) |

**Documentation:** [`docs/architecture/core.md`](../architecture/core.md)

### Memory Module

| Class | Description | Header |
|-------|-------------|--------|
| `IAllocator` | Base allocator interface | [`src/memory/Allocator.hpp`](../../src/memory/Allocator.hpp) |
| `LinearAllocator` | O(1) allocation, reset() clears all | [`src/memory/LinearAllocator.hpp`](../../src/memory/LinearAllocator.hpp) |
| `PoolAllocator` | Fixed-size slot allocator | [`src/memory/PoolAllocator.hpp`](../../src/memory/PoolAllocator.hpp) |
| `StackAllocator` | Marker-based allocator | [`src/memory/StackAllocator.hpp`](../../src/memory/StackAllocator.hpp) |

**Documentation:** [`docs/architecture/memory.md`](../architecture/memory.md) | [`docs/memory_model.md`](../memory_model.md)

### Containers Module

| Class | Description | Header |
|-------|-------------|--------|
| `Vector<T>` | Dynamic array | [`src/containers/Vector.hpp`](../../src/containers/Vector.hpp) |
| `HashMap<K, V>` | Hash table | [`src/containers/HashMap.hpp`](../../src/containers/HashMap.hpp) |
| `StringView` | Non-owning string | [`src/containers/StringView.hpp`](../../src/containers/StringView.hpp) |
| `FixedString<T, N>` | Fixed-size string | [`src/containers/FixedString.hpp`](../../src/containers/FixedString.hpp) |

**Documentation:** [`docs/containers/vector.md`](../containers/vector.md)

### Math Module

| Class/Function | Description | Header |
|----------------|-------------|--------|
| `Vec2` | 2D vector | [`src/math/Vec2.hpp`](../../src/math/Vec2.hpp) |
| `Vec3` | 3D vector | [`src/math/Vec3.hpp`](../../src/math/Vec3.hpp) |
| `Vec4` | 4D vector | [`src/math/Vec4.hpp`](../../src/math/Vec4.hpp) |
| `Mat4` | 4x4 matrix | [`src/math/Mat4.hpp`](../../src/math/Mat4.hpp) |
| `Math::lerp()` | Linear interpolation | [`src/math/Math.hpp`](../../src/math/Math.hpp) |
| `Math::clamp()` | Clamp value | [`src/math/Math.hpp`](../../src/math/Math.hpp) |
| `Math::isPowerOfTwo()` | Power of two check | [`src/math/Math.hpp`](../../src/math/Math.hpp) |

**Documentation:** [`docs/math/vectors.md`](../math/vectors.md)

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
| Containers | Core | Uses types for indices and sizes |

## See Also

- [MASTER.md](../MASTER.md) - Complete documentation
- [Core Architecture](../architecture/core.md)
- [Memory Architecture](../architecture/memory.md)
- [Containers Architecture](../containers/vector.md)
- [Math Architecture](../math/vectors.md)
- [ROADMAP.md](../ROADMAP.md) - Phase 1 requirements
