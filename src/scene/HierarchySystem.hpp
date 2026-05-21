#pragma once

#include "scene/SceneComponents.hpp"
#include "ecs/World.hpp"
#include "ecs/Components.hpp"
#include "ecs/Components3D.hpp"
#include "math/Mat4.hpp"
#include <math.h>

namespace Caffeine::Scene {

namespace {

inline Mat4 buildLocalTRS(const ECS::Transform& t) {
    static constexpr float DEG2RAD = 3.14159265f / 180.f;
    Mat4 T = Mat4::translation(t.position.x, t.position.y, t.position.z);
    Mat4 R = Mat4::rotationZ(t.rotation.z * DEG2RAD)
           * Mat4::rotationY(t.rotation.y * DEG2RAD)
           * Mat4::rotationX(t.rotation.x * DEG2RAD);
    Mat4 S = Mat4::scale(t.scale.x, t.scale.y, t.scale.z);
    return T * R * S;
}

inline Mat4 quatToMat4(const Vec4& q) {
    float x = q.x, y = q.y, z = q.z, w = q.w;
    Mat4 R = Mat4::identity();
    R(0,0) = 1.f - 2.f*(y*y + z*z);  R(0,1) = 2.f*(x*y - w*z);          R(0,2) = 2.f*(x*z + w*y);
    R(1,0) = 2.f*(x*y + w*z);          R(1,1) = 1.f - 2.f*(x*x + z*z);  R(1,2) = 2.f*(y*z - w*x);
    R(2,0) = 2.f*(x*z - w*y);          R(2,1) = 2.f*(y*z + w*x);          R(2,2) = 1.f - 2.f*(x*x + y*y);
    return R;
}

inline Mat4 buildLocalTRS_3D(const ECS::Position3D* p, const ECS::Rotation3D* r, const ECS::Scale3D* s) {
    Mat4 T = p ? Mat4::translation(p->position.x, p->position.y, p->position.z) : Mat4::identity();
    Mat4 R = r ? quatToMat4(r->quaternion) : Mat4::identity();
    Mat4 S = s ? Mat4::scale(s->scale.x, s->scale.y, s->scale.z) : Mat4::identity();
    return T * R * S;
}

} // anonymous namespace

// Computes Scene::WorldTransform for all entities in the world.
// Must be called once per frame before rendering.
// Supports arbitrary nesting depth up to MAX_DEPTH levels.
inline void propagateTransforms(ECS::World& world) {
    // Pass 1: seed WorldTransform with each entity's local TRS.
    // For entities with a parent this is just the initial value;
    // Pass 2+ will overwrite it with the correct world matrix.
    {
        ECS::ComponentQuery q;
        q.with<ECS::Transform>();
        world.forEach<ECS::Transform>(q, [&](ECS::Entity e, ECS::Transform& t) {
            WorldTransform& wt = world.add<WorldTransform>(e);
            wt.matrix = buildLocalTRS(t);
        });
    }
    {
        ECS::ComponentQuery q;
        q.with<ECS::Position3D>();
        world.forEach<ECS::Position3D>(q, [&](ECS::Entity e, ECS::Position3D& p) {
            if (world.has<ECS::Transform>(e)) return;
            auto* r = world.get<ECS::Rotation3D>(e);
            auto* s = world.get<ECS::Scale3D>(e);
            WorldTransform& wt = world.add<WorldTransform>(e);
            wt.matrix = buildLocalTRS_3D(&p, r, s);
        });
    }

    // Pass 2..N: propagate parent WorldTransform down to children.
    // Each iteration correctly handles one additional level of nesting,
    // regardless of entity iteration order within a pass.
    static constexpr int MAX_DEPTH = 8;
    for (int depth = 0; depth < MAX_DEPTH; ++depth) {
        ECS::ComponentQuery q;
        q.with<Scene::Parent>();
        world.forEach<Scene::Parent>(q, [&](ECS::Entity child, Scene::Parent& pc) {
            if (!pc.parent.isValid()) return;
            auto* parentWT = world.get<WorldTransform>(pc.parent);
            if (!parentWT) return;
            auto* childWT = world.get<WorldTransform>(child);
            if (!childWT) return;

            if (auto* t = world.get<ECS::Transform>(child)) {
                childWT->matrix = parentWT->matrix * buildLocalTRS(*t);
            } else {
                auto* p3 = world.get<ECS::Position3D>(child);
                if (!p3) return;
                auto* r3 = world.get<ECS::Rotation3D>(child);
                auto* s3 = world.get<ECS::Scale3D>(child);
                childWT->matrix = parentWT->matrix * buildLocalTRS_3D(p3, r3, s3);
            }
        });
    }
}

}
