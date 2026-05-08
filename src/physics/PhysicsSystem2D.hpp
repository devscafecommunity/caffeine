#pragma once

#include "core/Types.hpp"
#include "math/Vec2.hpp"
#include "ecs/World.hpp"
#include "ecs/Entity.hpp"
#include "ecs/ISystem.hpp"
#include "ecs/Components.hpp"
#include "ecs/ComponentQuery.hpp"
#include "events/EventBus.hpp"
#include "events/Events.hpp"
#include "physics/PhysicsComponents2D.hpp"

#include <cmath>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <utility>
#include <algorithm>
#include <limits>

namespace Caffeine::Physics2D {

using namespace Caffeine;

struct Rect2D {
    Vec2 position;
    Vec2 size;
};

struct RaycastHit {
    ECS::Entity entity;
    Vec2         point;
    Vec2         normal;
    f32          distance = 0.0f;
    bool         hit      = false;
};

struct CollisionManifold {
    Vec2 contactPoint;
    Vec2 normal;
    f32  penetration = 0.0f;
};

struct CollisionPair {
    u32 a;
    u32 b;
};

class PhysicsSystem2D : public ECS::ISystem {
public:
    static constexpr f32 kSleepVelThreshold = 2.0f;
    static constexpr f32 kSleepTime         = 0.5f;
    static constexpr f32 kSlop              = 0.01f;
    static constexpr f32 kBaumgartePercent  = 0.4f;
    static constexpr i32 kSubSteps          = 2;
    static constexpr i32 kGridCellSize      = 128;

    explicit PhysicsSystem2D(Events::EventBus* eventBus = nullptr)
        : m_eventBus(eventBus) {}

    void onUpdate(ECS::World& world, f32 dt) override {
        f32 subDt = dt / static_cast<f32>(kSubSteps);

        std::unordered_map<u32, Vec2> forces;
        std::unordered_map<u32, Vec2> impulses;
        {
            std::lock_guard<std::mutex> lock(m_forcesMutex);
            forces   = std::move(m_pendingForces);
            impulses = std::move(m_pendingImpulses);
            m_pendingForces.clear();
            m_pendingImpulses.clear();
        }

        m_lastManifolds.clear();

        for (i32 step = 0; step < kSubSteps; ++step) {
            if (step == 0) {
                applyQueued(world, forces, impulses, subDt);
            }
            integrateAll(world, subDt);
            buildGrid(world);
            detectAndResolve(world);
            updateSleep(world, subDt);
        }
    }

    void setGravity(Vec2 gravity)  { m_gravity = gravity; }
    Vec2 gravity()           const { return m_gravity; }

    RaycastHit raycast(ECS::World& world, Vec2 origin, Vec2 dir, f32 maxDist,
                       u32 layerMask = 0xFFFFFFFF) {
        Vec2 d = normalize(dir);
        RaycastHit best;
        best.distance = maxDist;

        ECS::ComponentQuery q;
        q.with<Collider2D>();
        q.with<ECS::Position2D>();

        world.forEach<Collider2D, ECS::Position2D>(q,
            [&](ECS::Entity e, Collider2D& col, ECS::Position2D& pos) {
                if (!(col.layerMask & layerMask)) return;

                Vec2 center = { pos.x + col.offset.x, pos.y + col.offset.y };
                f32 t = -1.0f;
                Vec2 n;

                if (col.shape == ColliderShape::AABB) {
                    t = rayVsAABB(origin, d, center, col.size, n);
                } else {
                    t = rayVsCircle(origin, d, center, col.radius, n);
                }

                if (t >= 0.0f && t < best.distance) {
                    best.hit      = true;
                    best.entity   = e;
                    best.distance = t;
                    best.normal   = n;
                    best.point    = { origin.x + d.x * t, origin.y + d.y * t };
                }
            });

        return best;
    }

