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
    /// How the two surfaces of a contact are merged. `Blend` (the default): friction is the geometric mean and
    /// restitution the larger of the two, like the 2D system. `Product`: Bullet's own rule, both multiplied
    /// (for scenes tuned against it: a bouncy ball needs a bouncy floor too).
    enum class SurfaceCombine : u8 { Blend, Product };
    void setSurfaceCombine(SurfaceCombine mode);
    SurfaceCombine surfaceCombine() const;
    void upsertBody(const BodyDesc& desc);
    /// Adds or refreshes a terrain's static height-field collider. A terrain with no samples is removed.
    void upsertTerrain(const TerrainDesc& desc);
    void removeBody(u32 entityId);
    void removeMissing(const std::vector<u32>& liveEntityIds);
    /// Moves a body somewhere else without the blend between poses (a respawn, a script setting its position).
    /// Velocity is kept; the body wakes. False for unknown or non-dynamic bodies.
    bool teleport(u32 entityId, const Vec3& position, const Vec4& rotation);
    /// Advances the world by `dt` seconds of *game* time. The simulation itself only ever runs in fixed
    /// steps (`fixedStep()`), however long or short the frames are: the remainder carries to the next call,
    /// at most `kMaxStepsPerFrame` steps run per call (a long hitch is dropped, not caught up), and
    /// `stateOf` blends the last two steps so motion stays smooth between them.
    void step(f32 dt);
    static constexpr u32 kMaxStepsPerFrame = 8;

    /// A pair of bodies that started or stopped touching during a `step`. `trigger` is true when one of them
    /// is a trigger volume (it detects overlap but does not push).
    struct ContactEvent {
        u32 entityA = 0;
        u32 entityB = 0;
        bool trigger = false;
        bool begin = true;
        Vec3 point{};
        Vec3 normal{};  // from A to B
        f32 impulse = 0.0f;
    };
    /// The contact changes since the last call (the world keeps at most one batch).
    std::vector<ContactEvent> takeContactEvents();
    void setFixedStep(f32 seconds);
    f32 fixedStep() const;
    /// How many fixed steps the last `step` call ran (0 on a frame shorter than the step).
    u32 lastStepCount() const;
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
