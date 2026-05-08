#pragma once

#include "core/Types.hpp"
#include "math/Vec2.hpp"

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

} // namespace Caffeine::Events