    std::vector<ECS::Entity> overlapCircle(ECS::World& world, Vec2 center, f32 radius,
                                            u32 layerMask = 0xFFFFFFFF) {
        std::vector<ECS::Entity> result;

        ECS::ComponentQuery q;
        q.with<Collider2D>();
        q.with<ECS::Position2D>();

        world.forEach<Collider2D, ECS::Position2D>(q,
            [&](ECS::Entity e, Collider2D& col, ECS::Position2D& pos) {
                if (!(col.layerMask & layerMask)) return;
                Vec2 c = { pos.x + col.offset.x, pos.y + col.offset.y };

                if (col.shape == ColliderShape::Circle) {
                    f32 dist = length({ center.x - c.x, center.y - c.y });
                    if (dist < radius + col.radius) result.push_back(e);
                } else {
                    if (circleVsAABB(center, radius, c, col.size))
                        result.push_back(e);
                }
            });

        return result;
    }

    std::vector<ECS::Entity> overlapAABB(ECS::World& world, Rect2D rect,
                                          u32 layerMask = 0xFFFFFFFF) {
        std::vector<ECS::Entity> result;

        ECS::ComponentQuery q;
        q.with<Collider2D>();
        q.with<ECS::Position2D>();

        world.forEach<Collider2D, ECS::Position2D>(q,
            [&](ECS::Entity e, Collider2D& col, ECS::Position2D& pos) {
                if (!(col.layerMask & layerMask)) return;
                Vec2 c = { pos.x + col.offset.x, pos.y + col.offset.y };

                if (col.shape == ColliderShape::Circle) {
                    if (circleVsAABB(c, col.radius, rect.position, rect.size))
                        result.push_back(e);
                } else {
                    if (aabbOverlap(rect.position, rect.size, c, col.size))
                        result.push_back(e);
                }
            });

        return result;
    }

    void applyForce(ECS::Entity e, Vec2 force) {
        std::lock_guard<std::mutex> lock(m_forcesMutex);
        auto it = m_pendingForces.find(e.id());
        if (it != m_pendingForces.end()) {
            it->second.x += force.x;
            it->second.y += force.y;
        } else {
            m_pendingForces[e.id()] = force;
        }
    }

    void applyImpulse(ECS::Entity e, Vec2 impulse) {
        if (auto* rb = e.get<RigidBody2D>()) {
            rb->isSleeping = false;
            rb->sleepTimer = 0.0f;
        }
        std::lock_guard<std::mutex> lock(m_forcesMutex);
        auto it = m_pendingImpulses.find(e.id());
        if (it != m_pendingImpulses.end()) {
            it->second.x += impulse.x;
            it->second.y += impulse.y;
        } else {
            m_pendingImpulses[e.id()] = impulse;
        }
    }

    void setKinematic(ECS::Entity e, bool kinematic) {
        if (auto* rb = e.get<RigidBody2D>()) rb->isKinematic = kinematic;
    }

    void teleport(ECS::Entity e, Vec2 position) {
        if (auto* pos = e.get<ECS::Position2D>()) {
            pos->x = position.x;
            pos->y = position.y;
        }
        if (auto* rb = e.get<RigidBody2D>()) {
            rb->isSleeping = false;
            rb->sleepTimer = 0.0f;
        }
    }

    const std::vector<CollisionManifold>& lastManifolds() const { return m_lastManifolds; }

private:
    void applyQueued(ECS::World& world,
                     const std::unordered_map<u32, Vec2>& forces,
                     const std::unordered_map<u32, Vec2>& impulses,
                     f32 dt) {
        ECS::ComponentQuery q;
        q.with<RigidBody2D>();
        q.with<ECS::Velocity2D>();
        q.with<Collider2D>();

        world.forEach<RigidBody2D, ECS::Velocity2D, Collider2D>(q,
            [&](ECS::Entity e, RigidBody2D& rb, ECS::Velocity2D& vel, Collider2D& col) {
                if (col.isStatic || rb.isKinematic || rb.isSleeping) return;

                f32 invMass = (rb.mass > 0.0f) ? 1.0f / rb.mass : 0.0f;

                auto fit = forces.find(e.id());
                if (fit != forces.end()) {
                    vel.x += fit->second.x * invMass * dt;
                    vel.y += fit->second.y * invMass * dt;
                }

                auto iit = impulses.find(e.id());
                if (iit != impulses.end()) {
                    vel.x += iit->second.x * invMass;
                    vel.y += iit->second.y * invMass;
                }
            });
    }

