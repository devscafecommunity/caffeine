#pragma once

#include "core/Types.hpp"
#include "math/Vec2.hpp"

namespace Caffeine::Physics2D {

using namespace Caffeine;

enum class ColliderShape : u8 { AABB, Circle };

struct PhysicsMaterial {
    f32 friction    = 0.5f;
    f32 restitution = 0.3f;

    static PhysicsMaterial ice()    { return { 0.05f, 0.1f }; }
    static PhysicsMaterial rubber() { return { 0.8f,  0.9f }; }
    static PhysicsMaterial metal()  { return { 0.6f,  0.3f }; }
    static PhysicsMaterial wood()   { return { 0.5f,  0.2f }; }
    static PhysicsMaterial stone()  { return { 0.7f,  0.1f }; }
};

struct RigidBody2D {
    f32  mass          = 1.0f;
    f32  restitution   = 0.3f;
    f32  friction      = 0.5f;
    f32  linearDamping = 0.0f;
    bool isKinematic   = false;
    bool lockRotation  = true;
    bool isSleeping    = false;
    f32  sleepTimer    = 0.0f;
};

struct Collider2D {
    Vec2          size      = { 32.0f, 32.0f };
    Vec2          offset    = { 0.0f,  0.0f  };
    f32           radius    = 16.0f;
    u32           layer     = 0;
    u32           layerMask = 0xFFFFFFFF;
    ColliderShape shape     = ColliderShape::AABB;
    bool          isStatic  = false;
    bool          isTrigger = false;
    bool          isOneWay  = false;
    u8            debugColor[4] = { 80, 200, 255, 220 };
};

}
