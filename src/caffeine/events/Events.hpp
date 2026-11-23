#pragma once

#include "core/Types.hpp"
#include "math/Vec2.hpp"
#include "math/Vec3.hpp"

namespace Caffeine::Events {

struct OnEntityCreated {
    u32 entityId;
};

struct OnEntityDestroyed {
    u32 entityId;
};

struct OnCollision2D {
    u32  entityA;
    u32  entityB;
    Vec2 contactPoint;
    Vec2 normal;
    f32  impulse;
};

struct OnTrigger2D {
    u32  triggerEntity;
    u32  otherEntity;
    bool entered; // true = enter, false = exit
};

/// 3D contacts, reported when a pair of bodies starts or stops touching (not every frame they rest).
struct OnCollision3D {
    u32  entityA;
    u32  entityB;
    Vec3 contactPoint;
    Vec3 normal;   // from A to B
    f32  impulse;  // total impulse the solver applied at the contact
    bool entered;  // true = began touching, false = stopped
};

/// A body entering or leaving a trigger volume (a 3D collider with `isTrigger`).
struct OnTrigger3D {
    u32  triggerEntity;
    u32  otherEntity;
    bool entered;
};

struct OnHealthChanged {
    u32 entityId;
    f32 previousHealth;
    f32 currentHealth;
    f32 maxHealth;
};

struct OnDeath {
    u32 entityId;
    u32 killerEntityId; // 0 = environmental / no killer
};

struct OnScoreChanged {
    u32 playerId;
    i32 previousScore;
    i32 currentScore;
    i32 delta;
};

struct OnLevelLoaded {
    u32 levelId;
    c8  levelName[64];
};

struct OnLevelUnloaded {
    u32 levelId;
};

struct OnActionPressed {
    u32 actionId; // hashed action name
    u32 playerId;
};

struct OnActionReleased {
    u32 actionId;
    u32 playerId;
};

/// Fired when a MapSurface2DComponent needs content for a logical chunk (game supplies data).
struct OnMapSurfaceChunkRequest {
    u32 surfaceEntityId = 0;
    i32 chunkX = 0;
    i32 chunkY = 0;
    f32 worldMinX = 0.0f;
    f32 worldMinY = 0.0f;
    f32 chunkSize = 0.0f;
};

} // namespace Caffeine::Events