    void integrateAll(ECS::World& world, f32 dt) {
        ECS::ComponentQuery q;
        q.with<RigidBody2D>();
        q.with<ECS::Position2D>();
        q.with<ECS::Velocity2D>();
        q.with<Collider2D>();

        world.forEach<RigidBody2D, ECS::Position2D, ECS::Velocity2D, Collider2D>(q,
            [&](ECS::Entity, RigidBody2D& rb, ECS::Position2D& pos,
                ECS::Velocity2D& vel, Collider2D& col) {
                if (col.isStatic) return;

                if (rb.isKinematic) {
                    pos.x += vel.x * dt;
                    pos.y += vel.y * dt;
                    return;
                }

                if (rb.isSleeping) return;

                vel.x += m_gravity.x * dt;
                vel.y += m_gravity.y * dt;

                f32 damping = 1.0f - rb.linearDamping * dt;
                if (damping < 0.0f) damping = 0.0f;
                vel.x *= damping;
                vel.y *= damping;

                pos.x += vel.x * dt;
                pos.y += vel.y * dt;
            });
    }

    void buildGrid(ECS::World& world) {
        m_grid.clear();
        m_entityData.clear();

        ECS::ComponentQuery q;
        q.with<Collider2D>();
        q.with<ECS::Position2D>();

        world.forEach<Collider2D, ECS::Position2D>(q,
            [&](ECS::Entity e, Collider2D& col, ECS::Position2D& pos) {
                EntityCell ec;
                ec.id  = e.id();
                ec.pos = { pos.x + col.offset.x, pos.y + col.offset.y };
                ec.col = &col;
                m_entityData.push_back(ec);

                Vec2 half = aabbHalf(col);
                i32 x0 = toCell(ec.pos.x - half.x);
                i32 y0 = toCell(ec.pos.y - half.y);
                i32 x1 = toCell(ec.pos.x + half.x);
                i32 y1 = toCell(ec.pos.y + half.y);

                for (i32 cx = x0; cx <= x1; ++cx) {
                    for (i32 cy = y0; cy <= y1; ++cy) {
                        m_grid[cellKey(cx, cy)].push_back(e.id());
                    }
                }
            });
    }

    void detectAndResolve(ECS::World& world) {
        std::vector<CollisionPair> pairs;

        for (auto& [key, ids] : m_grid) {
            for (usize i = 0; i < ids.size(); ++i) {
                for (usize j = i + 1; j < ids.size(); ++j) {
                    u32 a = ids[i], b = ids[j];
                    if (a > b) std::swap(a, b);
                    CollisionPair p{a, b};
                    bool dup = false;
                    for (auto& existing : pairs) {
                        if (existing.a == p.a && existing.b == p.b) { dup = true; break; }
                    }
                    if (!dup) pairs.push_back(p);
                }
            }
        }

        for (auto& pair : pairs) {
            auto* dataA = findEntityData(pair.a);
            auto* dataB = findEntityData(pair.b);
            if (!dataA || !dataB) continue;

            Collider2D& colA = *dataA->col;
            Collider2D& colB = *dataB->col;

            if (colA.isStatic && colB.isStatic) continue;
            if (!(colA.layerMask & (1u << colB.layer))) continue;
            if (!(colB.layerMask & (1u << colA.layer))) continue;

            CollisionManifold manifold;
            bool hit = false;

            if (colA.shape == ColliderShape::AABB && colB.shape == ColliderShape::AABB) {
                hit = detectAABBvsAABB(dataA->pos, colA, dataB->pos, colB, manifold);
            } else if (colA.shape == ColliderShape::Circle && colB.shape == ColliderShape::Circle) {
                hit = detectCirclevsCircle(dataA->pos, colA.radius, dataB->pos, colB.radius, manifold);
            } else {
                const EntityCell* circEnt = (colA.shape == ColliderShape::Circle) ? dataA : dataB;
                const EntityCell* aabbEnt = (colA.shape == ColliderShape::Circle) ? dataB : dataA;
                Collider2D& aabbCol       = *aabbEnt->col;
                hit = detectCirclevsAABB(circEnt->pos, circEnt->col->radius,
                                          aabbEnt->pos, aabbCol, manifold);
                if (hit && colA.shape == ColliderShape::AABB) {
                    manifold.normal.x = -manifold.normal.x;
                    manifold.normal.y = -manifold.normal.y;
                }
            }

            if (!hit) continue;

            if (colA.isOneWay || colB.isOneWay) {
                if (!shouldResolveOneWay(world, pair.a, pair.b, manifold, colA, colB)) continue;
            }

            if (colA.isTrigger || colB.isTrigger) {
                publishTrigger(world, pair.a, pair.b);
                continue;
            }

            m_lastManifolds.push_back(manifold);
            resolveCollision(world, pair.a, pair.b, manifold);
            positionalCorrection(world, pair.a, pair.b, manifold);
            publishCollision(world, pair.a, pair.b, manifold);
        }
    }

