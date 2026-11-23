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
#include "scene/HierarchySystem.hpp"

#include <cmath>
#include <cstdint>
#include <mutex>
#include <set>
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
    static constexpr f32 kSleepVelThreshold = 0.05f;
    static constexpr f32 kSleepTime         = 0.5f;
    static constexpr f32 kSlop              = 0.01f;
    static constexpr f32 kBaumgartePercent  = 0.4f;
    static constexpr i32 kSubSteps          = 2;
    static constexpr i32 kGridCellSize      = 128;

    explicit PhysicsSystem2D(Events::EventBus* eventBus = nullptr)
        : m_eventBus(eventBus) {}

    /// Fixed step of the simulation, in seconds. Frames of any length are turned into whole steps of this
    /// size (the remainder carries over), so a body falls, slides and sleeps the same at 30 and at 240 fps.
    static constexpr f32 kFixedStep         = 1.0f / 60.0f;
    /// A hitch longer than this many steps is dropped rather than simulated for seconds.
    static constexpr u32 kMaxStepsPerFrame  = 8;

    void onUpdate(ECS::World& world, f32 dt) override {
        if (dt <= 0.0f) return;
        m_accumulator += dt;
        u32 steps = static_cast<u32>((m_accumulator + 1e-5f) / kFixedStep);
        if (steps > kMaxStepsPerFrame) {
            steps = kMaxStepsPerFrame;
            m_accumulator = static_cast<f32>(steps) * kFixedStep;
        }

        // What was asked this frame becomes momentum that waits for a step: a force holds for the frame it was
        // asked in (force x frame time), an impulse is instant.
        {
            std::lock_guard<std::mutex> lock(m_forcesMutex);
            for (const auto& [id, force] : m_pendingForces) {
                Vec2& carried = m_carried[id];
                carried.x += force.x * dt;
                carried.y += force.y * dt;
            }
            for (const auto& [id, impulse] : m_pendingImpulses) {
                Vec2& carried = m_carried[id];
                carried.x += impulse.x;
                carried.y += impulse.y;
            }
            m_pendingForces.clear();
            m_pendingImpulses.clear();
        }

        if (steps == 0) return;
        m_accumulator = std::max(0.0f, m_accumulator - static_cast<f32>(steps) * kFixedStep);
        m_lastManifolds.clear();
        m_framePairs.clear();
        m_triggersNow.clear();
        m_subDt = kFixedStep / static_cast<f32>(kSubSteps);
        for (u32 i = 0; i < steps; ++i) {
            if (i == 0) applyQueued(world, m_carried);
            if (i == 0) m_carried.clear();
            const f32 subDt = kFixedStep / static_cast<f32>(kSubSteps);
            for (i32 sub = 0; sub < kSubSteps; ++sub) {
                // A body that would move farther than half its own size in one slice is advanced in several,
                // with a collision pass after each: nothing thin is jumped over.
                const u32 slices = motionSlices(world, subDt);
                const f32 sliceDt = subDt / static_cast<f32>(slices);
                for (u32 k = 0; k < slices; ++k) {
                    integrateAll(world, sliceDt);
                    buildGrid(world);
                    detectAndResolve(world);
                    updateSleep(world, sliceDt);
                }
            }
        }
        publishTriggerChanges();
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
        q.with<ECS::Transform>();

        world.forEach<Collider2D, ECS::Transform>(q,
            [&](ECS::Entity e, Collider2D& col, ECS::Transform& pos) {
                if (!(col.layerMask & layerMask)) return;

                Vec2 center = centerOf(world, e, pos, col);
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
        q.with<ECS::Transform>();

        world.forEach<Collider2D, ECS::Transform>(q,
            [&](ECS::Entity e, Collider2D& col, ECS::Transform& pos) {
                if (!(col.layerMask & layerMask)) return;
                Vec2 c = centerOf(world, e, pos, col);

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
        q.with<ECS::Transform>();

        world.forEach<Collider2D, ECS::Transform>(q,
            [&](ECS::Entity e, Collider2D& col, ECS::Transform& pos) {
                if (!(col.layerMask & layerMask)) return;
                Vec2 c = centerOf(world, e, pos, col);

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

    void applyImpulse(ECS::World& world, ECS::Entity e, Vec2 impulse) {
        if (auto* rb = world.get<RigidBody2D>(e)) {
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

    void setKinematic(ECS::World& world, ECS::Entity e, bool kinematic) {
        if (auto* rb = world.get<RigidBody2D>(e)) rb->isKinematic = kinematic;
    }

    /// Puts the entity at `position` in world space (a child of a moved parent gets the matching local value).
    void teleport(ECS::World& world, ECS::Entity e, Vec2 position) {
        if (auto* pos = world.get<ECS::Transform>(e)) {
            if (const ECS::Entity parent = parentOf(world, e); parent.isValid()) {
                const Mat4 current = Scene::computeWorldMatrix(world, e);
                const Vec3 target(position.x, position.y, current.transformPoint(Vec3(0.0f, 0.0f, 0.0f)).z);
                const Vec3 local = Scene::computeWorldMatrix(world, parent).inverted().transformPoint(target);
                pos->position.x = local.x;
                pos->position.y = local.y;
            } else {
                pos->position.x = position.x;
                pos->position.y = position.y;
            }
        }
        if (auto* rb = world.get<RigidBody2D>(e)) {
            rb->isSleeping = false;
            rb->sleepTimer = 0.0f;
        }
    }

    const std::vector<CollisionManifold>& lastManifolds() const { return m_lastManifolds; }

private:
    /// Adds the queued momentum (impulses, and forces times their frame time) to the bodies' velocities.
    void applyQueued(ECS::World& world, const std::unordered_map<u32, Vec2>& momentum) {
        if (momentum.empty()) return;
        ECS::ComponentQuery q;
        q.with<RigidBody2D>();

        world.forEach<RigidBody2D>(q,
            [&](ECS::Entity e, RigidBody2D& rb) {
                if (rb.isKinematic) return;
                auto it = momentum.find(e.id());
                if (it == momentum.end()) return;
                // A push wakes a sleeping body instead of being lost on it.
                rb.isSleeping = false;
                rb.sleepTimer = 0.0f;
                const f32 invMass = (rb.mass > 0.0f) ? 1.0f / rb.mass : 0.0f;
                rb.velocityX += it->second.x * invMass;
                rb.velocityY += it->second.y * invMass;
            });
    }

    static constexpr u32 kMaxSlices = 8;

    /// The parent of an entity in the hierarchy, or an invalid entity. Bodies live in world space: under a
    /// parent that is moved, turned or scaled, positions are read through the parent's matrix and every
    /// movement is turned back into a local change.
    static ECS::Entity parentOf(ECS::World& world, ECS::Entity e) {
        const auto* parent = world.get<Scene::Parent>(e);
        if (parent && parent->parent.isValid() && parent->parent.id() != e.id()) return parent->parent;
        return ECS::Entity::INVALID;
    }

    static Vec2 centerOf(ECS::World& world, ECS::Entity e, const ECS::Transform& t, const Collider2D& col) {
        Vec2 p = { t.position.x, t.position.y };
        if (parentOf(world, e).isValid()) {
            const Vec3 w = Scene::computeWorldMatrix(world, e).transformPoint(Vec3(0.0f, 0.0f, 0.0f));
            p = { w.x, w.y };
        }
        return { p.x + col.offset.x, p.y + col.offset.y };
    }

    static void moveWorld(ECS::World& world, ECS::Entity e, ECS::Transform& t, f32 dx, f32 dy) {
        const ECS::Entity parent = parentOf(world, e);
        if (!parent.isValid()) {
            t.position.x += dx;
            t.position.y += dy;
            return;
        }
        const Mat4 inverse = Scene::computeWorldMatrix(world, parent).inverted();
        const Vec3 a = inverse.transformPoint(Vec3(dx, dy, 0.0f));
        const Vec3 b = inverse.transformPoint(Vec3(0.0f, 0.0f, 0.0f));
        t.position.x += a.x - b.x;
        t.position.y += a.y - b.y;
    }

    u32 motionSlices(ECS::World& world, f32 dt) {
        ECS::ComponentQuery q;
        q.with<RigidBody2D>();
        q.with<Collider2D>();
        u32 slices = 1;
        world.forEach<RigidBody2D, Collider2D>(q, [&](ECS::Entity, RigidBody2D& rb, Collider2D& col) {
            if (rb.isSleeping || (col.isStatic && !rb.isKinematic)) return;
            const Vec2 half = aabbHalf(col);
            const f32 smallest = std::max(std::min(half.x, half.y), 0.5f);
            const f32 travel = std::sqrt(rb.velocityX * rb.velocityX + rb.velocityY * rb.velocityY) * dt;
            const u32 needed = static_cast<u32>(std::ceil(travel / (smallest * 0.5f)));
            slices = std::max(slices, std::min(needed, kMaxSlices));
        });
        return slices;
    }

    void integrateAll(ECS::World& world, f32 dt) {
        ECS::ComponentQuery q;
        q.with<RigidBody2D>();
        q.with<ECS::Transform>();

        world.forEach<RigidBody2D, ECS::Transform>(q,
            [&](ECS::Entity e, RigidBody2D& rb, ECS::Transform& pos) {
                // A static collider is scenery even when it carries a body (gravity must not drop a floor).
                if (const auto* col = world.get<Collider2D>(e); col && col->isStatic && !rb.isKinematic) return;
                if (rb.isKinematic) {
                    moveWorld(world, e, pos, rb.velocityX * dt, rb.velocityY * dt);
                    return;
                }

                if (rb.isSleeping) return;

                rb.velocityX += m_gravity.x * dt;
                rb.velocityY += m_gravity.y * dt;

                f32 damping = 1.0f - rb.linearDamping * dt;
                if (damping < 0.0f) damping = 0.0f;
                rb.velocityX *= damping;
                rb.velocityY *= damping;

                moveWorld(world, e, pos, rb.velocityX * dt, rb.velocityY * dt);
            });
    }

    void buildGrid(ECS::World& world) {
        m_grid.clear();
        m_entityData.clear();
        m_entityIndex.clear();

        ECS::ComponentQuery q;
        q.with<Collider2D>();
        q.with<ECS::Transform>();

        world.forEach<Collider2D, ECS::Transform>(q,
            [&](ECS::Entity e, Collider2D& col, ECS::Transform& pos) {
                EntityCell ec;
                ec.id  = e.id();
                ec.pos = centerOf(world, e, pos, col);
                ec.col = &col;
                m_entityIndex[ec.id] = m_entityData.size();
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

    static bool canCollide(const Collider2D& a, const Collider2D& b) {
        const u64 bitA = a.layer < 32 ? (1u << a.layer) : 0u;
        const u64 bitB = b.layer < 32 ? (1u << b.layer) : 0u;
        return (a.layerMask & bitB) && (b.layerMask & bitA);
    }

    void detectAndResolve(ECS::World& world) {
        // Candidate pairs from the grid: sorted and de-duplicated, so the order is the same on every run.
        std::vector<u64> keys;
        for (auto& [key, ids] : m_grid) {
            for (usize i = 0; i < ids.size(); ++i) {
                for (usize j = i + 1; j < ids.size(); ++j) {
                    const u32 a = std::min(ids[i], ids[j]);
                    const u32 b = std::max(ids[i], ids[j]);
                    keys.push_back((static_cast<u64>(a) << 32) | b);
                }
            }
        }
        std::sort(keys.begin(), keys.end());
        keys.erase(std::unique(keys.begin(), keys.end()), keys.end());

        for (const u64 key : keys) {
            CollisionPair pair{static_cast<u32>(key >> 32), static_cast<u32>(key & 0xFFFFFFFFu)};
            auto* dataA = findEntityData(pair.a);
            auto* dataB = findEntityData(pair.b);
            if (!dataA || !dataB) continue;

            Collider2D& colA = *dataA->col;
            Collider2D& colB = *dataB->col;

            if (colA.isStatic && colB.isStatic) continue;
            if (!canCollide(colA, colB)) continue;

            CollisionManifold manifold;
            bool hit = false;

            if (colA.shape == ColliderShape::AABB && colB.shape == ColliderShape::AABB) {
                hit = detectAABBvsAABB(dataA->pos, colA, dataB->pos, colB, manifold);
            } else if (colA.shape == ColliderShape::Circle && colB.shape == ColliderShape::Circle) {
                hit = detectCirclevsCircle(dataA->pos, colA.radius, dataB->pos, colB.radius, manifold);
            } else {
                // The manifold normal points from A to B. The helper returns circle -> box.
                const EntityCell* circEnt = (colA.shape == ColliderShape::Circle) ? dataA : dataB;
                const EntityCell* aabbEnt = (colA.shape == ColliderShape::Circle) ? dataB : dataA;
                hit = detectCirclevsAABB(circEnt->pos, circEnt->col->radius,
                                          aabbEnt->pos, *aabbEnt->col, manifold);
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
                m_triggersNow.insert(colA.isTrigger ? key : ((static_cast<u64>(pair.b) << 32) | pair.a));
                continue;
            }

            m_lastManifolds.push_back(manifold);
            resolveCollision(world, pair.a, pair.b, manifold);
            positionalCorrection(world, pair.a, pair.b, manifold);
            if (m_framePairs.insert(key).second) publishCollision(world, pair.a, pair.b, manifold);
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

    /// Circle against box; the normal points from the circle to the box.
    bool detectCirclevsAABB(Vec2 circlePos, f32 radius,
                             Vec2 aabbPos, const Collider2D& aabb,
                             CollisionManifold& out) {
        Vec2 half    = { aabb.size.x * 0.5f, aabb.size.y * 0.5f };
        f32  closestX = clampf(circlePos.x, aabbPos.x - half.x, aabbPos.x + half.x);
        f32  closestY = clampf(circlePos.y, aabbPos.y - half.y, aabbPos.y + half.y);
        f32  dx = closestX - circlePos.x;
        f32  dy = closestY - circlePos.y;
        f32  dist = std::sqrt(dx * dx + dy * dy);

        if (dist >= radius) return false;

        if (dist < 1e-6f) {
            // The centre is inside the box: leave by the nearest side.
            const f32 left   = circlePos.x - (aabbPos.x - half.x);
            const f32 right  = (aabbPos.x + half.x) - circlePos.x;
            const f32 bottom = circlePos.y - (aabbPos.y - half.y);
            const f32 top    = (aabbPos.y + half.y) - circlePos.y;
            const f32 nearest = std::min(std::min(left, right), std::min(bottom, top));
            if (nearest == left)        out.normal = { 1.0f, 0.0f };   // circle leaves to the left: box is to its right
            else if (nearest == right)  out.normal = { -1.0f, 0.0f };
            else if (nearest == bottom) out.normal = { 0.0f, 1.0f };
            else                        out.normal = { 0.0f, -1.0f };
            out.penetration = radius + nearest;
        } else {
            out.normal      = { dx / dist, dy / dist };
            out.penetration = radius - dist;
        }
        out.contactPoint = { closestX, closestY };
        return true;
    }

    void resolveCollision(ECS::World& world, u32 idA, u32 idB,
                          const CollisionManifold& m) {
        RigidBody2D*     rbA  = getRB(world, idA);
        RigidBody2D*     rbB  = getRB(world, idB);
        Collider2D*      colA = getCol(world, idA);
        Collider2D*      colB = getCol(world, idB);

        // A body counts only if it has a RigidBody2D and a non-static, non-kinematic collider. The other side
        // may be plain scenery (a floor that is only a collider): it simply does not move.
        const f32 invMassA = (rbA && !colA->isStatic && !rbA->isKinematic && rbA->mass > 0.0f)
                             ? 1.0f / rbA->mass : 0.0f;
        const f32 invMassB = (rbB && !colB->isStatic && !rbB->isKinematic && rbB->mass > 0.0f)
                             ? 1.0f / rbB->mass : 0.0f;

        if (invMassA + invMassB < 1e-9f) return;

        const Vec2 vA = rbA ? Vec2{rbA->velocityX, rbA->velocityY} : Vec2{0.0f, 0.0f};
        const Vec2 vB = rbB ? Vec2{rbB->velocityX, rbB->velocityY} : Vec2{0.0f, 0.0f};

        const f32 relVelN = (vB.x - vA.x) * m.normal.x + (vB.y - vA.y) * m.normal.y;

        // Already separating (or resting against each other): nothing to resolve.
        if (relVelN >= 0.0f) return;

        // The bounce of the two surfaces that have a material (a floor with none does not damp it); a speed
        // no bigger than one step of gravity is a body at rest, not a bounce.
        f32 e = 0.0f;
        if (rbA && rbB) e = std::fmin(rbA->restitution, rbB->restitution);
        else if (rbA)   e = rbA->restitution;
        else if (rbB)   e = rbB->restitution;
        const f32 restSpeed = std::sqrt(m_gravity.x * m_gravity.x + m_gravity.y * m_gravity.y) * m_subDt * 2.0f;
        if (-relVelN < restSpeed) e = 0.0f;

        const f32 j = -(1.0f + e) * relVelN / (invMassA + invMassB);
        const Vec2 impulse = { m.normal.x * j, m.normal.y * j };

        if (invMassA > 0.0f) {
            rbA->velocityX -= impulse.x * invMassA;
            rbA->velocityY -= impulse.y * invMassA;
            rbA->isSleeping = false;
            rbA->sleepTimer = 0.0f;
        }
        if (invMassB > 0.0f) {
            rbB->velocityX += impulse.x * invMassB;
            rbB->velocityY += impulse.y * invMassB;
            rbB->isSleeping = false;
            rbB->sleepTimer = 0.0f;
        }

        Vec2 tangent = {
            (vB.x - vA.x) - relVelN * m.normal.x,
            (vB.y - vA.y) - relVelN * m.normal.y
        };
        const f32 tanLen = std::sqrt(tangent.x * tangent.x + tangent.y * tangent.y);
        if (tanLen > 1e-6f) {
            tangent.x /= tanLen;
            tangent.y /= tanLen;
            const f32 frictionA = rbA ? rbA->friction : 0.5f;
            const f32 frictionB = rbB ? rbB->friction : 0.5f;
            const f32 mu        = std::sqrt(frictionA * frictionB);
            const f32 jt        = -((vB.x - vA.x) * tangent.x + (vB.y - vA.y) * tangent.y)
                                  / (invMassA + invMassB);
            const Vec2 frImpulse = (std::fabs(jt) < j * mu)
                                   ? Vec2{tangent.x * jt, tangent.y * jt}
                                   : Vec2{-tangent.x * j * mu, -tangent.y * j * mu};
            if (invMassA > 0.0f) {
                rbA->velocityX -= frImpulse.x * invMassA;
                rbA->velocityY -= frImpulse.y * invMassA;
            }
            if (invMassB > 0.0f) {
                rbB->velocityX += frImpulse.x * invMassB;
                rbB->velocityY += frImpulse.y * invMassB;
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
            moveWorld(world, ECS::Entity(idA, &world), *posA,
                      -m.normal.x * correctionMag * invMassA, -m.normal.y * correctionMag * invMassA);
        }
        if (posB && invMassB > 0.0f) {
            moveWorld(world, ECS::Entity(idB, &world), *posB,
                      m.normal.x * correctionMag * invMassB, m.normal.y * correctionMag * invMassB);
        }
    }

    void updateSleep(ECS::World& world, f32 dt) {
        ECS::ComponentQuery q;
        q.with<RigidBody2D>();

        const bool gravityActive = (m_gravity.x * m_gravity.x + m_gravity.y * m_gravity.y) > 1e-6f;

        world.forEach<RigidBody2D>(q,
            [&](ECS::Entity, RigidBody2D& rb) {
                if (rb.isKinematic) return;

                if (gravityActive) {
                    rb.isSleeping = false;
                    rb.sleepTimer = 0.0f;
                    return;
                }

                f32 speedSq = rb.velocityX * rb.velocityX + rb.velocityY * rb.velocityY;
                if (speedSq < kSleepVelThreshold * kSleepVelThreshold) {
                    rb.sleepTimer += dt;
                    if (rb.sleepTimer >= kSleepTime) {
                        rb.isSleeping = true;
                        rb.velocityX = 0.0f;
                        rb.velocityY = 0.0f;
                    }
                } else {
                    rb.sleepTimer = 0.0f;
                    rb.isSleeping = false;
                }
            });
    }

    /// A one-way platform stops a body that is above it and moving down (or still); from below, or from the
    /// side, it lets the body through.
    bool shouldResolveOneWay(ECS::World& world, u32 idA, u32 idB,
                              const CollisionManifold& manifold,
                              const Collider2D& colA, const Collider2D& colB) {
        const bool platformIsA = colA.isOneWay;
        const u32 moverId = platformIsA ? idB : idA;
        auto* rb = getRB(world, moverId);
        if (!rb) return false;

        // The normal points from A to B; this one points from the platform to the mover.
        const f32 upFromPlatform = platformIsA ? manifold.normal.y : -manifold.normal.y;
        return upFromPlatform > 0.5f && rb->velocityY <= 0.0f;
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

    /// Enter and exit events: the overlaps of this frame against the last one.
    void publishTriggerChanges() {
        if (m_eventBus) {
            for (const u64 key : m_triggersNow) {
                if (m_triggersPrev.count(key)) continue;
                Events::OnTrigger2D ev;
                ev.triggerEntity = static_cast<u32>(key >> 32);
                ev.otherEntity   = static_cast<u32>(key & 0xFFFFFFFFu);
                ev.entered       = true;
                m_eventBus->publishDeferred(ev);
            }
            for (const u64 key : m_triggersPrev) {
                if (m_triggersNow.count(key)) continue;
                Events::OnTrigger2D ev;
                ev.triggerEntity = static_cast<u32>(key >> 32);
                ev.otherEntity   = static_cast<u32>(key & 0xFFFFFFFFu);
                ev.entered       = false;
                m_eventBus->publishDeferred(ev);
            }
        }
        m_triggersPrev = m_triggersNow;
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

    ECS::Transform* getPos(ECS::World& world, u32 id) {
        return world.get<ECS::Transform>(ECS::Entity(id, &world));
    }

    RigidBody2D* getRB(ECS::World& world, u32 id) {
        return world.get<RigidBody2D>(ECS::Entity(id, &world));
    }

    Collider2D* getCol(ECS::World& world, u32 id) {
        return world.get<Collider2D>(ECS::Entity(id, &world));
    }

    struct EntityCell {
        u32        id;
        Vec2       pos;
        Collider2D* col;
    };

    EntityCell* findEntityData(u32 id) {
        auto it = m_entityIndex.find(id);
        return it == m_entityIndex.end() ? nullptr : &m_entityData[it->second];
    }

    Vec2              m_gravity      = { 0.0f, -9.81f };
    Events::EventBus* m_eventBus     = nullptr;

    std::mutex                              m_forcesMutex;
    std::unordered_map<u32, Vec2>           m_pendingForces;
    std::unordered_map<u32, Vec2>           m_pendingImpulses;
    std::unordered_map<u32, Vec2>           m_carried;
    f32                                     m_accumulator = 0.0f;

    std::unordered_map<u64, std::vector<u32>> m_grid;
    std::vector<EntityCell>                   m_entityData;
    std::unordered_map<u32, usize>            m_entityIndex;
    std::set<u64>                             m_framePairs;
    std::set<u64>                             m_triggersNow;
    std::set<u64>                             m_triggersPrev;
    f32                                       m_subDt = kFixedStep / static_cast<f32>(kSubSteps);
    std::vector<CollisionManifold>            m_lastManifolds;
};

}
