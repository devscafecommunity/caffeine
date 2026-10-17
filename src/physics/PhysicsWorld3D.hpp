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

private:
    struct Impl;
    Impl* m_impl = nullptr;
};

}  // namespace Caffeine::Physics3D
