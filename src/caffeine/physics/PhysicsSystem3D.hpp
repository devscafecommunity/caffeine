#pragma once

#include "ecs/ComponentQuery.hpp"
#include "ecs/Components.hpp"
#include "ecs/Components3D.hpp"
#include "ecs/ISystem.hpp"
#include "ecs/World.hpp"
#include "math/Quat.hpp"
#include "physics/PhysicsWorld3D.hpp"
#include "scene/HierarchySystem.hpp"

#include <vector>

namespace Caffeine::Physics3D {

class PhysicsSystem3D : public ECS::ISystem {
public:
    void onUpdate(ECS::World& world, f32 dt) override {
        if (!m_world.isAvailable()) return;

        std::vector<u32> live;
        ECS::ComponentQuery query;
        query.with<RigidBody3D>();
        query.with<Collider3D>();
        world.forEach<RigidBody3D, Collider3D>(query, [&](ECS::Entity entity, RigidBody3D& body, Collider3D& collider) {
            live.push_back(entity.id());

            BodyDesc desc;
            desc.entityId = entity.id();
            desc.shape = collider.shape;
            desc.halfExtents = collider.halfExtents;
            desc.offset = collider.offset;
            desc.isTrigger = collider.isTrigger;
            desc.mass = body.mass;
            desc.friction = body.friction;
            desc.restitution = body.restitution;
            desc.linearDamping = body.linearDamping;
            desc.angularDamping = body.angularDamping;
            desc.bodyType = body.bodyType;
            desc.lockRotation = body.lockRotation;
            desc.resetPose = body.bodyType != BodyType3D::Dynamic;

            if (auto* position = world.get<ECS::Position3D>(entity)) {
                desc.position = position->position;
            } else if (auto* transform = world.get<ECS::Transform>(entity)) {
                desc.position = transform->position;
            }
            if (auto* rotation = world.get<ECS::Rotation3D>(entity)) {
                desc.rotation = rotation->quaternion;
            } else if (auto* transform = world.get<ECS::Transform>(entity)) {
                constexpr f32 kDegToRad = 3.14159265f / 180.0f;
                const Quat q = Quat::fromEuler(transform->rotation.x * kDegToRad,
                                               transform->rotation.y * kDegToRad,
                                               transform->rotation.z * kDegToRad);
                desc.rotation = Vec4(q.x, q.y, q.z, q.w);
            }
            if (auto* scale = world.get<ECS::Scale3D>(entity)) {
                desc.scale = scale->scale;
            } else if (auto* transform = world.get<ECS::Transform>(entity)) {
                desc.scale = transform->scale;
            }

            m_world.upsertBody(desc);
        });

        m_world.removeMissing(live);
        m_world.step(dt);

        world.forEach<RigidBody3D, Collider3D>(query, [&](ECS::Entity entity, RigidBody3D& body, Collider3D&) {
            if (body.bodyType != BodyType3D::Dynamic) return;
            BodyState state;
            if (!m_world.stateOf(entity.id(), state)) return;

            if (auto* position = world.get<ECS::Position3D>(entity)) {
                position->position = state.position;
            }
            if (auto* transform = world.get<ECS::Transform>(entity)) {
                transform->position = state.position;
                const Quat q(state.rotation.x, state.rotation.y, state.rotation.z, state.rotation.w);
                const Vec3 euler = q.toEuler();
                constexpr f32 kRadToDeg = 180.0f / 3.14159265f;
                transform->rotation = euler * kRadToDeg;
            }
            if (auto* rotation = world.get<ECS::Rotation3D>(entity)) {
                rotation->quaternion = state.rotation;
            }
            if (auto* worldTransform = world.get<Scene::WorldTransform>(entity)) {
                worldTransform->matrix = Scene::computeWorldMatrix(world, entity);
            }
        });
    }

    void reset() { m_world.clear(); }
    PhysicsWorld3D& world() { return m_world; }

private:
    PhysicsWorld3D m_world;
};

}  // namespace Caffeine::Physics3D
