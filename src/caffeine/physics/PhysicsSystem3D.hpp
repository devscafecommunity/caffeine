#pragma once

#include "ecs/ComponentQuery.hpp"
#include "ecs/Components.hpp"
#include "ecs/Components3D.hpp"
#include "ecs/ISystem.hpp"
#include "ecs/World.hpp"
#include "math/Quat.hpp"
#include "physics/PhysicsWorld3D.hpp"
#include "scene/HierarchySystem.hpp"
#include "terrain/TerrainCache.hpp"

#include "events/EventBus.hpp"
#include "events/Events.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace Caffeine::Physics3D {

class PhysicsSystem3D : public ECS::ISystem {
public:
    explicit PhysicsSystem3D(Events::EventBus* eventBus = nullptr) : m_eventBus(eventBus) {}

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
            readPose(world, entity, desc.position, desc.rotation, desc.scale);

            m_world.upsertBody(desc);

            // A dynamic body whose entity was moved by something else (a script, the editor, a respawn) since
            // the physics last wrote it: follow the entity instead of overwriting it next frame.
            if (body.bodyType == BodyType3D::Dynamic) {
                auto written = m_written.find(entity.id());
                if (written != m_written.end() && movedSince(world, entity, written->second, desc)) {
                    m_world.teleport(entity.id(), desc.position, desc.rotation);
                }
            }
        });

        syncTerrains(world, live);

        m_world.removeMissing(live);
        for (auto it = m_written.begin(); it != m_written.end();) {
            it = std::find(live.begin(), live.end(), it->first) == live.end() ? m_written.erase(it) : std::next(it);
        }
        m_world.step(dt);

        world.forEach<RigidBody3D, Collider3D>(query, [&](ECS::Entity entity, RigidBody3D& body, Collider3D&) {
            if (body.bodyType != BodyType3D::Dynamic) return;
            BodyState state;
            if (!m_world.stateOf(entity.id(), state)) return;
            writePose(world, entity, state.position, state.rotation);
            Written written;
            written.state = state;
            if (const auto* transform = world.get<ECS::Transform>(entity)) written.euler = transform->rotation;
            m_written[entity.id()] = written;
        });

        publishContacts();
    }

    /// Every terrain with `buildCollisionMesh` on is a static height field at full sample resolution (the
    /// decimated `collisionSampleStep` mesh is for queries, not for resting bodies on). Edits to the heights
    /// (`dataRevision`) rebuild it; the entity's transform moves, turns and scales it.
    void syncTerrains(ECS::World& world, std::vector<u32>& live) {
        ECS::ComponentQuery terrains;
        terrains.with<ECS::TerrainComponent>();
        world.forEach<ECS::TerrainComponent>(terrains, [&](ECS::Entity entity, ECS::TerrainComponent& terrain) {
            if (!terrain.buildCollisionMesh) return;
            const Terrain::TerrainHeightmap* heightmap = Terrain::TerrainCache::instance().heightmapFor(entity);
            if (!heightmap || heightmap->empty()) return;

            TerrainDesc desc;
            desc.entityId = entity.id();
            desc.resolutionX = heightmap->resolutionX();
            desc.resolutionZ = heightmap->resolutionZ();
            desc.sizeX = terrain.worldSizeX;
            desc.sizeZ = terrain.worldSizeZ;
            desc.maxHeight = terrain.maxHeight;
            desc.heights = heightmap->heights().data();
            desc.revision = terrain.dataRevision;
            readPose(world, entity, desc.position, desc.rotation, desc.scale);
            if (const auto* body = world.get<RigidBody3D>(entity)) {
                desc.friction = body->friction;
                desc.restitution = body->restitution;
            }
            live.push_back(entity.id());
            m_world.upsertTerrain(desc);
        });
    }

    void reset() {
        m_world.clear();
        m_written.clear();
    }
    PhysicsWorld3D& world() { return m_world; }