    bool detectAABBvsAABB(Vec2 posA, const Collider2D& colA,
                           Vec2 posB, const Collider2D& colB,
                           CollisionManifold& out) {
        Vec2 halfA = { colA.size.x * 0.5f, colA.size.y * 0.5f };
        Vec2 halfB = { colB.size.x * 0.5f, colB.size.y * 0.5f };

        f32 dx = posB.x - posA.x;
        f32 dy = posB.y - posA.y;
        f32 overlapX = (halfA.x + halfB.x) - std::fabs(dx);
        f32 overlapY = (halfA.y + halfB.y) - std::fabs(dy);

        if (overlapX <= 0.0f || overlapY <= 0.0f) return false;

        if (overlapX < overlapY) {
            out.normal      = { (dx < 0.0f) ? -1.0f : 1.0f, 0.0f };
            out.penetration = overlapX;
        } else {
            out.normal      = { 0.0f, (dy < 0.0f) ? -1.0f : 1.0f };
            out.penetration = overlapY;
        }
        out.contactPoint = { posA.x + out.normal.x * halfA.x,
                              posA.y + out.normal.y * halfA.y };
        return true;
    }

    bool detectCirclevsCircle(Vec2 posA, f32 rA, Vec2 posB, f32 rB,
                               CollisionManifold& out) {
        f32 dx   = posB.x - posA.x;
        f32 dy   = posB.y - posA.y;
        f32 dist = std::sqrt(dx * dx + dy * dy);
        f32 sumR = rA + rB;

        if (dist >= sumR) return false;

        if (dist < 1e-6f) {
            out.normal = { 1.0f, 0.0f };
        } else {
            out.normal = { dx / dist, dy / dist };
        }
        out.penetration  = sumR - dist;
        out.contactPoint = { posA.x + out.normal.x * rA,
                              posA.y + out.normal.y * rA };
        return true;
    }

    bool detectCirclevsAABB(Vec2 circlePos, f32 radius,
                             Vec2 aabbPos, const Collider2D& aabb,
                             CollisionManifold& out) {
        Vec2 half    = { aabb.size.x * 0.5f, aabb.size.y * 0.5f };
        f32  closestX = clampf(circlePos.x, aabbPos.x - half.x, aabbPos.x + half.x);
        f32  closestY = clampf(circlePos.y, aabbPos.y - half.y, aabbPos.y + half.y);
        f32  dx = circlePos.x - closestX;
        f32  dy = circlePos.y - closestY;
        f32  dist = std::sqrt(dx * dx + dy * dy);

        if (dist >= radius) return false;

        if (dist < 1e-6f) {
            out.normal = { 0.0f, 1.0f };
        } else {
            out.normal = { dx / dist, dy / dist };
        }
        out.penetration  = radius - dist;
        out.contactPoint = { closestX, closestY };
        return true;
    }

