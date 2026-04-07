#include "../src/Caffeine.hpp"

#include <cstdio>

int main() {
    Caffeine::LinearAllocator linear(1024);
    
    void* p1 = linear.alloc(64, 8);
    void* p2 = linear.alloc(128, 8);
    void* p3 = linear.alloc(32, 8);
    
    printf("Linear Allocator Test:\n");
    printf("  Total: %zu bytes\n", linear.totalSize());
    printf("  Used: %zu bytes\n", linear.usedMemory());
    printf("  Allocations: %zu\n", linear.allocationCount());
    
    linear.reset();
    printf("  After reset - Used: %zu bytes\n", linear.usedMemory());
    
    Caffeine::PoolAllocator pool(1024, 32, 8);
    
    void* slot1 = pool.alloc(32);
    void* slot2 = pool.alloc(32);
    void* slot3 = pool.alloc(32);
    
    printf("\nPool Allocator Test:\n");
    printf("  Total: %zu bytes\n", pool.totalSize());
    printf("  Used: %zu bytes\n", pool.usedMemory());
    printf("  Free slots: %zu\n", pool.freeSlots());
    
    pool.free(slot2);
    printf("  After free - Free slots: %zu\n", pool.freeSlots());
    
    Caffeine::StackAllocator stack(1024);
    
    auto marker = stack.setMarker();
    void* sp1 = stack.alloc(64, 8);
    void* sp2 = stack.alloc(128, 8);
    
    printf("\nStack Allocator Test:\n");
    printf("  Used: %zu bytes\n", stack.usedMemory());
    
    stack.freeToMarker(marker);
    printf("  After freeToMarker - Used: %zu bytes\n", stack.usedMemory());
    
    Caffeine::Vec2 v1(1.0f, 2.0f);
    Caffeine::Vec2 v2(3.0f, 4.0f);
    Caffeine::Vec2 v3 = v1 + v2;
    
    printf("\nVec2 Test:\n");
    printf("  v1 + v2 = (%.2f, %.2f)\n", v3.x, v3.y);
    printf("  v1.length() = %.2f\n", v1.length());
    
    Caffeine::Mat4 m = Caffeine::Mat4::identity();
    printf("\nMat4 Test:\n");
    printf("  Identity diagonal[0][0] = %.2f\n", m(0, 0));
    
    printf("\nAll tests passed!\n");
    return 0;
}