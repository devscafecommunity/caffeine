#pragma once

#include "physics/PhysicsComponents3D.hpp"

#include <vector>

namespace Caffeine::Physics3D {

// Bullet3 backend. ECS never includes bt* headers; bodies live here, keyed by entity id.
class PhysicsWorld3D {
public:
    PhysicsWorld3D();
    ~PhysicsWorld3D();

    PhysicsWorld3D(const PhysicsWorld3D&) = delete;
    PhysicsWorld3D& operator=(const PhysicsWorld3D&) = delete;

    void clear();
    void setGravity(const Vec3& gravity);
    void upsertBody(const BodyDesc& desc);
    void removeBody(u32 entityId);
    void removeMissing(const std::vector<u32>& liveEntityIds);
    void step(f32 dt);
    bool stateOf(u32 entityId, BodyState& out) const;
    bool isAvailable() const;

    struct RayHit {
        bool hit = false;
        u32 entityId = 0;
        Vec3 point{};
        Vec3 normal{0.0f, 1.0f, 0.0f};
        f32 distance = 0.0f;
    };
    /// First body along the ray (`direction` need not be normalised). Triggers are ignored.
    RayHit raycast(const Vec3& origin, const Vec3& direction, f32 maxDistance) const;
    /// Force and impulse act on dynamic bodies only and wake them. Return false for unknown
    /// or non-dynamic bodies.
    bool addForce(u32 entityId, const Vec3& force);
    bool addImpulse(u32 entityId, const Vec3& impulse);
    bool setLinearVelocity(u32 entityId, const Vec3& velocity);
    bool linearVelocity(u32 entityId, Vec3& out) const;

private:
    struct Impl;
    Impl* m_impl = nullptr;
};

}  // namespace Caffeine::Physics3D
