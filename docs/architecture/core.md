# Core Module

> Documentation for Caffeine Engine Core module - Phase 1: Atomic Foundation

## Overview

The Core module provides fundamental types, platform abstractions, and compiler detection. All other modules depend on Core.

**Location:** `src/core/`

## Files

| File | Description | Status |
|------|-------------|--------|
| [`Types.hpp`](../../src/core/Types.hpp) | Fixed-width integer types, constants | ✅ Complete |
| [`Platform.hpp`](../../src/core/Platform.hpp) | Platform detection macros | ✅ Complete |
| [`Assertions.hpp`](../../src/core/Assertions.hpp) | Debug assertions | ✅ Complete |
| [`Compiler.hpp`](../../src/core/Compiler.hpp) | Compiler detection macros | ✅ Complete |

## Dependencies

```
Core has NO dependencies on other Caffeine modules.
Core is the foundation for all other modules.
```

## Quick Reference

| Type/Function | Description | Header |
|---------------|-------------|--------|
| `u8`, `u16`, `u32`, `u64` | Unsigned integers | Types.hpp |
| `i8`, `i16`, `i32`, `i64` | Signed integers | Types.hpp |
| `f32`, `f64` | Floating point | Types.hpp |
| `CF_ASSERT(condition, msg)` | Debug assertion | Assertions.hpp |
| `CF_PLATFORM_WINDOWS` | Windows platform macro | Platform.hpp |
| `CF_PLATFORM_LINUX` | Linux platform macro | Platform.hpp |
| `CF_PLATFORM_MACOS` | macOS platform macro | Platform.hpp |
| `CF_COMPILER_CLANG` | Clang compiler macro | Compiler.hpp |
| `CF_COMPILER_GCC` | GCC compiler macro | Compiler.hpp |
| `CF_COMPILER_MSVC` | MSVC compiler macro | Compiler.hpp |

## Usage Example

```cpp
#include <Caffeine.hpp>

// Using types
Caffeine::u32 count = 42;
Caffeine::f32 health = 100.0f;

// Using assertions
CF_ASSERT(health > 0, “Health must be positive”);
```

## See Also

### Related Documentation
- [ROADMAP.md](../ROADMAP.md) - Phase 1 requirements (RF1.1-RF1.9)
- [SPECS.md](../SPECS.md) - Development rules and patterns
- [memory_model.md](../memory_model.md) - Memory allocator specifications
- [architecture_specs.md](../architecture_specs.md) - Technical specifications
- [API Reference](../api/README.md) - Complete API documentation

### Related Modules
- [Memory Module](./memory.md) - Custom memory management
- [Containers Module](../containers/vector.md) - Data structures
- [Math Module](../math/vectors.md) - Mathematical types
