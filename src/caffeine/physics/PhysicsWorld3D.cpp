#include "physics/PhysicsWorld3D.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#ifdef CF_HAS_BULLET3
#include <btBulletDynamicsCommon.h>
#include <BulletCollision/NarrowPhaseCollision/btManifoldPoint.h>
#include <BulletCollision/CollisionShapes/btHeightfieldTerrainShape.h>
#endif

namespace Caffeine::Physics3D {

namespace {

u32 fingerprint(const BodyDesc& desc) {
    auto mix = [](u32 h, u32 v) { return h * 16777619u ^ v; };
    auto bits = [](f32 v) {
        u32 out = 0;
        static_assert(sizeof(f32) == sizeof(u32));
        std::memcpy(&out, &v, sizeof(out));
        return out;
    };
    u32 h = 2166136261u;
    h = mix(h, static_cast<u32>(desc.shape));
    h = mix(h, static_cast<u32>(desc.bodyType));
    h = mix(h, desc.lockRotation ? 1u : 0u);
    h = mix(h, desc.isTrigger ? 1u : 0u);
    h = mix(h, bits(desc.mass));
    h = mix(h, bits(desc.linearDamping));
    h = mix(h, bits(desc.angularDamping));
    h = mix(h, bits(desc.friction));
    h = mix(h, bits(desc.restitution));
    h = mix(h, bits(desc.halfExtents.x));
    h = mix(h, bits(desc.halfExtents.y));
    h = mix(h, bits(desc.halfExtents.z));
    h = mix(h, bits(desc.offset.x));
    h = mix(h, bits(desc.offset.y));
    h = mix(h, bits(desc.offset.z));
    h = mix(h, bits(desc.scale.x));
    h = mix(h, bits(desc.scale.y));
    h = mix(h, bits(desc.scale.z));
    return h;
}

u32 terrainFingerprint(const TerrainDesc& desc) {
    auto mix = [](u32 h, u32 v) { return h * 16777619u ^ v; };
    auto bits = [](f32 v) {
        u32 out = 0;
        std::memcpy(&out, &v, sizeof(out));
        return out;
    };
    u32 h = 2166136261u;
    h = mix(h, desc.resolutionX);
    h = mix(h, desc.resolutionZ);
    h = mix(h, desc.revision);
    h = mix(h, bits(desc.sizeX));
    h = mix(h, bits(desc.sizeZ));
    h = mix(h, bits(desc.maxHeight));
    h = mix(h, bits(desc.scale.x));
    h = mix(h, bits(desc.scale.y));
    h = mix(h, bits(desc.scale.z));
    h = mix(h, bits(desc.friction));
    h = mix(h, bits(desc.restitution));
    return h;
}

}  // namespace

#ifdef CF_HAS_BULLET3

/// Bullet multiplies the two surfaces' friction and restitution, so a bouncy ball on a floor left at the default
/// barely bounces and ice on stone sticks. The same rule as the 2D system and Box2D instead: friction is the
/// geometric mean, restitution the larger of the two. Bodies opt in with CF_CUSTOM_MATERIAL_CALLBACK.
bool combineSurfaces(btManifoldPoint& point, const btCollisionObjectWrapper* a, int, int,
                     const btCollisionObjectWrapper* b, int, int) {
    const btCollisionObject* first = a->getCollisionObject();
    const btCollisionObject* second = b->getCollisionObject();
    point.m_combinedFriction = std::sqrt(std::max(first->getFriction(), 0.0f) * std::max(second->getFriction(), 0.0f));
    point.m_combinedRestitution = std::max(first->getRestitution(), second->getRestitution());
    return true;
}

/// A ray ignores trigger volumes: they detect, they are not there to hit.
struct SolidRayCallback : btCollisionWorld::ClosestRayResultCallback {
    using ClosestRayResultCallback::ClosestRayResultCallback;
    bool needsCollision(btBroadphaseProxy* proxy) const override {
        const auto* object = static_cast<const btCollisionObject*>(proxy->m_clientObject);
        if (object && (object->getCollisionFlags() & btCollisionObject::CF_NO_CONTACT_RESPONSE)) return false;
        return ClosestRayResultCallback::needsCollision(proxy);
    }
};

struct PhysicsWorld3D::Impl {
    struct Record {
        btRigidBody* body = nullptr;
        btCollisionShape* shape = nullptr;
        btDefaultMotionState* motion = nullptr;
        u32 fingerprint = 0;
        BodyType3D type = BodyType3D::Dynamic;
        Vec3 offset{};
        /// The pose before the last fixed step: `stateOf` blends it with the current one.
        btTransform previous;
        bool hasPrevious = false;
        /// Force asked for since the last `step` call (a force holds for the frame it is asked in), and the
        /// momentum it became (force x frame time) while it waits for a fixed step to be applied in.
        btVector3 force{0, 0, 0};
        btVector3 pendingImpulse{0, 0, 0};
        /// A terrain's samples, which the height-field shape reads in place.
        std::unique_ptr<std::vector<f32>> terrainData;
    };

