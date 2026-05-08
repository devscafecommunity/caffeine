#include "catch.hpp"
#include "../src/Caffeine.hpp"
#include "../src/physics/PhysicsComponents2D.hpp"
#include "../src/physics/PhysicsSystem2D.hpp"

#include <cmath>

using namespace Caffeine;
using namespace Caffeine::ECS;
using namespace Caffeine::Physics2D;

static constexpr f32 kEps = 0.01f;

static bool approxEq(f32 a, f32 b) { return std::fabs(a - b) < kEps; }


TEST_CASE("PhysicsMaterial - ice preset", "[physics]") {
    auto m = PhysicsMaterial::ice();
    REQUIRE(m.friction    < 0.1f);
    REQUIRE(m.restitution < 0.2f);
}

TEST_CASE("PhysicsMaterial - rubber preset", "[physics]") {
    auto m = PhysicsMaterial::rubber();
    REQUIRE(m.restitution > 0.8f);
}


TEST_CASE("RigidBody2D - default mass is 1", "[physics]") {
    RigidBody2D rb{};
    REQUIRE(approxEq(rb.mass, 1.0f));
    REQUIRE_FALSE(rb.isKinematic);
    REQUIRE_FALSE(rb.isSleeping);
}

TEST_CASE("Collider2D - default shape is AABB", "[physics]") {
    Collider2D col{};
    REQUIRE(col.shape == ColliderShape::AABB);
    REQUIRE_FALSE(col.isStatic);
    REQUIRE_FALSE(col.isTrigger);
    REQUIRE_FALSE(col.isOneWay);
    REQUIRE(col.layerMask == 0xFFFFFFFF);
}


TEST_CASE("PhysicsSystem2D - default gravity", "[physics]") {
    PhysicsSystem2D sys;
    Vec2 g = sys.gravity();
    REQUIRE(approxEq(g.x, 0.0f));
    REQUIRE(g.y < 0.0f);
}

TEST_CASE("PhysicsSystem2D - setGravity", "[physics]") {
    PhysicsSystem2D sys;
    sys.setGravity({ 0.0f, -500.0f });
    REQUIRE(approxEq(sys.gravity().y, -500.0f));
}


TEST_CASE("PhysicsSystem2D - dynamic body falls under gravity", "[physics]") {
    World world;
    PhysicsSystem2D sys;
    sys.setGravity({ 0.0f, -100.0f });

    Entity e = world.create();
    world.add<Position2D>(e, Position2D{ 0.0f, 100.0f });
    world.add<Velocity2D>(e, Velocity2D{ 0.0f, 0.0f });
    world.add<RigidBody2D>(e, RigidBody2D{ .mass = 1.0f });
    world.add<Collider2D>(e);

    sys.onUpdate(world, 1.0f / 60.0f);

    auto* pos = world.get<Position2D>(e);
    REQUIRE(pos != nullptr);
    REQUIRE(pos->y < 100.0f);
}

TEST_CASE("PhysicsSystem2D - static body does not move under gravity", "[physics]") {
    World world;
    PhysicsSystem2D sys;
    sys.setGravity({ 0.0f, -100.0f });

    Entity e = world.create();
    world.add<Position2D>(e, Position2D{ 0.0f, 0.0f });
    world.add<Velocity2D>(e, Velocity2D{ 0.0f, 0.0f });
    world.add<RigidBody2D>(e);
    Collider2D col{};
    col.isStatic = true;
    world.add<Collider2D>(e, col);

    sys.onUpdate(world, 1.0f / 60.0f);

    auto* pos = world.get<Position2D>(e);
    REQUIRE(approxEq(pos->y, 0.0f));
}

