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

/// A terrain as a static height field. `heights` are the terrain's normalised samples (0..1, `resolutionX` fast,
/// `resolutionZ` slow) and stay owned by the caller: the world copies them when the collider is (re)built, which
/// happens when `revision` or any other field changes. The grid is centred on `position`, like the terrain mesh.
struct TerrainDesc {
    u32 entityId = 0;
    Vec3 position{};
    Vec4 rotation{0.0f, 0.0f, 0.0f, 1.0f};
    Vec3 scale{1.0f, 1.0f, 1.0f};
    u32 resolutionX = 0;
    u32 resolutionZ = 0;
    f32 sizeX = 0.0f;
    f32 sizeZ = 0.0f;
    f32 maxHeight = 1.0f;
    const f32* heights = nullptr;
    u32 revision = 0;
    f32 friction = 0.8f;
    f32 restitution = 0.0f;
};

struct BodyState {
    Vec3 position{};
    Vec4 rotation{0.0f, 0.0f, 0.0f, 1.0f};
};

}  // namespace Caffeine::Physics3D