    void resolveCollision(ECS::World& world, u32 idA, u32 idB,
                          const CollisionManifold& m) {
        ECS::Velocity2D* velA = getVel(world, idA);
        ECS::Velocity2D* velB = getVel(world, idB);
        RigidBody2D*     rbA  = getRB(world, idA);
        RigidBody2D*     rbB  = getRB(world, idB);
        Collider2D*      colA = getCol(world, idA);
        Collider2D*      colB = getCol(world, idB);

        f32 invMassA = (rbA && !colA->isStatic && !rbA->isKinematic && rbA->mass > 0.0f)
                       ? 1.0f / rbA->mass : 0.0f;
        f32 invMassB = (rbB && !colB->isStatic && !rbB->isKinematic && rbB->mass > 0.0f)
                       ? 1.0f / rbB->mass : 0.0f;

        if (invMassA + invMassB < 1e-9f) return;

        Vec2 vA = velA ? Vec2{velA->x, velA->y} : Vec2{0.0f, 0.0f};
        Vec2 vB = velB ? Vec2{velB->x, velB->y} : Vec2{0.0f, 0.0f};

        f32 relVelN = (vB.x - vA.x) * m.normal.x + (vB.y - vA.y) * m.normal.y;

        if (relVelN > 0.0f) return;

        f32 e = 0.0f;
        if (rbA) e = rbA->restitution;
        if (rbB) e = std::fmin(e, rbB->restitution);

        f32 j = -(1.0f + e) * relVelN / (invMassA + invMassB);

        Vec2 impulse = { m.normal.x * j, m.normal.y * j };

        if (velA && rbA && !colA->isStatic && !rbA->isKinematic) {
            velA->x -= impulse.x * invMassA;
            velA->y -= impulse.y * invMassA;
            rbA->isSleeping = false;
            rbA->sleepTimer = 0.0f;
        }
        if (velB && rbB && !colB->isStatic && !rbB->isKinematic) {
            velB->x += impulse.x * invMassB;
            velB->y += impulse.y * invMassB;
            rbB->isSleeping = false;
            rbB->sleepTimer = 0.0f;
        }

        Vec2 tangent = {
            (vB.x - vA.x) - relVelN * m.normal.x,
            (vB.y - vA.y) - relVelN * m.normal.y
        };
        f32 tanLen = std::sqrt(tangent.x * tangent.x + tangent.y * tangent.y);
        if (tanLen > 1e-6f) {
            tangent.x /= tanLen;
            tangent.y /= tanLen;
            f32 frictionA  = rbA ? rbA->friction : 0.5f;
            f32 frictionB  = rbB ? rbB->friction : 0.5f;
            f32 mu         = std::sqrt(frictionA * frictionB);
            f32 jt         = -((vB.x - vA.x) * tangent.x + (vB.y - vA.y) * tangent.y)
                              / (invMassA + invMassB);
            Vec2 frImpulse = (std::fabs(jt) < j * mu)
                              ? Vec2{tangent.x * jt, tangent.y * jt}
                              : Vec2{-tangent.x * j * mu, -tangent.y * j * mu};

            if (velA && rbA && !colA->isStatic && !rbA->isKinematic) {
                velA->x -= frImpulse.x * invMassA;
                velA->y -= frImpulse.y * invMassA;
            }
            if (velB && rbB && !colB->isStatic && !rbB->isKinematic) {
                velB->x += frImpulse.x * invMassB;
                velB->y += frImpulse.y * invMassB;
            }
        }
    }

    void positionalCorrection(ECS::World& world, u32 idA, u32 idB,
                               const CollisionManifold& m) {
        if (m.penetration <= kSlop) return;

        Collider2D*   colA = getCol(world, idA);
        Collider2D*   colB = getCol(world, idB);
        RigidBody2D*  rbA  = getRB(world, idA);
        RigidBody2D*  rbB  = getRB(world, idB);

        f32 invMassA = (rbA && !colA->isStatic && !rbA->isKinematic && rbA->mass > 0.0f)
                       ? 1.0f / rbA->mass : 0.0f;
        f32 invMassB = (rbB && !colB->isStatic && !rbB->isKinematic && rbB->mass > 0.0f)
                       ? 1.0f / rbB->mass : 0.0f;

        f32 totalInvMass = invMassA + invMassB;
        if (totalInvMass < 1e-9f) return;

        f32 correctionMag = (m.penetration - kSlop) * kBaumgartePercent / totalInvMass;

        auto* posA = getPos(world, idA);
        auto* posB = getPos(world, idB);

        if (posA && invMassA > 0.0f) {
            posA->x -= m.normal.x * correctionMag * invMassA;
            posA->y -= m.normal.y * correctionMag * invMassA;
        }
        if (posB && invMassB > 0.0f) {
            posB->x += m.normal.x * correctionMag * invMassB;
            posB->y += m.normal.y * correctionMag * invMassB;
        }
    }

