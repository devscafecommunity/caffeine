# Memory Module

> Documentation for Caffeine Engine Memory module - Phase 1: Atomic Foundation

## Overview

The Memory module provides custom allocators for zero-dependency memory management. All allocators implement the `IAllocator` interface.

**Location:** `src/memory/`

**Namespace:** `Caffeine` (not `Caffeine::Memory` — allocators live directly in the root namespace).

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

// Free helpers (Allocator.hpp)
inline usize calculatePadding(void* ptr, usize alignment);
inline usize calculateAlignedSize(usize size, usize alignment);
```

## Allocators Comparison

| Allocator | Allocation | Deallocation | Use Case |
|-----------|------------|--------------|----------|
| **Linear** | O(1) | `reset()` only (`free()` is a no-op) | Frame scratch memory |
| **Pool** | O(1) | O(1); `free(nullptr)` safe; **no double-free detection** | Fixed-size objects (particles) |
| **Stack** | O(1) | `freeToMarker()` (`free()` is a no-op) | Scoped allocations |

## Semantics confirmed in code

- **Constructors:** each allocator can own its buffer (`explicit X(usize size)`,
  allocated with `new u8[]`) or wrap an external buffer (`X(void* buffer, usize size)`,
  no ownership transfer).
- **Out of memory:** `alloc()` returns `nullptr` — always check the result.
- **Pool specifics:** constructor asserts `alignment >= 8` and power-of-two;
  `alloc()` ignores its `size`/`alignment` parameters (always returns one fixed
  slot, asserts `size <= slotSize` in debug); slots must fit a pointer
  (min. 8 bytes — free list is intrusive); `reset()` rebuilds the free list.
  Introspection: `freeSlots()`, `slotSize()`, `maxSlots()`.
- **Stack specifics:** `Marker` is a `usize` offset from the buffer start
  (`setMarker()`); `freeToMarker()` ignores markers beyond the cursor and resets
  the allocation count; `free(ptr)` does nothing.
- **Stats:** `usedMemory()` / `totalSize()` / `peakMemory()` /
  `allocationCount()` are tracked live; `reset()` zeroes the allocation count
  (Linear/Stack) or the used count (Pool).

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
- [memory/memory-model.md](../memory/memory-model.md) - Detailed allocator specifications
- [HISTORY.md](../HISTORY.md) - Phase 1 requirements and development rules
- [architecture_specs.md](../architecture_specs.md) - Technical specifications
- [API Reference](../api/README.md) - Complete API documentation
- [Test Documentation](../../tests/test_allocators.cpp) - Allocator tests

### Related Modules
- [Core Module](../core/types.md) - Foundation types (used by Memory)
- [Containers Module](../memory/containers.md) - Vector uses allocators
- [Math Module](../math/vectors.md) - Mathematical types
