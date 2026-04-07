# Containers Module

> Documentation for Caffeine Engine Containers module - Phase 1: Atomic Foundation

## Overview

The Containers module provides data structures optimized for game development with cache locality and zero heap allocations where possible.

**Location:** `src/containers/`

## Files

| File | Description |
|------|-------------|
| [`Vector.hpp`](../../src/containers/Vector.hpp) | Dynamic array with cache-friendly contiguous memory |
| [`HashMap.hpp`](../../src/containers/HashMap.hpp) | Hash table with O(1) lookup |
| [`StringView.hpp`](../../src/containers/StringView.hpp) | String without ownership (zero-copy) |
| [`FixedString.hpp`](../../src/containers/FixedString.hpp) | Inline buffer string (zero heap) |

## Vector<T>

Dynamic array with automatic growth. Supports custom allocators or defaults to global `operator new`.

```cpp
// Default construction (uses global new/delete)
Caffeine::Vector<int> vec;
vec.pushBack(42);

// With custom allocator
Caffeine::LinearAllocator alloc(1024);
Caffeine::Vector<int, Caffeine::IAllocator*> vec2(&alloc);
```

### Key Methods

| Method | Complexity | Description |
|--------|------------|--------------|
| `pushBack()` | O(1) amortized | Add element |
| `operator[]` | O(1) | Access by index |
| `reserve()` | O(n) | Pre-allocate capacity |
| `clear()` | O(n) | Clear all elements |
| `size()` | O(1) | Get element count |
| `capacity()` | O(1) | Get allocated capacity |

## HashMap<K, V>

Open addressing hash map with linear probing.

```cpp
Caffeine::HashMap<int, const char*> map;
map.set(1, "one");
map.set(2, "two");

const char* val = map.get(1);
bool exists = map.contains(1);
```

## StringView

Zero-copy string reference (pointer + length).

```cpp
Caffeine::StringView sv("Hello World", 5);  // "Hello"
```

## FixedString<T, N>

Stack-allocated string with inline buffer.

```cpp
Caffeine::FixedString<char, 32> fs;
fs.append("Hello");
```

## DOD (Data-Oriented Design)

- **Vector<T>**: Contiguous memory, no fragmentation
- **HashMap<K,V>**: Linear probing for cache-friendly access
- **StringView**: Zero-copy, no allocation
- **FixedString<T,N>**: Inline buffer, zero heap

## See Also

- [Core Module](../architecture/core.md) - Foundation types
- [Memory Module](../architecture/memory.md) - Allocators used by containers
- [Math Module](../math/vectors.md) - Vector math types

## Related Documentation

- [ROADMAP.md](../ROADMAP.md) - Phase 1 RF1.7-RF1.9
- [Test Documentation](../../tests/test_containers.cpp) - Container tests
