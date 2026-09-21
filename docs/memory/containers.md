# Containers Module

> Documentation for Caffeine Engine Containers module - Phase 1: Atomic Foundation

## Overview

The Containers module provides data structures optimized for game development with cache locality and zero heap allocations where possible.

**Location:** `src/containers/`

## Files

| File | Description | Status |
|------|-------------|--------|
| [`Vector.hpp`](../../src/containers/Vector.hpp) | Dynamic array with cache-friendly contiguous memory | ✅ Complete |
| [`HashMap.hpp`](../../src/containers/HashMap.hpp) | Hash table with O(1) lookup | ✅ Complete |
| [`StringView.hpp`](../../src/containers/StringView.hpp) | String without ownership (zero-copy) | ✅ Complete |
| [`FixedString.hpp`](../../src/containers/FixedString.hpp) | Inline buffer string (zero heap) | ✅ Complete |

## Vector<T>

Dynamic array with automatic growth. Supports custom allocators or defaults to global `operator new`.

```cpp
// Default construction (uses global new/delete)
Caffeine::Vector<int> vec;
vec.pushBack(42);

// With custom allocator
Caffeine::LinearAllocator alloc(1024);
Caffeine::Vector<int> vec2(&alloc);
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

Small associative container backed by `Vector<Pair>` with **linear search**
— O(n) `get`/`set`/`contains`/`remove`. There is no hashing and no open
addressing; it is suited for small tables (dozens of entries), not hot paths
over thousands of keys.

```cpp
Caffeine::HashMap<int, const char*> map;
map.set(1, "one");
map.set(2, "two");

const char* const* val = map.get(1);  // pointer; nullptr if missing
bool exists = map.contains(1);
map.remove(1);  // swap-with-back removal (unordered)
```

### Key Methods

| Method | Complexity | Description |
|--------|------------|--------------|
| `set()` (lvalue + rvalue overloads) | O(n) | Insert or update |
| `get()` (const + non-const) | O(n) | Pointer to value, `nullptr` if missing |
| `contains()` | O(n) | Check key exists |
| `remove()` | O(n) | Swap-with-back delete (order not preserved) |
| `clear()` / `size()` / `empty()` | O(n) / O(1) / O(1) | Reset / count / check |
| `begin()` / `end()` (const + non-const) | O(1) | Iterate pairs |
| `explicit HashMap(usize capacity)` | — | Pre-reserve storage |

## StringView

Zero-copy string reference (pointer + length, no ownership).

```cpp
Caffeine::StringView sv("Hello World", 5); // "Hello"
Caffeine::StringView full("Hello");        // length computed
```

Provides `data()` / `length()` / `size()` / `empty()`, `operator[]`,
and lexicographic `compare()` with `==`, `!=`, `<` operators.
Not null-terminated — do not pass `data()` to `%s` APIs unless length is honored.

## FixedString<N>

Stack-allocated string with inline buffer. Single template parameter:
`FixedString<32>`, **not** `FixedString<T, N>`.

```cpp
Caffeine::FixedString<32> fs;
fs.append("Hello");
const char* s = fs.cStr();  // always null-terminated
```

Provides `cStr()` / `data()` / `length()` / `size()` / `capacity()` (returns `N`),
`empty()` / `full()`, `clear()`, `append(const char*)` / `append(char)`,
`==` / `!=`, `operator[]`. Appends **silently truncate** at `N - 1` chars
(buffer always keeps room for `'\0'`).

## DOD (Data-Oriented Design)

- **Vector<T>**: Contiguous memory, no fragmentation; optional `IAllocator*`
  (defaults to global `new`/`delete` when none is given)
- **HashMap<K,V>**: Linear search over contiguous pairs — cache-friendly but O(n)
- **StringView**: Zero-copy, no allocation
- **FixedString<N>**: Inline buffer, zero heap

## See Also

### Related Documentation
- [HISTORY.md](../HISTORY.md) - Phase 1 requirements and development rules
- [architecture_specs.md](../architecture_specs.md) - Technical specifications
- [API Reference](../api/README.md) - Complete API documentation
- [Test Documentation](../../tests/test_containers.cpp) - Container tests

### Related Modules
- [Core Module](../core/types.md) - Foundation types
- [Memory Module](../memory/allocators.md) - Allocators used by containers
- [Math Module](../math/vectors.md) - Vector math types
