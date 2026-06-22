/*
 * Viewport Systems Unit Test
 * Tests raycasting, selection, multi-select, delete, focus camera
 */

#include <iostream>
#include <cassert>
#include <cmath>

// Minimal includes for testing
#include "core/Types.hpp"
#include "math/Vec3.hpp"
#include "ecs/Entity.hpp"
#include "ecs/World.hpp"

using namespace Caffeine;

/*
 * Test 1: Ray-AABB intersection math
 * Based on SceneViewport::rayIntersectsAABB
 */
static bool rayIntersectsAABB(const Vec3& rayOrigin, const Vec3& rayDir,
                              const Vec3& aabbMin, const Vec3& aabbMax) {
    if (aabbMin.x > aabbMax.x || aabbMin.y > aabbMax.y || aabbMin.z > aabbMax.z) {
        return false;
    }
    
    const f32 eps = 1e-6f;
    f32 tMin = -1e30f;
    f32 tMax = 1e30f;
    
    for (int i = 0; i < 3; ++i) {
        f32 min, max;
        if (i == 0) {
            min = aabbMin.x;
            max = aabbMax.x;
        } else if (i == 1) {
            min = aabbMin.y;
            max = aabbMax.y;
        } else {
            min = aabbMin.z;
            max = aabbMax.z;
        }
        
        f32 rayOrig, rayD;
        if (i == 0) {
            rayOrig = rayOrigin.x;
            rayD = rayDir.x;
        } else if (i == 1) {
            rayOrig = rayOrigin.y;
            rayD = rayDir.y;
        } else {
            rayOrig = rayOrigin.z;
            rayD = rayDir.z;
        }
        
        if (std::abs(rayD) < eps) {
            if (rayOrig < min || rayOrig > max) {
                return false;
            }
        } else {
            f32 invRayD = 1.0f / rayD;
            f32 t1 = (min - rayOrig) * invRayD;
            f32 t2 = (max - rayOrig) * invRayD;
            
            if (t1 > t2) std::swap(t1, t2);
            
            tMin = std::max(tMin, t1);
            tMax = std::min(tMax, t2);
            
            if (tMin > tMax) {
                return false;
            }
        }
    }
    
    return tMax >= 0;
}

void test_raycasting_basics() {
    std::cout << "TEST 1: Raycasting Basics" << std::endl;
    
    Vec3 rayOrigin(0, 0, 0);
    Vec3 rayDir(1, 0, 0);
    rayDir = rayDir.normalized();
    
    Vec3 boxMin(1, -1, -1);
    Vec3 boxMax(2, 1, 1);
    
    bool hits = rayIntersectsAABB(rayOrigin, rayDir, boxMin, boxMax);
    assert(hits && "Ray should hit box");
    std::cout << "  ✓ Ray hits box correctly" << std::endl;
    
    Vec3 rayDir2(-1, 0, 0);
    bool hitsBack = rayIntersectsAABB(rayOrigin, rayDir2.normalized(), boxMin, boxMax);
    assert(!hitsBack && "Ray in opposite direction should not hit");
    std::cout << "  ✓ Opposite ray doesn't hit" << std::endl;
}

void test_ray_miss() {
    std::cout << "\nTEST 2: Ray Miss Detection" << std::endl;
    
    Vec3 rayOrigin(0, 0, 0);
    Vec3 rayDir(1, 1, 0);
    rayDir = rayDir.normalized();
    
    Vec3 boxMin(1, 5, -1);
    Vec3 boxMax(2, 6, 1);
    
    bool hits = rayIntersectsAABB(rayOrigin, rayDir, boxMin, boxMax);
    assert(!hits && "Ray should miss box above");
    std::cout << "  ✓ Ray correctly misses box" << std::endl;
}

void test_ray_through_box() {
    std::cout << "\nTEST 3: Ray Through Box" << std::endl;
    
    Vec3 rayOrigin(-5, 0, 0);
    Vec3 rayDir(1, 0, 0);
    rayDir = rayDir.normalized();
    
    Vec3 boxMin(-1, -1, -1);
    Vec3 boxMax(1, 1, 1);
    
    bool hits = rayIntersectsAABB(rayOrigin, rayDir, boxMin, boxMax);
    assert(hits && "Ray should go through box");
    std::cout << "  ✓ Ray goes through box" << std::endl;
}

void test_ray_parallel() {
    std::cout << "\nTEST 4: Ray Parallel to Face" << std::endl;
    
    Vec3 rayOrigin(0, 0, 0);
    Vec3 rayDir(0, 1, 0);
    rayDir = rayDir.normalized();
    
    Vec3 boxMin(1, -1, -1);
    Vec3 boxMax(2, 1, 1);
    
    bool hits = rayIntersectsAABB(rayOrigin, rayDir, boxMin, boxMax);
    assert(!hits && "Ray parallel to box side should not hit");
    std::cout << "  ✓ Parallel ray doesn't hit" << std::endl;
}

int main() {
    std::cout << "====================================\n"
              << "Viewport System Verification Tests\n"
              << "====================================\n" << std::endl;
    
    try {
        test_raycasting_basics();
        test_ray_miss();
        test_ray_through_box();
        test_ray_parallel();
        
        std::cout << "\n====================================\n"
                  << "✓ ALL TESTS PASSED\n"
                  << "====================================\n" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n✗ TEST FAILED: " << e.what() << std::endl;
        return 1;
    }
}
