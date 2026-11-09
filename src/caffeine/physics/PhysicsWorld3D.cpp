#include "physics/PhysicsWorld3D.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

#ifdef CF_HAS_BULLET3
#include <btBulletDynamicsCommon.h>
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

}  // namespace

#ifdef CF_HAS_BULLET3

struct PhysicsWorld3D::Impl {
    struct Record {
        btRigidBody* body = nullptr;
        btCollisionShape* shape = nullptr;
        btDefaultMotionState* motion = nullptr;
        u32 fingerprint = 0;
        BodyType3D type = BodyType3D::Dynamic;
        Vec3 offset{};
    };

    btDefaultCollisionConfiguration* config = nullptr;
    btCollisionDispatcher* dispatcher = nullptr;
    btBroadphaseInterface* broadphase = nullptr;
    btSequentialImpulseConstraintSolver* solver = nullptr;
    btDiscreteDynamicsWorld* world = nullptr;
    std::unordered_map<u32, Record> bodies;

    Impl() {
        config = new btDefaultCollisionConfiguration();
        dispatcher = new btCollisionDispatcher(config);
        broadphase = new btDbvtBroadphase();
        solver = new btSequentialImpulseConstraintSolver();
        world = new btDiscreteDynamicsWorld(dispatcher, broadphase, solver, config);
        world->setGravity(btVector3(0.0f, -9.81f, 0.0f));
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
        record = {};
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

void PhysicsWorld3D::clear() { m_impl->clear(); }

void PhysicsWorld3D::setGravity(const Vec3& gravity) {
    m_impl->world->setGravity(btVector3(gravity.x, gravity.y, gravity.z));
}

void PhysicsWorld3D::upsertBody(const BodyDesc& desc) {
    const u32 fp = fingerprint(desc);
    auto it = m_impl->bodies.find(desc.entityId);
    if (it != m_impl->bodies.end() && it->second.fingerprint == fp) {
        if (desc.bodyType != BodyType3D::Dynamic || desc.resetPose) {
            const btTransform tr = Impl::makeTransform(desc);
            it->second.body->setWorldTransform(tr);
            it->second.motion->setWorldTransform(tr);
            it->second.body->setLinearVelocity(btVector3(0, 0, 0));
            it->second.body->setAngularVelocity(btVector3(0, 0, 0));
            it->second.body->activate(true);
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
    record.body = new btRigidBody(info);
    record.body->setUserIndex(static_cast<int>(desc.entityId));
    if (desc.isTrigger) {
        record.body->setCollisionFlags(record.body->getCollisionFlags() | btCollisionObject::CF_NO_CONTACT_RESPONSE);
    }
    if (desc.bodyType == BodyType3D::Kinematic) {
        record.body->setCollisionFlags(record.body->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
        record.body->setActivationState(DISABLE_DEACTIVATION);
    }
    if (desc.lockRotation) record.body->setAngularFactor(btVector3(0, 0, 0));

    m_impl->world->addRigidBody(record.body);
    m_impl->bodies.emplace(desc.entityId, record);
}

void PhysicsWorld3D::removeBody(u32 entityId) {
    auto it = m_impl->bodies.find(entityId);
    if (it == m_impl->bodies.end()) return;
    m_impl->destroyRecord(it->second);
    m_impl->bodies.erase(it);
}

void PhysicsWorld3D::removeMissing(const std::vector<u32>& liveEntityIds) {
    std::vector<u32> stale;
    for (const auto& [id, _] : m_impl->bodies) {
        if (std::find(liveEntityIds.begin(), liveEntityIds.end(), id) == liveEntityIds.end()) {
            stale.push_back(id);
        }
    }
    for (u32 id : stale) removeBody(id);
}

void PhysicsWorld3D::step(f32 dt) {
    if (dt <= 0.0f) return;
    m_impl->world->stepSimulation(dt, 4, 1.0f / 60.0f);
}

bool PhysicsWorld3D::stateOf(u32 entityId, BodyState& out) const {
    auto it = m_impl->bodies.find(entityId);
    if (it == m_impl->bodies.end() || it->second.type != BodyType3D::Dynamic) return false;
    btTransform tr;
    it->second.motion->getWorldTransform(tr);
    const btVector3 worldOffset = tr.getBasis() * btVector3(it->second.offset.x, it->second.offset.y, it->second.offset.z);
    const btVector3 origin = tr.getOrigin() - worldOffset;
    const btQuaternion rot = tr.getRotation();
    out.position = Vec3(origin.x(), origin.y(), origin.z());
    out.rotation = Vec4(rot.x(), rot.y(), rot.z(), rot.w());
    return true;
}

bool PhysicsWorld3D::isAvailable() const { return true; }

#else

struct PhysicsWorld3D::Impl {};

PhysicsWorld3D::PhysicsWorld3D() : m_impl(new Impl()) {}
PhysicsWorld3D::~PhysicsWorld3D() { delete m_impl; }
void PhysicsWorld3D::clear() {}
void PhysicsWorld3D::setGravity(const Vec3&) {}
void PhysicsWorld3D::upsertBody(const BodyDesc&) {}
void PhysicsWorld3D::removeBody(u32) {}
void PhysicsWorld3D::removeMissing(const std::vector<u32>&) {}
void PhysicsWorld3D::step(f32) {}
bool PhysicsWorld3D::stateOf(u32, BodyState&) const { return false; }
bool PhysicsWorld3D::isAvailable() const { return false; }

#endif

}  // namespace Caffeine::Physics3D