    void updateSleep(ECS::World& world, f32 dt) {
        ECS::ComponentQuery q;
        q.with<RigidBody2D>();
        q.with<ECS::Velocity2D>();
        q.with<Collider2D>();

        world.forEach<RigidBody2D, ECS::Velocity2D, Collider2D>(q,
            [&](ECS::Entity, RigidBody2D& rb, ECS::Velocity2D& vel, Collider2D& col) {
                if (col.isStatic || rb.isKinematic) return;

                f32 speedSq = vel.x * vel.x + vel.y * vel.y;
                if (speedSq < kSleepVelThreshold * kSleepVelThreshold) {
                    rb.sleepTimer += dt;
                    if (rb.sleepTimer >= kSleepTime) {
                        rb.isSleeping = true;
                        vel.x = 0.0f;
                        vel.y = 0.0f;
                    }
                } else {
                    rb.sleepTimer = 0.0f;
                    rb.isSleeping = false;
                }
            });
    }

    bool shouldResolveOneWay(ECS::World& world, u32 idA, u32 idB,
                              const CollisionManifold& manifold,
                              const Collider2D& colA, const Collider2D& colB) {
        Collider2D* oneWay = colA.isOneWay ? const_cast<Collider2D*>(&colA)
                                            : const_cast<Collider2D*>(&colB);
        u32 moverId = colA.isOneWay ? idB : idA;

        (void)oneWay;

        auto* vel = getVel(world, moverId);
        if (!vel) return false;

        float dotWithNormal = vel->y * manifold.normal.y;
        return dotWithNormal <= 0.0f;
    }

    void publishCollision(ECS::World& world, u32 idA, u32 idB,
                          const CollisionManifold& m) {
        (void)world;
        if (!m_eventBus) return;
        Events::OnCollision2D ev;
        ev.entityA     = idA;
        ev.entityB     = idB;
        ev.contactPoint = m.contactPoint;
        ev.normal       = m.normal;
        ev.impulse      = m.penetration;
        m_eventBus->publishDeferred(ev);
    }

    void publishTrigger(ECS::World& world, u32 idA, u32 idB) {
        (void)world;
        if (!m_eventBus) return;
        Events::OnTrigger2D ev;
        ev.triggerEntity = idA;
        ev.otherEntity   = idB;
        ev.entered       = true;
        m_eventBus->publishDeferred(ev);
    }

    static i32  toCell(f32 v)         { return static_cast<i32>(std::floor(v / kGridCellSize)); }
    static u64  cellKey(i32 cx, i32 cy) {
        return (static_cast<u64>(static_cast<u32>(cx)) << 32) | static_cast<u32>(cy);
    }

    static Vec2 aabbHalf(const Collider2D& col) {
        if (col.shape == ColliderShape::Circle)
            return { col.radius, col.radius };
        return { col.size.x * 0.5f, col.size.y * 0.5f };
    }

    static f32 length(Vec2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }

    static Vec2 normalize(Vec2 v) {
        f32 len = length(v);
        if (len < 1e-9f) return { 1.0f, 0.0f };
        return { v.x / len, v.y / len };
    }