TEST_CASE("PhysicsSystem2D - kinematic body moves by velocity, ignores gravity", "[physics]") {
    World world;
    PhysicsSystem2D sys;
    sys.setGravity({ 0.0f, -1000.0f });

    Entity e = world.create();
    world.add<Position2D>(e, Position2D{ 0.0f, 0.0f });
    world.add<Velocity2D>(e, Velocity2D{ 100.0f, 0.0f });
    RigidBody2D rb{};
    rb.isKinematic = true;
    world.add<RigidBody2D>(e, rb);
    world.add<Collider2D>(e);

    sys.onUpdate(world, 1.0f);

    auto* pos = world.get<Position2D>(e);
    REQUIRE(pos->x > 50.0f);
    REQUIRE(approxEq(pos->y, 0.0f));
}


TEST_CASE("PhysicsSystem2D - two AABB bodies collide and separate", "[physics]") {
    World world;
    PhysicsSystem2D sys;
    sys.setGravity({ 0.0f, 0.0f });

    Entity a = world.create();
    world.add<Position2D>(a, Position2D{ 0.0f, 0.0f });
    world.add<Velocity2D>(a, Velocity2D{ 50.0f, 0.0f });
    world.add<RigidBody2D>(a, RigidBody2D{ .mass = 1.0f, .restitution = 0.0f, .friction = 0.0f });
    Collider2D colA{}; colA.size = { 20.0f, 20.0f }; colA.layerMask = (1u << 0); colA.layer = 0;
    world.add<Collider2D>(a, colA);

    Entity b = world.create();
    world.add<Position2D>(b, Position2D{ 15.0f, 0.0f });
    world.add<Velocity2D>(b, Velocity2D{ 0.0f, 0.0f });
    world.add<RigidBody2D>(b, RigidBody2D{ .mass = 1.0f, .restitution = 0.0f, .friction = 0.0f });
    Collider2D colB{}; colB.size = { 20.0f, 20.0f }; colB.layerMask = (1u << 0); colB.layer = 0;
    world.add<Collider2D>(b, colB);

    sys.onUpdate(world, 1.0f / 60.0f);

    auto* velA = world.get<Velocity2D>(a);
    auto* velB = world.get<Velocity2D>(b);
    REQUIRE(velA != nullptr);
    REQUIRE(velB != nullptr);
    REQUIRE(velB->x > 0.0f);
}

TEST_CASE("PhysicsSystem2D - dynamic AABB vs static AABB", "[physics]") {
    World world;
    PhysicsSystem2D sys;
    sys.setGravity({ 0.0f, 0.0f });

    Entity dyn = world.create();
    world.add<Position2D>(dyn, Position2D{ 0.0f, 0.0f });
    world.add<Velocity2D>(dyn, Velocity2D{ 100.0f, 0.0f });
    world.add<RigidBody2D>(dyn, RigidBody2D{ .mass = 1.0f, .restitution = 1.0f, .friction = 0.0f });
    Collider2D dynCol{}; dynCol.size = { 20.0f, 20.0f }; dynCol.layer = 0; dynCol.layerMask = (1u << 1);
    world.add<Collider2D>(dyn, dynCol);

    Entity stat = world.create();
    world.add<Position2D>(stat, Position2D{ 15.0f, 0.0f });
    world.add<Velocity2D>(stat, Velocity2D{ 0.0f, 0.0f });
    world.add<RigidBody2D>(stat);
    Collider2D statCol{}; statCol.size = { 20.0f, 20.0f }; statCol.isStatic = true; statCol.layer = 1; statCol.layerMask = (1u << 0);
    world.add<Collider2D>(stat, statCol);

    sys.onUpdate(world, 1.0f / 60.0f);

    auto* vel = world.get<Velocity2D>(dyn);
    REQUIRE(vel != nullptr);
    REQUIRE(vel->x < 100.0f);
}