private:
    /// What the physics last wrote to a body's entity. A turn stored as Euler angles is compared as those
    /// same angles (the quaternion round trip is not exact), one stored as a quaternion as the quaternion.
    struct Written {
        BodyState state;
        Vec3 euler{};
    };

    static bool movedSince(ECS::World& world, ECS::Entity entity, const Written& written, const BodyDesc& now) {
        const Vec3 d = now.position - written.state.position;
        if (d.x * d.x + d.y * d.y + d.z * d.z > 1e-6f) return true;
        const bool plainEuler = !hasParent(world, entity) && !world.get<ECS::Rotation3D>(entity);
        if (plainEuler) {
            const auto* transform = world.get<ECS::Transform>(entity);
            if (!transform) return false;
            const Vec3 e = transform->rotation - written.euler;
            return e.x * e.x + e.y * e.y + e.z * e.z > 1e-6f;
        }
        const Vec4& a = written.state.rotation;
        const Vec4& b = now.rotation;
        return std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w) < 0.99999f;
    }

    static bool hasParent(ECS::World& world, ECS::Entity entity) {
        const auto* parent = world.get<Scene::Parent>(entity);
        return parent && parent->parent.isValid() && parent->parent.id() != entity.id();
    }

    /// The entity's pose in world space: its own components, or, under a parent, its resolved world matrix
    /// (the Hierarchy composes parents into children, and bodies live in world space).
    static void readPose(ECS::World& world, ECS::Entity entity, Vec3& position, Vec4& rotation, Vec3& scale) {
        if (hasParent(world, entity)) {
            const Mat4 m = Scene::computeWorldMatrix(world, entity);
            position = m.transformPoint(Vec3(0.0f, 0.0f, 0.0f));
            Mat4 r = m;
            Vec3 axes[3];
            for (int c = 0; c < 3; ++c) {
                axes[c] = Vec3(m(0, c), m(1, c), m(2, c));
                const f32 len = std::max(std::sqrt(axes[c].x * axes[c].x + axes[c].y * axes[c].y + axes[c].z * axes[c].z), 1e-6f);
                r(0, c) = m(0, c) / len;
                r(1, c) = m(1, c) / len;
                r(2, c) = m(2, c) / len;
                scale = c == 0 ? Vec3(len, scale.y, scale.z) : c == 1 ? Vec3(scale.x, len, scale.z) : Vec3(scale.x, scale.y, len);
            }
            const Quat q = Quat::fromMatrix(r);
            rotation = Vec4(q.x, q.y, q.z, q.w);
            return;
        }
        if (auto* p = world.get<ECS::Position3D>(entity)) {
            position = p->position;
        } else if (auto* transform = world.get<ECS::Transform>(entity)) {
            position = transform->position;
        }
        if (auto* r = world.get<ECS::Rotation3D>(entity)) {
            rotation = r->quaternion;
        } else if (auto* transform = world.get<ECS::Transform>(entity)) {
            constexpr f32 kDegToRad = 3.14159265f / 180.0f;
            const Quat q = Quat::fromEuler(transform->rotation.x * kDegToRad, transform->rotation.y * kDegToRad,
                                           transform->rotation.z * kDegToRad);
            rotation = Vec4(q.x, q.y, q.z, q.w);
        }
        if (auto* sc = world.get<ECS::Scale3D>(entity)) {
            scale = sc->scale;
        } else if (auto* transform = world.get<ECS::Transform>(entity)) {
            scale = transform->scale;
        }
    }

    /// Writes a world-space pose back to the entity's local components (undoing the parent for a child).
    static void writePose(ECS::World& world, ECS::Entity entity, Vec3 position, Vec4 rotation) {
        if (hasParent(world, entity)) {
            const auto* parent = world.get<Scene::Parent>(entity);
            const Mat4 parentWorld = Scene::computeWorldMatrix(world, parent->parent);
            const Mat4 local = parentWorld.inverted() * (Mat4::translation(position) * Scene::quatToMat4(rotation));
            position = local.transformPoint(Vec3(0.0f, 0.0f, 0.0f));
            Mat4 r = local;
            for (int c = 0; c < 3; ++c) {
                const f32 len = std::max(std::sqrt(local(0, c) * local(0, c) + local(1, c) * local(1, c) + local(2, c) * local(2, c)), 1e-6f);
                r(0, c) = local(0, c) / len;
                r(1, c) = local(1, c) / len;
                r(2, c) = local(2, c) / len;
            }
            const Quat q = Quat::fromMatrix(r);
            rotation = Vec4(q.x, q.y, q.z, q.w);
        }
        if (auto* p = world.get<ECS::Position3D>(entity)) {
            p->position = position;
        }
        if (auto* transform = world.get<ECS::Transform>(entity)) {
            transform->position = position;
            const Quat q(rotation.x, rotation.y, rotation.z, rotation.w);
            constexpr f32 kRadToDeg = 180.0f / 3.14159265f;
            transform->rotation = q.toEuler() * kRadToDeg;
        }
        if (auto* r = world.get<ECS::Rotation3D>(entity)) {
            r->quaternion = rotation;
        }
        if (auto* worldTransform = world.get<Scene::WorldTransform>(entity)) {
            worldTransform->matrix = Scene::computeWorldMatrix(world, entity);
        }
    }

    void publishContacts() {
        auto events = m_world.takeContactEvents();
        if (!m_eventBus) return;
        for (const auto& ev : events) {
            if (ev.trigger) {
                Events::OnTrigger3D out;
                out.triggerEntity = ev.entityA;
                out.otherEntity = ev.entityB;
                out.entered = ev.begin;
                m_eventBus->publishDeferred(out);
            } else {
                Events::OnCollision3D out;
                out.entityA = ev.entityA;
                out.entityB = ev.entityB;
                out.contactPoint = ev.point;
                out.normal = ev.normal;
                out.impulse = ev.impulse;
                out.entered = ev.begin;
                m_eventBus->publishDeferred(out);
            }
        }
    }

    PhysicsWorld3D m_world;
    Events::EventBus* m_eventBus = nullptr;
    /// The pose the physics last wrote to each dynamic body's entity.
    std::unordered_map<u32, Written> m_written;
};

}  // namespace Caffeine::Physics3D
