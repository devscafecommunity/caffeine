#define CATCH_CONFIG_MAIN
#include "catch.hpp"
#include "../src/memory/LinearAllocator.hpp"
#include "../src/memory/PoolAllocator.hpp"
#include "../src/containers/Vector.hpp"
#include "../src/math/Vec2.hpp"
#include "../src/math/Mat4.hpp"

#include <chrono>

using namespace Caffeine;

BENCHMARK("LinearAllocator - 1K allocations") {
    LinearAllocator allocator(1024 * 1024);
    for (int i = 0; i < 1000; i++) {
        allocator.alloc(64, 8);
    }
    allocator.reset();
}

BENCHMARK("LinearAllocator - 10K allocations") {
    LinearAllocator allocator(10 * 1024 * 1024);
    for (int i = 0; i < 10000; i++) {
        allocator.alloc(64, 8);
    }
    allocator.reset();
}

BENCHMARK("PoolAllocator - 1K allocations") {
    PoolAllocator allocator(64 * 1024, 64, 8);
    void* slots[1000];
    for (int i = 0; i < 1000; i++) {
        slots[i] = allocator.alloc(64);
    }
    for (int i = 0; i < 1000; i++) {
        allocator.free(slots[i]);
    }
}

BENCHMARK("Vector - 1K pushBack") {
    Vector<int> vec;
    vec.reserve(1024);
    for (int i = 0; i < 1000; i++) {
        vec.pushBack(i);
    }
}

BENCHMARK("Vector - 10K pushBack") {
    Vector<int> vec;
    vec.reserve(10240);
    for (int i = 0; i < 10000; i++) {
        vec.pushBack(i);
    }
}

BENCHMARK("Vec2 - dot product") {
    Vec2 v1(1.0f, 2.0f);
    Vec2 v2(3.0f, 4.0f);
    volatile float result = 0.0f;
    for (int i = 0; i < 10000; i++) {
        result = v1.dot(v2);
    }
}

BENCHMARK("Mat4 - identity") {
    volatile Mat4 result;
    for (int i = 0; i < 1000; i++) {
        result = Mat4::identity();
    }
}

BENCHMARK("Mat4 - multiply") {
    Mat4 a = Mat4::identity();
    Mat4 b = Mat4::identity();
    volatile Mat4 result;
    for (int i = 0; i < 1000; i++) {
        result = a * b;
    }
}

BENCHMARK("Mat4 - transformPoint") {
    Mat4 translation = Mat4::translation(10.0f, 20.0f, 30.0f);
    Vec3 point(1.0f, 2.0f, 3.0f);
    volatile Vec3 result;
    for (int i = 0; i < 10000; i++) {
        result = translation.transformPoint(point);
    }
}