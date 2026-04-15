# 🧪 Test System

> **Fase:** 2 — O Pulso e a Concorrência  
> **Namespace:** `Caffeine::Testing`  
> **Framework:** Catch2 v3.x (header-only)  
> **Status:** 📅 Planejado  

---

## Overview

This document describes the test system implementation for Caffeine Engine, including framework selection, test organization, CI integration, and success criteria.

---

## Framework Selection

- **Catch2 v3.x** - Header-only C++ testing framework
- **Rationale:** Simple integration, no external dependencies, follows Caffeine's "zero dependency" philosophy as much as a test framework allows

---

## File Structure

```
tests/
├── Catch2/                   # Header-only framework
│   └── catch.hpp           # Main header
├── test_allocators.cpp     # Memory allocator tests
├── test_containers.cpp     # Container tests
├── test_math.cpp           # Math library tests
├── test_core.cpp           # Core module tests
├── test_threading.cpp      # Job System tests
├── benchmarks.cpp          # Performance benchmarks
├── CMakeLists.txt          # Test build configuration
└── README.md               # Test documentation
```

---

## Test Categories

### 1. Functional Tests

Verify correct behavior of all components under normal and edge conditions.

**Coverage:**
- Core module tests (Types, Platform, Compiler, Assertions)
- Memory allocator tests (Linear, Pool, Stack allocators)
- Container tests (Vector, HashMap, String)
- Math tests (Vec2, Vec3, Vec4, Mat4)
- Threading tests (Job System, JobHandle, Workers)

### 2. Stress Tests

- **Memory:** 1M allocations, verify zero leaks, fragmentation < 0.1%
- **Containers:** Large dataset handling
- **Math:** Precision under extreme values
- **Threading:** 1k, 10k, 50k jobs with work-stealing

### 3. Benchmark Tests

Establish performance baselines for CI regression detection.

**Key benchmarks:**
- Allocator performance (allocation/deallocation throughput)
- Container insertion/lookup (Vector, HashMap)
- Job scheduling overhead (1k/10k/50k jobs)
- Job System work-stealing efficiency

---

## GitHub Actions CI

```yaml
name: Tests
on: [push, pull_request]
jobs:
  build-and-test:
    runs-on: ubuntu-latest
    strategy:
      matrix:
        compiler: [gcc-13, clang-17]
    steps:
      - uses: actions/checkout@v4
      - name: Install compiler
        run: sudo apt-get install ${{ matrix.compiler }}
      - name: Configure
        run: cmake -B build -DCMAKE_CXX_COMPILER=${{ matrix.compiler }}
      - name: Build
        run: cmake --build build
      - name: Run Tests
        run: ./build/CaffeineTest
      - name: Run Benchmarks
        run: ./build/CaffeineBenchmarks
```

---

## Success Criteria

- [x] Design approved
- [ ] Catch2 integrated
- [ ] All module tests functional
- [ ] Stress tests pass (1M allocs, 1k/10k/50k jobs)
- [ ] Benchmarks established
- [ ] CI pipeline green
- [ ] ThreadSanitizer (TSAN) clean on threading tests

---

## Testing Strategy

### TDD Workflow

All implementations follow Test-Driven Development:

1. **Write failing test** — Define expected behavior
2. **Run test** — Verify it fails with meaningful error
3. **Implement** — Write minimal code to pass test
4. **Run test** — Verify it passes
5. **Commit** — Atomic commit with test + implementation

### Stress Test Progression

**Threading Module (Job System):**

| Phase | Test | Benchmark | Target |
|-------|------|-----------|--------|
| Alpha | 1k jobs (mutex-based) | < 100ms | Correctness |
| Beta | 10k jobs (work-stealing) | < 500ms | Load balancing |
| Gold | 50k jobs (lock-free) | < 2s, TSAN clean | Performance |

---

## Integration with Build System

### CMakeLists.txt

```cmake
# src/CMakeLists.txt
enable_testing()
add_subdirectory(tests)
```

### Running Tests

```bash
# Run all tests
ctest --output-on-failure

# Run specific test
ctest -R "test_threading" -V

# Run benchmarks
./build/CaffeineBenchmarks
```

---

## Continuous Integration

- Tests run on **every commit** (gcc-13, clang-17)
- **Benchmarks** establish baselines for regression detection
- **ThreadSanitizer** (TSAN) verifies zero data races
- **AddressSanitizer** (ASAN) verifies memory safety

---

## Test Documentation

Each test file includes:
- Module description at top
- Test case names describing behavior
- Comments on non-obvious test logic
- Link to implementation in source code

---

## References

- [Catch2 Documentation](https://github.com/catchorg/Catch2)
- [C++20 Testing Best Practices](https://en.cppreference.com/)
- [Test-Driven Development](https://en.wikipedia.org/wiki/Test-driven_development)
