#define CATCH_CONFIG_MAIN
#include "catch.hpp"
#include "../src/memory/LinearAllocator.hpp"
#include "../src/memory/PoolAllocator.hpp"
#include "../src/memory/StackAllocator.hpp"

using namespace Caffeine;

TEST_CASE("LinearAllocator - Basic Allocation", "[memory][linear]") {
    LinearAllocator allocator(1024);

    REQUIRE(allocator.totalSize() == 1024);
    REQUIRE(allocator.usedMemory() == 0);
    REQUIRE(allocator.allocationCount() == 0);

    void* ptr = allocator.alloc(64, 8);
    REQUIRE(ptr != nullptr);
    REQUIRE(allocator.usedMemory() == 64);
    REQUIRE(allocator.allocationCount() == 1);
}

TEST_CASE("LinearAllocator - Multiple Allocations", "[memory][linear]") {
    LinearAllocator allocator(1024);

    void* p1 = allocator.alloc(64, 8);
    void* p2 = allocator.alloc(128, 8);
    void* p3 = allocator.alloc(32, 8);

    REQUIRE(p1 != nullptr);
    REQUIRE(p2 != nullptr);
    REQUIRE(p3 != nullptr);
    REQUIRE(allocator.usedMemory() == 224);
    REQUIRE(allocator.allocationCount() == 3);
}

TEST_CASE("LinearAllocator - Reset", "[memory][linear]") {
    LinearAllocator allocator(1024);

    allocator.alloc(64, 8);
    allocator.alloc(128, 8);
    REQUIRE(allocator.usedMemory() == 192);

    allocator.reset();
    REQUIRE(allocator.usedMemory() == 0);
    REQUIRE(allocator.allocationCount() == 0);
}

TEST_CASE("LinearAllocator - Alignment", "[memory][linear]") {
    LinearAllocator allocator(256);

    void* p1 = allocator.alloc(16, 16);
    void* p2 = allocator.alloc(16, 8);
    void* p3 = allocator.alloc(16, 16);

    REQUIRE(p1 != nullptr);
    REQUIRE(p2 != nullptr);
    REQUIRE(p3 != nullptr);
    REQUIRE(allocator.usedMemory() <= 48);
}

TEST_CASE("LinearAllocator - Out Of Memory", "[memory][linear]") {
    LinearAllocator allocator(64);

    void* p1 = allocator.alloc(64, 8);
    REQUIRE(p1 != nullptr);

    void* p2 = allocator.alloc(8, 8);
    REQUIRE(p2 == nullptr);
}

TEST_CASE("PoolAllocator - Basic Allocation", "[memory][pool]") {
    PoolAllocator allocator(256, 32, 8);

    REQUIRE(allocator.totalSize() == 256);
    REQUIRE(allocator.slotSize() == 32);
    REQUIRE(allocator.maxSlots() == 8);
    REQUIRE(allocator.freeSlots() == 8);
    REQUIRE(allocator.usedMemory() == 0);
}

TEST_CASE("PoolAllocator - Allocate And Free", "[memory][pool]") {
    PoolAllocator allocator(256, 32, 8);

    void* slot1 = allocator.alloc(32);
    void* slot2 = allocator.alloc(32);
    void* slot3 = allocator.alloc(32);

    REQUIRE(slot1 != nullptr);
    REQUIRE(slot2 != nullptr);
    REQUIRE(slot3 != nullptr);
    REQUIRE(allocator.freeSlots() == 5);
    REQUIRE(allocator.usedMemory() == 96);

    allocator.free(slot2);
    REQUIRE(allocator.freeSlots() == 6);
    REQUIRE(allocator.usedMemory() == 64);
}

TEST_CASE("PoolAllocator - Reset", "[memory][pool]") {
    PoolAllocator allocator(256, 32, 8);

    allocator.alloc(32);
    allocator.alloc(32);
    allocator.alloc(32);

    allocator.reset();
    REQUIRE(allocator.freeSlots() == 8);
    REQUIRE(allocator.usedMemory() == 0);
}

