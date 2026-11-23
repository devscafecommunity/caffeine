#include "physics/PhysicsWorld3D.hpp"

#include <sol/sol.hpp>

namespace Caffeine::Script {

namespace P = Physics3D;

// `caffeine.physics3d`: queries and forces on the Bullet world. Everything is a safe no-op
// (false / nil) when the host runs without a 3D physics world.
void registerPhysics3DBindings(sol::state& lua, P::PhysicsWorld3D** physicsPtr) {
    auto physics = [physicsPtr]() -> P::PhysicsWorld3D* { return physicsPtr ? *physicsPtr : nullptr; };
    sol::table t = lua["caffeine"]["physics3d"].get_or_create<sol::table>();

    t["isAvailable"] = [physics]() { auto* p = physics(); return p && p->isAvailable(); };
    // "blend" (friction: geometric mean, restitution: the larger) or "product" (Bullet's rule).
    t["setSurfaceCombine"] = [physics](const std::string& mode) {
        auto* p = physics();
        if (!p) return false;
        if (mode == "blend") p->setSurfaceCombine(P::PhysicsWorld3D::SurfaceCombine::Blend);
        else if (mode == "product") p->setSurfaceCombine(P::PhysicsWorld3D::SurfaceCombine::Product);
        else return false;
        return true;
    };
    t["setGravity"] = [physics](f32 x, f32 y, f32 z) { if (auto* p = physics()) p->setGravity(Vec3(x, y, z)); };
    t["raycast"] = [physics, &lua](f32 ox, f32 oy, f32 oz, f32 dx, f32 dy, f32 dz, f32 maxDistance) -> sol::object {
        auto* p = physics();
        if (!p) return sol::nil;
        const auto hit = p->raycast(Vec3(ox, oy, oz), Vec3(dx, dy, dz), maxDistance);
        if (!hit.hit) return sol::nil;
        sol::table r = lua.create_table();
        r["entity"] = hit.entityId;
        r["x"] = hit.point.x; r["y"] = hit.point.y; r["z"] = hit.point.z;
        r["nx"] = hit.normal.x; r["ny"] = hit.normal.y; r["nz"] = hit.normal.z;
        r["distance"] = hit.distance;
        return r;
    };
    t["addForce"] = [physics](u32 e, f32 x, f32 y, f32 z) { auto* p = physics(); return p && p->addForce(e, Vec3(x, y, z)); };
    t["addImpulse"] = [physics](u32 e, f32 x, f32 y, f32 z) { auto* p = physics(); return p && p->addImpulse(e, Vec3(x, y, z)); };
    t["setVelocity"] = [physics](u32 e, f32 x, f32 y, f32 z) { auto* p = physics(); return p && p->setLinearVelocity(e, Vec3(x, y, z)); };
    t["velocity"] = [physics, &lua](u32 e) -> sol::object {
        auto* p = physics();
        Vec3 v;
        if (!p || !p->linearVelocity(e, v)) return sol::nil;
        sol::table r = lua.create_table();
        r["x"] = v.x; r["y"] = v.y; r["z"] = v.z;
        return r;
    };
}

}  // namespace Caffeine::Script
