#pragma once

#include "core/Types.hpp"
#include "math/Vec3.hpp"
#include "math/Vec4.hpp"

namespace Caffeine::Physics3D {

using namespace Caffeine;

enum class ColliderShape3D : u8 { Box, Sphere, Capsule };
enum class BodyType3D : u8 { Dynamic, Kinematic, Static };

struct RigidBody3D {
    f32 mass = 1.0f;
    f32 linearDamping = 0.05f;
    f32 angularDamping = 0.05f;
    f32 friction = 0.5f;
    f32 restitution = 0.2f;
    BodyType3D bodyType = BodyType3D::Dynamic;
    bool lockRotation = false;
};

struct Collider3D {
    ColliderShape3D shape = ColliderShape3D::Box;
    // Box: half extents. Sphere: x = radius. Capsule: x = radius, y = total height.
    Vec3 halfExtents = {0.5f, 0.5f, 0.5f};
    Vec3 offset{};
    bool isTrigger = false;
};

struct BodyDesc {
    u32 entityId = 0;
    Vec3 position{};
    Vec4 rotation{0.0f, 0.0f, 0.0f, 1.0f};
    Vec3 scale{1.0f, 1.0f, 1.0f};
    ColliderShape3D shape = ColliderShape3D::Box;
    Vec3 halfExtents{0.5f, 0.5f, 0.5f};
    Vec3 offset{};
    f32 mass = 1.0f;
    f32 friction = 0.5f;
    f32 restitution = 0.2f;
    f32 linearDamping = 0.05f;
    f32 angularDamping = 0.05f;
    BodyType3D bodyType = BodyType3D::Dynamic;
    bool lockRotation = false;
    bool isTrigger = false;
    bool resetPose = true;
};

struct BodyState {
    Vec3 position{};
    Vec4 rotation{0.0f, 0.0f, 0.0f, 1.0f};
};

}  // namespace Caffeine::Physics3D