TEST_CASE("PoolAllocator - Out Of Slots", "[memory][pool]") {
    PoolAllocator allocator(96, 32, 8);

    for (int i = 0; i < 3; i++) {
        REQUIRE(allocator.alloc(32) != nullptr);
    }
    REQUIRE(allocator.freeSlots() == 0);

    void* fail = allocator.alloc(32);
    REQUIRE(fail == nullptr);
}

TEST_CASE("StackAllocator - Basic Allocation", "[memory][stack]") {
    StackAllocator allocator(1024);

    REQUIRE(allocator.totalSize() == 1024);
    REQUIRE(allocator.usedMemory() == 0);
}

TEST_CASE("StackAllocator - Set Marker And FreeToMarker", "[memory][stack]") {
    StackAllocator allocator(1024);

    allocator.alloc(64, 8);
    Marker m1 = allocator.setMarker();
    allocator.alloc(128, 8);
    allocator.alloc(32, 8);
    REQUIRE(allocator.usedMemory() == 224);

    allocator.freeToMarker(m1);
    REQUIRE(allocator.usedMemory() == 64);
}

TEST_CASE("StackAllocator - Multiple Markers", "[memory][stack]") {
    StackAllocator allocator(1024);

    allocator.alloc(32, 8);
    Marker m1 = allocator.setMarker();
    allocator.alloc(64, 8);
    Marker m2 = allocator.setMarker();
    allocator.alloc(128, 8);

    REQUIRE(allocator.usedMemory() == 224);

    allocator.freeToMarker(m1);
    REQUIRE(allocator.usedMemory() == 32);

    allocator.alloc(64, 8);
    Marker m3 = allocator.setMarker();
    allocator.alloc(32, 8);
    REQUIRE(allocator.usedMemory() == 128);
}

TEST_CASE("StackAllocator - Reset", "[memory][stack]") {
    StackAllocator allocator(1024);

    allocator.alloc(512, 8);
    REQUIRE(allocator.usedMemory() == 512);

    allocator.reset();
    REQUIRE(allocator.usedMemory() == 0);
}

TEST_CASE("Memory Stress - LinearAllocator 1M Allocations", "[memory][stress]") {
    LinearAllocator allocator(64 * 1024 * 1024);

    constexpr int ITERATIONS = 1000000;
    void* pointers[100];
    
    for (int i = 0; i < ITERATIONS; i++) {
        void* ptr = allocator.alloc(64, 8);
        REQUIRE(ptr != nullptr);
        
        if (i % 10000 == 0) {
            pointers[i / 10000 % 100] = ptr;
        }
    }

    REQUIRE(allocator.allocationCount() == ITERATIONS);
    REQUIRE(allocator.usedMemory() > 0);

    allocator.reset();
    REQUIRE(allocator.usedMemory() == 0);
    REQUIRE(allocator.allocationCount() == 0);
}

TEST_CASE("Memory Stress - PoolAllocator 10K Allocations", "[memory][stress]") {
    PoolAllocator allocator(1024 * 1024, 64, 8);

    constexpr int ITERATIONS = 10000;
    void* pointers[ITERATIONS];

    for (int i = 0; i < ITERATIONS; i++) {
        pointers[i] = allocator.alloc(64);
        REQUIRE(pointers[i] != nullptr);
    }

    REQUIRE(allocator.freeSlots() == 0);

    for (int i = 0; i < ITERATIONS; i++) {
        allocator.free(pointers[i]);
    }

    REQUIRE(allocator.freeSlots() == ITERATIONS);
    REQUIRE(allocator.usedMemory() == 0);
}

TEST_CASE("Memory Stress - Fragmentation Test", "[memory][stress]") {
    LinearAllocator allocator(1024 * 1024);

    constexpr int ITERATIONS = 1000;
    for (int i = 0; i < ITERATIONS; i++) {
        allocator.alloc(i % 128 + 1, 8);
    }

    usize usedBeforeReset = allocator.usedMemory();
    usize peakBeforeReset = allocator.peakMemory();

    allocator.reset();
    allocator.alloc(usedBeforeReset, 8);

    usize available = allocator.availableMemory();
    usize originalUsed = usedBeforeReset - allocator.usedMemory();

    float fragmentation = (available > 0) ? (float)originalUsed / (float)usedBeforeReset : 0.0f;
    REQUIRE(fragmentation < 0.001f);
}