TEST_CASE("PhysicsSystem2D - two circles collide", "[physics]") {
    World world;
    PhysicsSystem2D sys;
    sys.setGravity({ 0.0f, 0.0f });

    Entity a = world.create();
    world.add<Position2D>(a, Position2D{ 0.0f, 0.0f });
    world.add<Velocity2D>(a, Velocity2D{ 100.0f, 0.0f });
    world.add<RigidBody2D>(a, RigidBody2D{ .mass = 1.0f, .restitution = 1.0f, .friction = 0.0f });
    Collider2D cA{}; cA.shape = ColliderShape::Circle; cA.radius = 16.0f; cA.layer = 0; cA.layerMask = (1u << 0);
    world.add<Collider2D>(a, cA);

    Entity b = world.create();
    world.add<Position2D>(b, Position2D{ 25.0f, 0.0f });
    world.add<Velocity2D>(b, Velocity2D{ 0.0f, 0.0f });
    world.add<RigidBody2D>(b, RigidBody2D{ .mass = 1.0f, .restitution = 1.0f, .friction = 0.0f });
    Collider2D cB{}; cB.shape = ColliderShape::Circle; cB.radius = 16.0f; cB.layer = 0; cB.layerMask = (1u << 0);
    world.add<Collider2D>(b, cB);

    sys.onUpdate(world, 1.0f / 60.0f);

    auto* velB = world.get<Velocity2D>(b);
    REQUIRE(velB != nullptr);
    REQUIRE(velB->x > 0.0f);
}


TEST_CASE("PhysicsSystem2D - trigger does not push bodies", "[physics]") {
    World world;
    Events::EventBus bus;
    PhysicsSystem2D sys(&bus);
    sys.setGravity({ 0.0f, 0.0f });

    bool triggered = false;
    bus.subscribe<Events::OnTrigger2D>([&](const Events::OnTrigger2D&) { triggered = true; });

    Entity a = world.create();
    world.add<Position2D>(a, Position2D{ 0.0f, 0.0f });
    world.add<Velocity2D>(a, Velocity2D{ 0.0f, 0.0f });
    world.add<RigidBody2D>(a);
    Collider2D colA{}; colA.size = { 30.0f, 30.0f }; colA.layer = 0; colA.layerMask = (1u << 0);
    world.add<Collider2D>(a, colA);

    Entity b = world.create();
    world.add<Position2D>(b, Position2D{ 10.0f, 0.0f });
    world.add<Velocity2D>(b, Velocity2D{ 0.0f, 0.0f });
    world.add<RigidBody2D>(b);
    Collider2D colB{}; colB.size = { 30.0f, 30.0f }; colB.isTrigger = true; colB.layer = 0; colB.layerMask = (1u << 0);
    world.add<Collider2D>(b, colB);

    sys.onUpdate(world, 1.0f / 60.0f);
    bus.dispatch();

    REQUIRE(triggered);
    auto* velA = world.get<Velocity2D>(a);
    REQUIRE(approxEq(velA->x, 0.0f));
}


TEST_CASE("PhysicsSystem2D - body falls asleep when velocity is near zero", "[physics]") {
    World world;
    PhysicsSystem2D sys;
    sys.setGravity({ 0.0f, 0.0f });

    Entity e = world.create();
    world.add<Position2D>(e);
    world.add<Velocity2D>(e, Velocity2D{ 0.1f, 0.0f });
    world.add<RigidBody2D>(e);
    world.add<Collider2D>(e);

    for (int i = 0; i < 200; ++i) {
        sys.onUpdate(world, 1.0f / 60.0f);
    }

    auto* rb = world.get<RigidBody2D>(e);
    REQUIRE(rb != nullptr);
    REQUIRE(rb->isSleeping);
}


TEST_CASE("PhysicsSystem2D - applyImpulse wakes sleeping body", "[physics]") {
    World world;
    PhysicsSystem2D sys;
    sys.setGravity({ 0.0f, 0.0f });

    Entity e = world.create();
    world.add<Position2D>(e);
    world.add<Velocity2D>(e);
    RigidBody2D rb{}; rb.isSleeping = true;
    world.add<RigidBody2D>(e, rb);
    world.add<Collider2D>(e);

    sys.applyImpulse(world, e, { 100.0f, 0.0f });

    auto* rbc = world.get<RigidBody2D>(e);
    REQUIRE_FALSE(rbc->isSleeping);
}

