# Memory Module

> Documentation for Caffeine Engine Memory module - Phase 1: Atomic Foundation

## Overview

The Memory module provides custom allocators for zero-dependency memory management. All allocators implement the `IAllocator` interface.

**Location:** `src/memory/`

## Files

| File | Description | Status |
|------|-------------|--------|
| [`Allocator.hpp`](../../src/memory/Allocator.hpp) | IAllocator interface | ✅ Complete |
| [`LinearAllocator.hpp`](../../src/memory/LinearAllocator.hpp) | O(1) allocation, reset() clears all | ✅ Complete |
| [`PoolAllocator.hpp`](../../src/memory/PoolAllocator.hpp) | Fixed-size slots, O(1) amortized | ✅ Complete |
| [`StackAllocator.hpp`](../../src/memory/StackAllocator.hpp) | Markers, freeToMarker() | ✅ Complete |

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

## Usage Examples

### LinearAllocator

```cpp
// Create 64KB linear allocator
Caffeine::LinearAllocator alloc(64 * 1024);

// Allocate (O(1))
void* ptr = alloc.alloc(1024, 8);

// Reset all at once
alloc.reset();
```

### PoolAllocator

```cpp
// Create pool for 100 particles of 64 bytes each
Caffeine::PoolAllocator pool(64 * 100, 64);

// Allocate (O(1) amortized)
void* p1 = pool.alloc(64);
void* p2 = pool.alloc(64);

// Free returns slot to pool
pool.free(p1);
```

### StackAllocator

```cpp
Caffeine::StackAllocator stack(1024 * 1024); // 1MB

// Set marker
auto marker = stack.setMarker();

// Allocate
void* ptr = stack.alloc(512);

// Free to marker (frees everything after marker)
stack.freeToMarker(marker);
```

## DOD (Data-Oriented Design)

- All allocators use contiguous memory blocks
- No dynamic memory after initial allocation
- Not thread-safe for shared instances; use per-thread allocators or external synchronization
- Zero heap allocations during normal operation

## Quick Reference

| Class | Complexity | Key Method |
|-------|------------|------------|
| `LinearAllocator` | O(1) alloc, O(1) reset | `reset()` clears all |
| `PoolAllocator` | O(1) amortized | `free()` returns slot |
| `StackAllocator` | O(1) alloc | `freeToMarker()` |

## See Also

### Related Documentation
- [memory_model.md](../memory_model.md) - Detailed allocator specifications
- [ROADMAP.md](../ROADMAP.md) - Phase 1 RF1.4-RF1.6
- [SPECS.md](../SPECS.md) - Development rules
- [architecture_specs.md](../architecture_specs.md) - Technical specifications
- [API Reference](../api/README.md) - Complete API documentation
- [Test Documentation](../../tests/test_allocators.cpp) - Allocator tests

### Related Modules
- [Core Module](./core.md) - Foundation types (used by Memory)
- [Containers Module](../containers/vector.md) - Vector uses allocators
- [Math Module](../math/vectors.md) - Mathematical types