    btDefaultCollisionConfiguration* config = nullptr;
    btCollisionDispatcher* dispatcher = nullptr;
    btBroadphaseInterface* broadphase = nullptr;
    btSequentialImpulseConstraintSolver* solver = nullptr;
    btDiscreteDynamicsWorld* world = nullptr;
    std::unordered_map<u32, Record> bodies;
    f32 fixedStep = 1.0f / 60.0f;
    f32 accumulator = 0.0f;
    u32 lastSteps = 0;
    SurfaceCombine combine = SurfaceCombine::Blend;

    /// Bodies take part in the custom merge only in `Blend` mode; otherwise Bullet's own rule applies.
    void applyCombineFlag(btRigidBody* body) const {
        int flags = body->getCollisionFlags();
        flags = combine == SurfaceCombine::Blend ? (flags | btCollisionObject::CF_CUSTOM_MATERIAL_CALLBACK)
                                                 : (flags & ~btCollisionObject::CF_CUSTOM_MATERIAL_CALLBACK);
        body->setCollisionFlags(flags);
    }
    /// Pairs touching after the last step (key = lower id << 32 | higher id) and the changes not yet taken.
    std::unordered_map<u64, ContactEvent> touching;
    std::vector<ContactEvent> pendingEvents;

    /// Compares the contacts the last step left with the ones before it.
    void collectContacts() {
        std::unordered_map<u64, ContactEvent> now;
        const int count = dispatcher->getNumManifolds();
        for (int i = 0; i < count; ++i) {
            btPersistentManifold* manifold = dispatcher->getManifoldByIndexInternal(i);
            const auto* bodyA = manifold->getBody0();
            const auto* bodyB = manifold->getBody1();
            const bool trigger = ((bodyA->getCollisionFlags() | bodyB->getCollisionFlags()) &
                                  btCollisionObject::CF_NO_CONTACT_RESPONSE) != 0;
            ContactEvent ev;
            bool touchingNow = false;
            for (int c = 0; c < manifold->getNumContacts(); ++c) {
                const btManifoldPoint& pt = manifold->getContactPoint(c);
                if (pt.getDistance() > 0.01f) continue;
                if (!touchingNow || pt.getDistance() < 0.0f) {
                    ev.point = Vec3(pt.m_positionWorldOnB.x(), pt.m_positionWorldOnB.y(), pt.m_positionWorldOnB.z());
                    // Bullet's normal points from B to A; the event reports A to B.
                    ev.normal = Vec3(-pt.m_normalWorldOnB.x(), -pt.m_normalWorldOnB.y(), -pt.m_normalWorldOnB.z());
                }
                ev.impulse += pt.getAppliedImpulse();
                touchingNow = true;
            }
            if (!touchingNow) continue;
            u32 a = static_cast<u32>(bodyA->getUserIndex());
            u32 b = static_cast<u32>(bodyB->getUserIndex());
            if (a == b) continue;
            // Triggers report as (trigger, other).
            if (trigger && !(bodyA->getCollisionFlags() & btCollisionObject::CF_NO_CONTACT_RESPONSE)) std::swap(a, b);
            else if (!trigger && a > b) std::swap(a, b);
            ev.entityA = a;
            ev.entityB = b;
            ev.trigger = trigger;
            const u64 key = trigger ? ((static_cast<u64>(a) << 32) | b)
                                    : ((static_cast<u64>(std::min(a, b)) << 32) | std::max(a, b));
            now[key] = ev;
        }
        for (const auto& [key, ev] : now) {
            if (!touching.count(key)) {
                ContactEvent out = ev;
                out.begin = true;
                pendingEvents.push_back(out);
            }
        }
        for (const auto& [key, ev] : touching) {
            if (!now.count(key)) {
                ContactEvent out = ev;
                out.begin = false;
                pendingEvents.push_back(out);
            }
        }
        touching = std::move(now);
    }

