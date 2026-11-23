// Dryad natives for `caffeine.physics3d`; same behaviour as the Lua functions.
//   caffeine_physics3d_available() / _set_gravity(x, y, z)
//   caffeine_physics3d_set_surface_combine("blend" | "product") -> bool
//   caffeine_physics3d_raycast(ox, oy, oz, dx, dy, dz, maxDistance) -> {entity, x, y, z, nx, ny, nz, distance} or null
//   caffeine_physics3d_add_force / _add_impulse / _set_velocity (entity, x, y, z) -> bool
//   caffeine_physics3d_velocity(entity) -> {x, y, z} or null

#include "physics/PhysicsWorld3D.hpp"
#include "script/DryadNatives.hpp"

namespace Caffeine::Script {

using Args = std::vector<dryad::Value>;

namespace {
double num(const Args& a, size_t i) {
    if (i >= a.size()) return 0.0;
    if (a[i].is_integer()) return static_cast<double>(a[i].as_integer());
    if (a[i].is_float()) return a[i].as_float();
    return 0.0;
}
Vec3 vec(const Args& a, size_t i) {
    return Vec3(static_cast<f32>(num(a, i)), static_cast<f32>(num(a, i + 1)), static_cast<f32>(num(a, i + 2)));
}
}  // namespace

void bindPhysics3DNatives(dryad::Interpreter& interp, Physics3D::PhysicsWorld3D** physicsPtr) {
    auto physics = [physicsPtr]() -> Physics3D::PhysicsWorld3D* { return physicsPtr ? *physicsPtr : nullptr; };

    defineNative(interp, "caffeine_physics3d_available", [=](const Args&) {
        auto* p = physics();
        return dryad::Value(p && p->isAvailable()); });
    defineNative(interp, "caffeine_physics3d_set_gravity", [=](const Args& a) {
        if (auto* p = physics()) p->setGravity(vec(a, 0));
        return dryad::Value(); });
    defineNative(interp, "caffeine_physics3d_set_surface_combine", [=](const Args& a) {
        auto* p = physics();
        if (!p || a.empty() || !a[0].is_string()) return dryad::Value(false);
        const std::string mode = a[0].as_string();
        if (mode == "blend") p->setSurfaceCombine(Physics3D::PhysicsWorld3D::SurfaceCombine::Blend);
        else if (mode == "product") p->setSurfaceCombine(Physics3D::PhysicsWorld3D::SurfaceCombine::Product);
        else return dryad::Value(false);
        return dryad::Value(true); });
    defineNative(interp, "caffeine_physics3d_raycast", [=](const Args& a) {
        auto* p = physics();
        if (!p) return dryad::Value();
        const auto hit = p->raycast(vec(a, 0), vec(a, 3), static_cast<f32>(num(a, 6)));
        if (!hit.hit) return dryad::Value();
        dryad::Value o = dryad::Value::create_object();
        o.object_set("entity", dryad::Value(static_cast<int64_t>(hit.entityId)));
        o.object_set("x", dryad::Value(static_cast<double>(hit.point.x)));
        o.object_set("y", dryad::Value(static_cast<double>(hit.point.y)));
        o.object_set("z", dryad::Value(static_cast<double>(hit.point.z)));
        o.object_set("nx", dryad::Value(static_cast<double>(hit.normal.x)));
        o.object_set("ny", dryad::Value(static_cast<double>(hit.normal.y)));
        o.object_set("nz", dryad::Value(static_cast<double>(hit.normal.z)));
        o.object_set("distance", dryad::Value(static_cast<double>(hit.distance)));
        return o; });
    defineNative(interp, "caffeine_physics3d_add_force", [=](const Args& a) {
        auto* p = physics();
        return dryad::Value(p && p->addForce(static_cast<u32>(num(a, 0)), vec(a, 1))); });
    defineNative(interp, "caffeine_physics3d_add_impulse", [=](const Args& a) {
        auto* p = physics();
        return dryad::Value(p && p->addImpulse(static_cast<u32>(num(a, 0)), vec(a, 1))); });
    defineNative(interp, "caffeine_physics3d_set_velocity", [=](const Args& a) {
        auto* p = physics();
        return dryad::Value(p && p->setLinearVelocity(static_cast<u32>(num(a, 0)), vec(a, 1))); });
    defineNative(interp, "caffeine_physics3d_velocity", [=](const Args& a) {
        auto* p = physics();
        Vec3 v;
        if (!p || !p->linearVelocity(static_cast<u32>(num(a, 0)), v)) return dryad::Value();
        dryad::Value o = dryad::Value::create_object();
        o.object_set("x", dryad::Value(static_cast<double>(v.x)));
        o.object_set("y", dryad::Value(static_cast<double>(v.y)));
        o.object_set("z", dryad::Value(static_cast<double>(v.z)));
        return o; });
}

}  // namespace Caffeine::Script