    static f32 clampf(f32 v, f32 lo, f32 hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    static bool aabbOverlap(Vec2 posA, Vec2 sizeA, Vec2 posB, Vec2 sizeB) {
        Vec2 halfA = { sizeA.x * 0.5f, sizeA.y * 0.5f };
        Vec2 halfB = { sizeB.x * 0.5f, sizeB.y * 0.5f };
        return std::fabs(posA.x - posB.x) < halfA.x + halfB.x &&
               std::fabs(posA.y - posB.y) < halfA.y + halfB.y;
    }

    static bool circleVsAABB(Vec2 circlePos, f32 radius, Vec2 aabbPos, Vec2 aabbSize) {
        Vec2 half     = { aabbSize.x * 0.5f, aabbSize.y * 0.5f };
        f32  closestX = clampf(circlePos.x, aabbPos.x - half.x, aabbPos.x + half.x);
        f32  closestY = clampf(circlePos.y, aabbPos.y - half.y, aabbPos.y + half.y);
        f32  dx = circlePos.x - closestX;
        f32  dy = circlePos.y - closestY;
        return dx * dx + dy * dy < radius * radius;
    }

    static f32 rayVsAABB(Vec2 origin, Vec2 dir, Vec2 center, Vec2 size, Vec2& normal) {
        Vec2 half = { size.x * 0.5f, size.y * 0.5f };
        f32 tminX = (center.x - half.x - origin.x);
        f32 tmaxX = (center.x + half.x - origin.x);
        f32 tminY = (center.y - half.y - origin.y);
        f32 tmaxY = (center.y + half.y - origin.y);

        if (std::fabs(dir.x) > 1e-9f) { tminX /= dir.x; tmaxX /= dir.x; }
        else { if (origin.x < center.x - half.x || origin.x > center.x + half.x) return -1.0f; tminX = -1e30f; tmaxX = 1e30f; }
        if (std::fabs(dir.y) > 1e-9f) { tminY /= dir.y; tmaxY /= dir.y; }
        else { if (origin.y < center.y - half.y || origin.y > center.y + half.y) return -1.0f; tminY = -1e30f; tmaxY = 1e30f; }

        if (tminX > tmaxX) std::swap(tminX, tmaxX);
        if (tminY > tmaxY) std::swap(tminY, tmaxY);

        f32 tmin = std::fmax(tminX, tminY);
        f32 tmax = std::fmin(tmaxX, tmaxY);

        if (tmax < 0.0f || tmin > tmax) return -1.0f;
        f32 t = (tmin >= 0.0f) ? tmin : tmax;

        if (tminX > tminY)
            normal = { (dir.x < 0.0f) ? 1.0f : -1.0f, 0.0f };
        else
            normal = { 0.0f, (dir.y < 0.0f) ? 1.0f : -1.0f };

        return t;
    }

    static f32 rayVsCircle(Vec2 origin, Vec2 dir, Vec2 center, f32 radius, Vec2& normal) {
        Vec2 oc = { origin.x - center.x, origin.y - center.y };
        f32 b   = oc.x * dir.x + oc.y * dir.y;
        f32 c   = oc.x * oc.x + oc.y * oc.y - radius * radius;
        f32 disc = b * b - c;
        if (disc < 0.0f) return -1.0f;
        f32 t = -b - std::sqrt(disc);
        if (t < 0.0f) t = -b + std::sqrt(disc);
        if (t < 0.0f) return -1.0f;
        Vec2 hit = { origin.x + dir.x * t, origin.y + dir.y * t };
        f32 nd = length({ hit.x - center.x, hit.y - center.y });
        normal = nd > 1e-6f ? Vec2{ (hit.x - center.x) / nd, (hit.y - center.y) / nd }
                             : Vec2{ 0.0f, 1.0f };
        return t;
    }

    ECS::Velocity2D* getVel(ECS::World& world, u32 id) {
        ECS::Entity e(id, &world);
        return e.get<ECS::Velocity2D>();
    }

    ECS::Position2D* getPos(ECS::World& world, u32 id) {
        ECS::Entity e(id, &world);
        return e.get<ECS::Position2D>();
    }

    RigidBody2D* getRB(ECS::World& world, u32 id) {
        ECS::Entity e(id, &world);
        return e.get<RigidBody2D>();
    }

    Collider2D* getCol(ECS::World& world, u32 id) {
        ECS::Entity e(id, &world);
        return e.get<Collider2D>();
    }

    struct EntityCell {
        u32        id;
        Vec2       pos;
        Collider2D* col;
    };

    EntityCell* findEntityData(u32 id) {
        for (auto& ec : m_entityData) {
            if (ec.id == id) return &ec;
        }
        return nullptr;
    }

    Vec2              m_gravity      = { 0.0f, -9.81f * 60.0f };
    Events::EventBus* m_eventBus     = nullptr;

    std::mutex                              m_forcesMutex;
    std::unordered_map<u32, Vec2>           m_pendingForces;
    std::unordered_map<u32, Vec2>           m_pendingImpulses;

    std::unordered_map<u64, std::vector<u32>> m_grid;
    std::vector<EntityCell>                   m_entityData;
    std::vector<CollisionManifold>            m_lastManifolds;
};

}