    Impl() {
        config = new btDefaultCollisionConfiguration();
        dispatcher = new btCollisionDispatcher(config);
        broadphase = new btDbvtBroadphase();
        solver = new btSequentialImpulseConstraintSolver();
        world = new btDiscreteDynamicsWorld(dispatcher, broadphase, solver, config);
        world->setGravity(btVector3(0.0f, -9.81f, 0.0f));
        gContactAddedCallback = combineSurfaces;
    }

    ~Impl() {
        clear();
        delete world;
        delete solver;
        delete broadphase;
        delete dispatcher;
        delete config;
    }

    void destroyRecord(Record& record) {
        if (record.body) {
            world->removeRigidBody(record.body);
            delete record.body;
        }
        delete record.motion;
        delete record.shape;
        record.body = nullptr;
        record.motion = nullptr;
        record.shape = nullptr;
        record.terrainData.reset();
    }

    /// A terrain that changed under resting bodies: they must not sleep on a surface that is no longer there.
    void wakeDynamicBodies() {
        for (auto& [_, record] : bodies) {
            if (record.type == BodyType3D::Dynamic) record.body->activate(true);
        }
    }

    void clear() {
        for (auto& [_, record] : bodies) {
            destroyRecord(record);
        }
        bodies.clear();
    }

    static btTransform makeTransform(const BodyDesc& desc) {
        btQuaternion rotation(desc.rotation.x, desc.rotation.y, desc.rotation.z, desc.rotation.w);
        const btVector3 offset = quatRotate(rotation, btVector3(desc.offset.x, desc.offset.y, desc.offset.z));
        btTransform tr;
        tr.setIdentity();
        tr.setOrigin(btVector3(desc.position.x, desc.position.y, desc.position.z) + offset);
        tr.setRotation(rotation);
        return tr;
    }

