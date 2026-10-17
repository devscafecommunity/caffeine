# Core Module

> Documentation for Caffeine Engine Core module - Phase 1: Atomic Foundation

## Overview

The Core module provides fundamental types, platform abstractions, and compiler detection. All other modules depend on Core.

**Location:** `src/core/`

## Files

| File | Description | Status |
|------|-------------|--------|
| [`Types.hpp`](../../src/core/Types.hpp) | Fixed-width integer/float/char types, limit constants | ✅ Complete |
| [`Platform.hpp`](../../src/core/Platform.hpp) | Platform and bitness detection macros | ✅ Complete |
| [`Assertions.hpp`](../../src/core/Assertions.hpp) | Debug assertions | ✅ Complete |
| [`Compiler.hpp`](../../src/core/Compiler.hpp) | Compiler detection and portability macros | ✅ Complete |
| [`WorldUnits.hpp`](../../src/core/WorldUnits.hpp) | World-space convention (1 unit = 1 meter) and terrain scale helpers | ✅ Complete |
| [`IDebugHooks.hpp`](../../src/core/IDebugHooks.hpp) / `DebugHookRegistry.cpp` | IDE extension point (`FrameStats`, engine event hooks, zero overhead when unregistered) | ✅ Complete |
| [`io/`](../../src/core/io) (`BlobLoader`, `CafWriter`, `CafTypes`, `Crc32`, `FileWatcher`) | Low-level binary I/O, CAF framing/CRC, file watching | ✅ Complete |

## Dependencies

```
Core has NO dependencies on other Caffeine modules.
Core is the foundation for all other modules.
```

## Quick Reference

### Scalar types (`Types.hpp`)

| Type | Description | Header |
|------|-------------|--------|
| `u8`, `u16`, `u32`, `u64` | Unsigned integers | Types.hpp |
| `i8`, `i16`, `i32`, `i64` | Signed integers | Types.hpp |
| `f32`, `f64` | Floating point (`float` / `double`) | Types.hpp |
| `c8`, `c16`, `c32` | Character types (`char` / `char16_t` / `char32_t`) | Types.hpp |
| `usize`, `isize` | Pointer-sized (`std::size_t` / `std::ptrdiff_t`) | Types.hpp |
| `Byte` | Alias for `u8` | Types.hpp |

Limit constants are provided for every integer width (`u8_max` … `u64_max`,
`i8_min`/`i8_max` … `i64_min`/`i64_max`), and `static_assert`s validate the
size of each type at compile time.

### Platform detection (`Platform.hpp`)

| Macro | Description |
|-------|-------------|
| `CF_PLATFORM_WINDOWS` / `CF_PLATFORM_LINUX` / `CF_PLATFORM_MACOS` / `CF_PLATFORM_FREEBSD` / `CF_PLATFORM_UNKNOWN` | Active OS |
| `CF_PLATFORM_64BIT` / `CF_PLATFORM_32BIT` | Pointer bitness |
| `CF_PLATFORM_NAME` | `"Windows"`, `"Linux"`, `"macOS"`, `"FreeBSD"` or `"Unknown"` string literal |

### Compiler abstraction (`Compiler.hpp`)

| Macro | Description |
|-------|-------------|
| `CF_COMPILER_MSVC` / `CF_COMPILER_CLANG` / `CF_COMPILER_GCC` / `CF_COMPILER_UNKNOWN` | Active compiler |
| `CF_COMPILER_NAME` | `"MSVC"`, `"Clang"`, `"GCC"` or `"Unknown"` string literal |
| `CF_INLINE` / `CF_NOINLINE` / `CF_NORETURN` | Forced inline / noinline / noreturn |
| `CF_LIKELY(x)` / `CF_UNLIKELY(x)` | Branch-prediction hints |
| `CF_BUILTIN_TRAP()` / `CF_BUILTIN_DEBUGTRAP()` | Trapping / debug-break intrinsics |
| `CF_THREAD_LOCAL` | Thread-local storage specifier |
| `CF_EXPORT` / `CF_IMPORT` / `CF_HIDDEN` | Symbol visibility |
| `CF_CALL` | Calling convention (`__cdecl` on Windows) |
| `CF_CPP14` / `CF_CPP17` / `CF_CPP20` | Active C++ standard |

### Assertions (`Assertions.hpp`)

| Macro/Function | Description | Header |
|----------------|-------------|--------|
| `CF_ASSERT(cond, msg)` | Debug assertion; compiled out (`((void)0)`) when `CF_DEBUG` is not defined | Assertions.hpp |
| `CF_ASSERT_NOT_NULL(ptr)` | Null-pointer guard | Assertions.hpp |
| `CF_UNREACHABLE()` | Marks unreachable code (always active) | Assertions.hpp |
| `CF_DEBUG_ONLY(x)` | Expands to `x` in debug builds, nothing otherwise | Assertions.hpp |
| `Caffeine::assertFailed(cond, file, line, msg)` | Failure handler (traps via `CF_BUILTIN_TRAP`) | Assertions.hpp |

`CF_ASSERT_ENABLED` is `1` when `CF_DEBUG` is defined, `0` otherwise.

### World units (`WorldUnits.hpp`)

World-space convention: **1 engine unit = 1 meter** (`kMetersPerWorldUnit`).
`Caffeine::WorldUnits` provides `centimeter` / `meter` / `kilometer` constants,
default terrain scale constants (`kDefaultTerrainSizeM = 512`, …) and
`suggestedHeightM(worldSizeM)` for terrain generation.

## Usage Example

```cpp
#include <Caffeine.hpp>

// Using types
Caffeine::u32 count = 42;
Caffeine::f32 health = 100.0f;

// Limit constants
if (count == Caffeine::u32_max) { /* saturado */ }

// Using assertions (ativas apenas com CF_DEBUG)
CF_ASSERT(health > 0, "Health must be positive");
CF_ASSERT_NOT_NULL(ptr);
```

## See Also

### Related Documentation
- [HISTORY.md](../HISTORY.md) - Phase 1 requirements and development rules
- [memory/memory-model.md](../memory/memory-model.md) - Memory allocator specifications
- [architecture_specs.md](../architecture_specs.md) - Technical specifications
- [API Reference](../api/README.md) - Complete API documentation

### Related Modules
- [Memory Module](../memory/allocators.md) - Custom memory management
- [Containers Module](../memory/containers.md) - Data structures
- [Math Module](../math/vectors.md) - Mathematical types
