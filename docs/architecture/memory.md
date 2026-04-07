# Memory Module

> Documentation for Caffeine Engine Memory module - Phase 1: Atomic Foundation

## Overview

The Memory module provides custom allocators for zero-dependency memory management. All allocators implement the `IAllocator` interface.

**Location:** `src/memory/`

## Files

| File | Description |
|------|-------------|
| [`Allocator.hpp`](../../src/memory/Allocator.hpp) | IAllocator interface |
| [`LinearAllocator.hpp`](../../src/memory/LinearAllocator.hpp) | O(1) allocation, reset() clears all |
| [`PoolAllocator.hpp`](../../src/memory/PoolAllocator.hpp) | Fixed-size slots, O(1) amortized |
| [`StackAllocator.hpp`](../../src/memory/StackAllocator.hpp) | Markers, freeToMarker() |

## Architecture

### IAllocator Interface

```cpp
class IAllocator {
public:
    virtual ~IAllocator() = default;
    virtual void* alloc(usize size, usize alignment = 8) = 0;
    virtual void free(void* ptr) = 0;
    virtual void reset() = 0;
    virtual usize usedMemory() const = 0;
    virtual usize totalSize() const = 0;
    virtual usize peakMemory() const = 0;
    virtual usize allocationCount() const = 0;
    virtual const char* name() const = 0;
};
```

## Allocators Comparison

| Allocator | Allocation | Deallocation | Use Case |
|-----------|------------|--------------|----------|
| **Linear** | O(1) | reset() only | Frame scratch memory |
| **Pool** | O(1) amortized | O(1) | Fixed-size objects (particles) |
| **Stack** | O(1) | freeToMarker() | Scoped allocations |

## DOD (Data-Oriented Design)

- All allocators use contiguous memory blocks
- No dynamic memory after initial allocation
- Thread-safe by design (no locks in hot path)
- Zero heap allocations during normal operation

## See Also

- [Core Module](./core.md) - Foundation types
- [Containers Module](../containers/vector.md) - Vector uses allocators
- [memory_model.md](../memory_model.md) - Detailed allocator specs

## Related Documentation

- [ROADMAP.md](../ROADMAP.md) - Phase 1 RF1.4-RF1.6
- [SPECS.md](../SPECS.md) - Development rules
- [Test Documentation](../../tests/test_allocators.cpp) - Allocator tests