TEST_CASE("PhysicsSystem2D - teleport moves entity and wakes it", "[physics]") {
    World world;
    PhysicsSystem2D sys;

    Entity e = world.create();
    world.add<Position2D>(e, Position2D{ 0.0f, 0.0f });
    world.add<Velocity2D>(e);
    RigidBody2D rb{}; rb.isSleeping = true;
    world.add<RigidBody2D>(e, rb);
    world.add<Collider2D>(e);

    sys.teleport(world, e, { 500.0f, 250.0f });

    auto* pos = world.get<Position2D>(e);
    auto* rbc = world.get<RigidBody2D>(e);
    REQUIRE(approxEq(pos->x, 500.0f));
    REQUIRE(approxEq(pos->y, 250.0f));
    REQUIRE_FALSE(rbc->isSleeping);
}

TEST_CASE("PhysicsSystem2D - setKinematic changes flag", "[physics]") {
    World world;
    PhysicsSystem2D sys;

    Entity e = world.create();
    world.add<RigidBody2D>(e);

    sys.setKinematic(world, e, true);
    REQUIRE(world.get<RigidBody2D>(e)->isKinematic);

    sys.setKinematic(world, e, false);
    REQUIRE_FALSE(world.get<RigidBody2D>(e)->isKinematic);
}


TEST_CASE("PhysicsSystem2D - raycast hits AABB", "[physics]") {
    World world;
    PhysicsSystem2D sys;

    Entity e = world.create();
    world.add<Position2D>(e, Position2D{ 100.0f, 0.0f });
    Collider2D col{}; col.size = { 40.0f, 40.0f };
    world.add<Collider2D>(e, col);

    auto hit = sys.raycast(world, { 0.0f, 0.0f }, { 1.0f, 0.0f }, 200.0f);

    REQUIRE(hit.hit);
    REQUIRE(hit.distance < 200.0f);
}

TEST_CASE("PhysicsSystem2D - raycast misses when pointing away", "[physics]") {
    World world;
    PhysicsSystem2D sys;

    Entity e = world.create();
    world.add<Position2D>(e, Position2D{ 100.0f, 0.0f });
    Collider2D col{}; col.size = { 40.0f, 40.0f };
    world.add<Collider2D>(e, col);

    auto hit = sys.raycast(world, { 0.0f, 0.0f }, { -1.0f, 0.0f }, 200.0f);

    REQUIRE_FALSE(hit.hit);
}

TEST_CASE("PhysicsSystem2D - raycast hits circle", "[physics]") {
    World world;
    PhysicsSystem2D sys;

    Entity e = world.create();
    world.add<Position2D>(e, Position2D{ 50.0f, 0.0f });
    Collider2D col{}; col.shape = ColliderShape::Circle; col.radius = 20.0f;
    world.add<Collider2D>(e, col);

    auto hit = sys.raycast(world, { 0.0f, 0.0f }, { 1.0f, 0.0f }, 200.0f);

    REQUIRE(hit.hit);
    REQUIRE(hit.distance < 50.0f);
}

TEST_CASE("PhysicsSystem2D - raycast respects layerMask", "[physics]") {
    World world;
    PhysicsSystem2D sys;

    Entity e = world.create();
    world.add<Position2D>(e, Position2D{ 50.0f, 0.0f });
    Collider2D col{}; col.size = { 30.0f, 30.0f }; col.layer = 3;
    world.add<Collider2D>(e, col);

    auto hit = sys.raycast(world, { 0.0f, 0.0f }, { 1.0f, 0.0f }, 200.0f, (1u << 5));

    REQUIRE_FALSE(hit.hit);
}