    static btCollisionShape* makeShape(const BodyDesc& desc) {
        const Vec3 scale(std::max(std::abs(desc.scale.x), 0.001f),
                         std::max(std::abs(desc.scale.y), 0.001f),
                         std::max(std::abs(desc.scale.z), 0.001f));
        switch (desc.shape) {
            case ColliderShape3D::Sphere: {
                const f32 radius = std::max(desc.halfExtents.x, 0.01f) * std::max(scale.x, std::max(scale.y, scale.z));
                return new btSphereShape(radius);
            }
            case ColliderShape3D::Capsule: {
                const f32 radius = std::max(desc.halfExtents.x, 0.01f) * std::max(scale.x, scale.z);
                const f32 totalHeight = std::max(desc.halfExtents.y, radius * 2.0f + 0.02f) * scale.y;
                const f32 cylinder = std::max(0.01f, totalHeight - radius * 2.0f);
                return new btCapsuleShape(radius, cylinder);
            }
            case ColliderShape3D::Box:
            default: {
                const Vec3 half(std::max(desc.halfExtents.x * scale.x, 0.01f),
                                std::max(desc.halfExtents.y * scale.y, 0.01f),
                                std::max(desc.halfExtents.z * scale.z, 0.01f));
                return new btBoxShape(btVector3(half.x, half.y, half.z));
            }
        }
    }
};

PhysicsWorld3D::PhysicsWorld3D() : m_impl(new Impl()) {}

PhysicsWorld3D::~PhysicsWorld3D() { delete m_impl; }

void PhysicsWorld3D::clear() {
    m_impl->clear();
    m_impl->accumulator = 0.0f;
    m_impl->touching.clear();
    m_impl->pendingEvents.clear();
}

void PhysicsWorld3D::setSurfaceCombine(SurfaceCombine mode) {
    m_impl->combine = mode;
    for (auto& [_, record] : m_impl->bodies) m_impl->applyCombineFlag(record.body);
}

PhysicsWorld3D::SurfaceCombine PhysicsWorld3D::surfaceCombine() const { return m_impl->combine; }

void PhysicsWorld3D::setGravity(const Vec3& gravity) {
    m_impl->world->setGravity(btVector3(gravity.x, gravity.y, gravity.z));
}

void PhysicsWorld3D::upsertBody(const BodyDesc& desc) {
    const u32 fp = fingerprint(desc);
    auto it = m_impl->bodies.find(desc.entityId);
    if (it != m_impl->bodies.end() && it->second.fingerprint == fp) {
        if (desc.bodyType != BodyType3D::Dynamic || desc.resetPose) {
            // Only a pose that really changed is pushed in: a still floor must not be re-set (and so woken)
            // every frame, and a moving kinematic body lets Bullet derive its speed from the motion.
            const btTransform tr = Impl::makeTransform(desc);
            btTransform now;
            it->second.motion->getWorldTransform(now);
            const bool moved = (now.getOrigin() - tr.getOrigin()).length2() > 1e-10f ||
                               now.getRotation().angle(tr.getRotation()) > 1e-5f;
            if (moved) {
                it->second.body->setWorldTransform(tr);
                it->second.motion->setWorldTransform(tr);
                it->second.previous = tr;
                if (desc.bodyType != BodyType3D::Kinematic) {
                    it->second.body->setLinearVelocity(btVector3(0, 0, 0));
                    it->second.body->setAngularVelocity(btVector3(0, 0, 0));
                }
                it->second.body->activate(true);
                if (desc.bodyType == BodyType3D::Static) m_impl->wakeDynamicBodies();
            }
        }
        return;
    }

    if (it != m_impl->bodies.end()) {
        m_impl->destroyRecord(it->second);
        m_impl->bodies.erase(it);
    }

    Impl::Record record;
    record.shape = Impl::makeShape(desc);
    record.fingerprint = fp;
    record.type = desc.bodyType;
    record.offset = desc.offset;
    const btTransform tr = Impl::makeTransform(desc);
    record.motion = new btDefaultMotionState(tr);

    f32 mass = (desc.bodyType == BodyType3D::Dynamic) ? std::max(desc.mass, 0.001f) : 0.0f;
    btVector3 inertia(0, 0, 0);
    if (mass > 0.0f) record.shape->calculateLocalInertia(mass, inertia);

    btRigidBody::btRigidBodyConstructionInfo info(mass, record.motion, record.shape, inertia);
    info.m_friction = desc.friction;
    info.m_restitution = desc.restitution;
    info.m_linearDamping = desc.linearDamping;
    info.m_angularDamping = desc.angularDamping;
    // A ball or a capsule on its side must stop rolling, not coast for ever on a flat floor.
    if (desc.shape != ColliderShape3D::Box) info.m_rollingFriction = 0.03f;
    record.body = new btRigidBody(info);
    record.body->setUserIndex(static_cast<int>(desc.entityId));
    record.previous = tr;
    if (desc.bodyType == BodyType3D::Dynamic && !desc.isTrigger) {
        // A fast body must not tunnel through a thin surface (a terrain has no thickness): sweep it once
        // it moves farther than half its smallest side in one step.
        btVector3 lo, hi;
        btTransform identity;
        identity.setIdentity();
        record.shape->getAabb(identity, lo, hi);
        const btVector3 size = hi - lo;
        const f32 smallest = std::max(0.02f, std::min(size.x(), std::min(size.y(), size.z())) * 0.5f);
        record.body->setCcdMotionThreshold(smallest * 0.5f);
        record.body->setCcdSweptSphereRadius(smallest * 0.5f);
    }
    if (desc.isTrigger) {
        record.body->setCollisionFlags(record.body->getCollisionFlags() | btCollisionObject::CF_NO_CONTACT_RESPONSE);
    }
    if (desc.bodyType == BodyType3D::Kinematic) {
        record.body->setCollisionFlags(record.body->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
        record.body->setActivationState(DISABLE_DEACTIVATION);
    }
    if (desc.lockRotation) record.body->setAngularFactor(btVector3(0, 0, 0));

    m_impl->applyCombineFlag(record.body);
    m_impl->world->addRigidBody(record.body);
    m_impl->bodies.emplace(desc.entityId, std::move(record));
}

void PhysicsWorld3D::upsertTerrain(const TerrainDesc& desc) {
    if (!desc.heights || desc.resolutionX < 2 || desc.resolutionZ < 2 || desc.sizeX <= 0.0f ||
        desc.sizeZ <= 0.0f) {
        removeBody(desc.entityId);
        return;
    }
    const u32 fp = terrainFingerprint(desc);
    const f32 scaleX = std::max(std::abs(desc.scale.x), 0.001f);
    const f32 scaleY = std::max(std::abs(desc.scale.y), 0.001f);
    const f32 scaleZ = std::max(std::abs(desc.scale.z), 0.001f);

    auto it = m_impl->bodies.find(desc.entityId);
    f32 middle = 0.0f;  // normalised height of the shape's local origin
    if (it == m_impl->bodies.end() || it->second.fingerprint != fp) {
        if (it != m_impl->bodies.end()) {
            m_impl->destroyRecord(it->second);
            m_impl->bodies.erase(it);
        }
        Impl::Record record;
        record.fingerprint = fp;
        record.type = BodyType3D::Static;
        const size_t count = static_cast<size_t>(desc.resolutionX) * desc.resolutionZ;
        record.terrainData = std::make_unique<std::vector<f32>>(desc.heights, desc.heights + count);
        const auto [lo, hi] = std::minmax_element(record.terrainData->begin(), record.terrainData->end());
        const f32 low = *lo;
        const f32 high = std::max(*hi, low + 1e-4f);
        auto* shape = new btHeightfieldTerrainShape(static_cast<int>(desc.resolutionX),
                                                    static_cast<int>(desc.resolutionZ),
                                                    record.terrainData->data(), 1.0f, low, high, 1, PHY_FLOAT,
                                                    false);
        shape->setLocalScaling(btVector3(desc.sizeX / static_cast<f32>(desc.resolutionX - 1) * scaleX,
                                         desc.maxHeight * scaleY,
                                         desc.sizeZ / static_cast<f32>(desc.resolutionZ - 1) * scaleZ));
        record.shape = shape;
        record.offset = Vec3(0.0f, (low + high) * 0.5f * desc.maxHeight * scaleY, 0.0f);
        BodyDesc pose;
        pose.position = desc.position;
        pose.rotation = desc.rotation;
        pose.offset = record.offset;
        const btTransform tr = Impl::makeTransform(pose);
        record.motion = new btDefaultMotionState(tr);
        record.previous = tr;
        btRigidBody::btRigidBodyConstructionInfo info(0.0f, record.motion, record.shape, btVector3(0, 0, 0));
        info.m_friction = desc.friction;
        info.m_restitution = desc.restitution;
        record.body = new btRigidBody(info);
        record.body->setUserIndex(static_cast<int>(desc.entityId));
        record.body->setCollisionFlags(record.body->getCollisionFlags() | btCollisionObject::CF_STATIC_OBJECT);
        m_impl->applyCombineFlag(record.body);
        m_impl->world->addRigidBody(record.body);
        m_impl->bodies.emplace(desc.entityId, std::move(record));
        m_impl->wakeDynamicBodies();
        return;
    }

    // Same samples: follow the terrain if its entity moved.
    Impl::Record& record = it->second;
    middle = record.offset.y;
    BodyDesc pose;
    pose.position = desc.position;
    pose.rotation = desc.rotation;
    pose.offset = Vec3(0.0f, middle, 0.0f);
    const btTransform tr = Impl::makeTransform(pose);
    const btTransform& now = record.body->getWorldTransform();
    if ((now.getOrigin() - tr.getOrigin()).length2() > 1e-10f ||
        now.getRotation().angle(tr.getRotation()) > 1e-5f) {
        record.body->setWorldTransform(tr);
        record.motion->setWorldTransform(tr);
        record.previous = tr;
        record.body->activate(true);
        m_impl->wakeDynamicBodies();
    }
}

void PhysicsWorld3D::removeBody(u32 entityId) {
    auto it = m_impl->bodies.find(entityId);
    if (it == m_impl->bodies.end()) return;
    m_impl->destroyRecord(it->second);
    m_impl->bodies.erase(it);
}

void PhysicsWorld3D::removeMissing(const std::vector<u32>& liveEntityIds) {
    const std::unordered_set<u32> live(liveEntityIds.begin(), liveEntityIds.end());
    std::vector<u32> stale;
    for (const auto& [id, _] : m_impl->bodies) {
        if (!live.count(id)) stale.push_back(id);
    }
    for (u32 id : stale) removeBody(id);
}

void PhysicsWorld3D::step(f32 dt) {
    m_impl->lastSteps = 0;
    if (dt <= 0.0f) return;
    Impl& impl = *m_impl;
    impl.accumulator += dt;
    u32 steps = static_cast<u32>((impl.accumulator + 1e-5f) / impl.fixedStep);
    if (steps > kMaxStepsPerFrame) {
        // A hitch longer than the cap: keep the world running at real speed rather than in slow motion
        // for seconds, by dropping the time that cannot be simulated.
        steps = kMaxStepsPerFrame;
        impl.accumulator = static_cast<f32>(steps) * impl.fixedStep;
    }
    for (auto& [_, record] : impl.bodies) {
        record.pendingImpulse += record.force * dt;
        record.force = btVector3(0, 0, 0);
    }
    for (u32 i = 0; i < steps; ++i) {
        for (auto& [_, record] : impl.bodies) {
            if (record.type != BodyType3D::Dynamic) continue;
            record.motion->getWorldTransform(record.previous);
            record.hasPrevious = true;
            // The frame's force, as momentum, shared evenly by the steps it ran: the same push at any frame rate.
            record.body->applyCentralImpulse(record.pendingImpulse / static_cast<f32>(steps));
        }
        // One call is one step: the world's own accumulator is bypassed (`maxSubSteps` 0).
        impl.world->stepSimulation(impl.fixedStep, 0, impl.fixedStep);
    }
    impl.accumulator = std::max(0.0f, impl.accumulator - static_cast<f32>(steps) * impl.fixedStep);
    impl.lastSteps = steps;
    if (steps > 0) {
        impl.collectContacts();
        for (auto& [_, record] : impl.bodies) record.pendingImpulse = btVector3(0, 0, 0);
    }
}

bool PhysicsWorld3D::teleport(u32 entityId, const Vec3& position, const Vec4& rotation) {
    auto it = m_impl->bodies.find(entityId);
    if (it == m_impl->bodies.end() || it->second.type != BodyType3D::Dynamic) return false;
    BodyDesc pose;
    pose.position = position;
    pose.rotation = rotation;
    pose.offset = it->second.offset;
    const btTransform tr = Impl::makeTransform(pose);
    it->second.body->setWorldTransform(tr);
    it->second.motion->setWorldTransform(tr);
    it->second.previous = tr;
    it->second.hasPrevious = false;
    it->second.body->activate(true);
    return true;
}

std::vector<PhysicsWorld3D::ContactEvent> PhysicsWorld3D::takeContactEvents() {
    return std::move(std::exchange(m_impl->pendingEvents, {}));
}

void PhysicsWorld3D::setFixedStep(f32 seconds) {
    m_impl->fixedStep = std::clamp(seconds, 1.0f / 1000.0f, 1.0f / 10.0f);
}

f32 PhysicsWorld3D::fixedStep() const { return m_impl->fixedStep; }

u32 PhysicsWorld3D::lastStepCount() const { return m_impl->lastSteps; }

bool PhysicsWorld3D::stateOf(u32 entityId, BodyState& out) const {
    auto it = m_impl->bodies.find(entityId);
    if (it == m_impl->bodies.end() || it->second.type != BodyType3D::Dynamic) return false;
    btTransform tr;
    it->second.motion->getWorldTransform(tr);
    btVector3 position = tr.getOrigin();
    btQuaternion rot = tr.getRotation();
    if (it->second.hasPrevious) {
        // Between two fixed steps the picture is the blend of the last two: `alpha` is how far into the
        // next step the clock already is.
        const f32 alpha = std::clamp(m_impl->accumulator / m_impl->fixedStep, 0.0f, 1.0f);
        const btQuaternion before = it->second.previous.getRotation();
        position = it->second.previous.getOrigin().lerp(position, alpha);
        rot = before.slerp(rot, alpha);
    }
    const btVector3 worldOffset = quatRotate(rot, btVector3(it->second.offset.x, it->second.offset.y, it->second.offset.z));
    const btVector3 origin = position - worldOffset;
    out.position = Vec3(origin.x(), origin.y(), origin.z());
    out.rotation = Vec4(rot.x(), rot.y(), rot.z(), rot.w());
    return true;
}

bool PhysicsWorld3D::isAvailable() const { return true; }

PhysicsWorld3D::RayHit PhysicsWorld3D::raycast(const Vec3& origin, const Vec3& direction,
                                               f32 maxDistance) const {
    RayHit out;
    const f32 len = std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
    if (len < 1e-6f || maxDistance <= 0.0f) return out;
    const btVector3 from(origin.x, origin.y, origin.z);
    const btVector3 to = from + btVector3(direction.x, direction.y, direction.z) * (maxDistance / len);
    SolidRayCallback cb(from, to);
    m_impl->world->rayTest(from, to, cb);
    if (!cb.hasHit()) return out;
    const auto* body = btRigidBody::upcast(cb.m_collisionObject);
    if (!body) return out;
    if (body->getCollisionFlags() & btCollisionObject::CF_NO_CONTACT_RESPONSE) return out;  // trigger
    out.hit = true;
    out.entityId = static_cast<u32>(body->getUserIndex());
    out.point = Vec3(cb.m_hitPointWorld.x(), cb.m_hitPointWorld.y(), cb.m_hitPointWorld.z());
    out.normal = Vec3(cb.m_hitNormalWorld.x(), cb.m_hitNormalWorld.y(), cb.m_hitNormalWorld.z());
    out.distance = cb.m_closestHitFraction * maxDistance;
    return out;
}

bool PhysicsWorld3D::addForce(u32 entityId, const Vec3& force) {
    auto it = m_impl->bodies.find(entityId);
    if (it == m_impl->bodies.end() || it->second.type != BodyType3D::Dynamic) return false;
    it->second.body->activate(true);
    it->second.force += btVector3(force.x, force.y, force.z);
    return true;
}

bool PhysicsWorld3D::addImpulse(u32 entityId, const Vec3& impulse) {
    auto it = m_impl->bodies.find(entityId);
    if (it == m_impl->bodies.end() || it->second.type != BodyType3D::Dynamic) return false;
    it->second.body->activate(true);
    it->second.body->applyCentralImpulse(btVector3(impulse.x, impulse.y, impulse.z));
    return true;
}

bool PhysicsWorld3D::setLinearVelocity(u32 entityId, const Vec3& velocity) {
    auto it = m_impl->bodies.find(entityId);
    if (it == m_impl->bodies.end() || it->second.type != BodyType3D::Dynamic) return false;
    it->second.body->activate(true);
    it->second.body->setLinearVelocity(btVector3(velocity.x, velocity.y, velocity.z));
    return true;
}

bool PhysicsWorld3D::linearVelocity(u32 entityId, Vec3& out) const {
    auto it = m_impl->bodies.find(entityId);
    if (it == m_impl->bodies.end() || it->second.type != BodyType3D::Dynamic) return false;
    const btVector3 v = it->second.body->getLinearVelocity();
    out = Vec3(v.x(), v.y(), v.z());
    return true;
}

#else

struct PhysicsWorld3D::Impl {};

PhysicsWorld3D::PhysicsWorld3D() : m_impl(new Impl()) {}
PhysicsWorld3D::~PhysicsWorld3D() { delete m_impl; }
void PhysicsWorld3D::clear() {}
void PhysicsWorld3D::setGravity(const Vec3&) {}
void PhysicsWorld3D::setSurfaceCombine(SurfaceCombine) {}
PhysicsWorld3D::SurfaceCombine PhysicsWorld3D::surfaceCombine() const { return SurfaceCombine::Blend; }
void PhysicsWorld3D::upsertBody(const BodyDesc&) {}
void PhysicsWorld3D::upsertTerrain(const TerrainDesc&) {}
void PhysicsWorld3D::removeBody(u32) {}
void PhysicsWorld3D::removeMissing(const std::vector<u32>&) {}
void PhysicsWorld3D::step(f32) {}
void PhysicsWorld3D::setFixedStep(f32) {}
f32 PhysicsWorld3D::fixedStep() const { return 1.0f / 60.0f; }
u32 PhysicsWorld3D::lastStepCount() const { return 0; }
bool PhysicsWorld3D::stateOf(u32, BodyState&) const { return false; }
bool PhysicsWorld3D::teleport(u32, const Vec3&, const Vec4&) { return false; }
std::vector<PhysicsWorld3D::ContactEvent> PhysicsWorld3D::takeContactEvents() { return {}; }
bool PhysicsWorld3D::isAvailable() const { return false; }
PhysicsWorld3D::RayHit PhysicsWorld3D::raycast(const Vec3&, const Vec3&, f32) const { return {}; }
bool PhysicsWorld3D::addForce(u32, const Vec3&) { return false; }
bool PhysicsWorld3D::addImpulse(u32, const Vec3&) { return false; }
bool PhysicsWorld3D::setLinearVelocity(u32, const Vec3&) { return false; }
bool PhysicsWorld3D::linearVelocity(u32, Vec3&) const { return false; }

#endif

}  // namespace Caffeine::Physics3D
