# Core Module

> Documentation for Caffeine Engine Core module - Phase 1: Atomic Foundation

## Overview

The Core module provides fundamental types, platform abstractions, and compiler detection. All other modules depend on Core.

**Location:** `src/core/`

## Files

| File | Description |
|------|-------------|
| [`Types.hpp`](../../src/core/Types.hpp) | Fixed-width integer types, constants |
| [`Platform.hpp`](../../src/core/Platform.hpp) | Platform detection macros |
| [`Assertions.hpp`](../../src/core/Assertions.hpp) | Debug assertions |
| [`Compiler.hpp`](../../src/core/Compiler.hpp) | Compiler detection macros |

## Dependencies

```
Core has NO dependencies on other Caffeine modules.
Core is the foundation for all other modules.
```

## See Also

- [Memory Module](../memory/allocators.md) - Custom memory management
- [Containers Module](../containers/vector.md) - Data structures
- [Math Module](../math/vectors.md) - Mathematical types
- [API Reference](../api/README.md) - Complete API documentation

## Related Documentation

- [ROADMAP.md](../ROADMAP.md) - Phase 1 requirements (RF1.1-RF1.9)
- [SPECS.md](../SPECS.md) - Development rules and patterns
- [memory_model.md](../memory_model.md) - Memory allocator specifications