TEST_CASE("PhysicsSystem2D - overlapCircle finds overlapping entity", "[physics]") {
    World world;
    PhysicsSystem2D sys;

    Entity e = world.create();
    world.add<Position2D>(e, Position2D{ 10.0f, 0.0f });
    Collider2D col{}; col.size = { 20.0f, 20.0f };
    world.add<Collider2D>(e, col);

    auto hits = sys.overlapCircle(world, { 0.0f, 0.0f }, 30.0f);
    REQUIRE(hits.size() >= 1);
}

TEST_CASE("PhysicsSystem2D - overlapCircle returns empty when no overlap", "[physics]") {
    World world;
    PhysicsSystem2D sys;

    Entity e = world.create();
    world.add<Position2D>(e, Position2D{ 500.0f, 500.0f });
    Collider2D col{}; col.size = { 20.0f, 20.0f };
    world.add<Collider2D>(e, col);

    auto hits = sys.overlapCircle(world, { 0.0f, 0.0f }, 30.0f);
    REQUIRE(hits.empty());
}

TEST_CASE("PhysicsSystem2D - overlapAABB finds overlapping entity", "[physics]") {
    World world;
    PhysicsSystem2D sys;

    Entity e = world.create();
    world.add<Position2D>(e, Position2D{ 5.0f, 5.0f });
    Collider2D col{}; col.size = { 20.0f, 20.0f };
    world.add<Collider2D>(e, col);

    Rect2D query{ Vec2{0.0f, 0.0f}, Vec2{30.0f, 30.0f} };
    auto hits = sys.overlapAABB(world, query);
    REQUIRE(hits.size() >= 1);
}


TEST_CASE("PhysicsSystem2D - collision event is published", "[physics]") {
    World world;
    Events::EventBus bus;
    PhysicsSystem2D sys(&bus);
    sys.setGravity({ 0.0f, 0.0f });

    bool received = false;
    bus.subscribe<Events::OnCollision2D>([&](const Events::OnCollision2D&) {
        received = true;
    });

    Entity a = world.create();
    world.add<Position2D>(a, Position2D{ 0.0f, 0.0f });
    world.add<Velocity2D>(a, Velocity2D{ 100.0f, 0.0f });
    world.add<RigidBody2D>(a, RigidBody2D{ .mass = 1.0f });
    Collider2D cA{}; cA.size = { 30.0f, 30.0f }; cA.layer = 0; cA.layerMask = (1u << 0);
    world.add<Collider2D>(a, cA);

    Entity b = world.create();
    world.add<Position2D>(b, Position2D{ 20.0f, 0.0f });
    world.add<Velocity2D>(b, Velocity2D{ 0.0f, 0.0f });
    world.add<RigidBody2D>(b, RigidBody2D{ .mass = 1.0f });
    Collider2D cB{}; cB.size = { 30.0f, 30.0f }; cB.layer = 0; cB.layerMask = (1u << 0);
    world.add<Collider2D>(b, cB);

    sys.onUpdate(world, 1.0f / 60.0f);
    bus.dispatch();

    REQUIRE(received);
}


TEST_CASE("PhysicsSystem2D - many bodies all fall under gravity", "[physics]") {
    World world;
    PhysicsSystem2D sys;
    sys.setGravity({ 0.0f, -100.0f });

    static constexpr int N = 20;
    for (int i = 0; i < N; ++i) {
        Entity e = world.create();
        world.add<Position2D>(e, Position2D{ static_cast<f32>(i) * 50.0f, 200.0f });
        world.add<Velocity2D>(e);
        world.add<RigidBody2D>(e);
        Collider2D col{}; col.size = { 10.0f, 10.0f };
        world.add<Collider2D>(e, col);
    }

    sys.onUpdate(world, 1.0f / 60.0f);

    int fallen = 0;
    ComponentQuery q; q.with<Position2D>();
    world.forEach<Position2D>(q, [&](Entity, Position2D& pos) {
        if (pos.y < 200.0f) ++fallen;
    });
    REQUIRE(fallen == N);
}
