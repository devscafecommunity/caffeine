#include "catch.hpp"
#include "../src/caffeine/Caffeine.hpp"
#include "../src/caffeine/physics/PhysicsComponents2D.hpp"
#include "../src/caffeine/physics/PhysicsSystem2D.hpp"

#include <cmath>

using namespace Caffeine;
using namespace Caffeine::ECS;
using namespace Caffeine::Physics2D;

static constexpr f32 kEps = 0.01f;

static bool approxEq(f32 a, f32 b) { return std::fabs(a - b) < kEps; }

namespace {

constexpr f32 kStep = 1.0f / 60.0f;

Entity spawn(World& world, f32 x, f32 y, Collider2D col, const RigidBody2D* body = nullptr) {
    Entity e = world.create();
    world.add<Transform>(e).position = Vec3(x, y, 0.0f);
    if (body) world.add<RigidBody2D>(e, *body);
    world.add<Collider2D>(e, col);
    return e;
}

Collider2D box(f32 w, f32 h, bool isStatic = false) {
    Collider2D c{};
    c.size = { w, h };
    c.isStatic = isStatic;
    return c;
}

Collider2D circle(f32 radius) {
    Collider2D c{};
    c.shape = ColliderShape::Circle;
    c.radius = radius;
    return c;
}

/// A floor that is only a collider (no RigidBody2D): the common way to build scenery. Its top is at y = 10.
Entity floorOf(World& world) { return spawn(world, 0.0f, 0.0f, box(2000.0f, 20.0f, true)); }

void run(World& world, PhysicsSystem2D& sys, f32 seconds, f32 frame = kStep) {
    const int frames = static_cast<int>(std::lround(seconds / frame));
    for (int i = 0; i < frames; ++i) sys.onUpdate(world, frame);
}

Vec3 at(World& world, Entity e) { return world.get<Transform>(e)->position; }
RigidBody2D& body(World& world, Entity e) { return *world.get<RigidBody2D>(e); }

}  // namespace



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
    RigidBody2D rb{};
    Entity e = spawn(world, 0.0f, 1000.0f, box(10, 10), &rb);
    run(world, sys, 1.0f);
    REQUIRE(1000.0f - at(world, e).y == Approx(50.0f).margin(1.5f));
}

TEST_CASE("PhysicsSystem2D - a static collider never moves, even when it carries a body", "[physics]") {
    World world;
    PhysicsSystem2D sys;
    sys.setGravity({ 0.0f, -500.0f });
    RigidBody2D rb{};
    Entity plain = spawn(world, 0.0f, 100.0f, box(10, 10, true));
    Entity withBody = spawn(world, 50.0f, 100.0f, box(10, 10, true), &rb);
    run(world, sys, 1.0f);
    REQUIRE(at(world, plain).y == Approx(100.0f));
    REQUIRE(at(world, withBody).y == Approx(100.0f));
}

TEST_CASE("PhysicsSystem2D - kinematic body moves by velocity and ignores gravity", "[physics]") {
    World world;
    PhysicsSystem2D sys;
    sys.setGravity({ 0.0f, -1000.0f });
    RigidBody2D rb{};
    rb.isKinematic = true;
    rb.velocityX = 100.0f;
    Entity e = spawn(world, 0.0f, 0.0f, box(10, 10), &rb);
    run(world, sys, 1.0f);
    REQUIRE(at(world, e).x == Approx(100.0f).margin(1.5f));
    REQUIRE(at(world, e).y == Approx(0.0f).margin(kEps));
}

TEST_CASE("PhysicsSystem2D - a kinematic platform carries nothing but pushes bodies", "[physics]") {
    World world;
    PhysicsSystem2D sys;
    sys.setGravity({ 0.0f, 0.0f });
    RigidBody2D mover{};
    mover.isKinematic = true;
    mover.velocityX = 60.0f;
    spawn(world, 0.0f, 0.0f, box(20, 20), &mover);
    RigidBody2D light{};
    light.restitution = 0.0f;
    Entity victim = spawn(world, 40.0f, 0.0f, box(20, 20), &light);
    run(world, sys, 1.0f);
    REQUIRE(at(world, victim).x > 60.0f);  // shoved ahead of the platform, not run through
}

TEST_CASE("PhysicsSystem2D - two AABB bodies collide and separate", "[physics]") {
    World world;
    PhysicsSystem2D sys;
    sys.setGravity({ 0.0f, 0.0f });
    RigidBody2D a{}; a.restitution = 0.0f; a.friction = 0.0f; a.velocityX = 50.0f;
    RigidBody2D b{}; b.restitution = 0.0f; b.friction = 0.0f;
    Entity ea = spawn(world, 0.0f, 0.0f, box(20, 20), &a);
    Entity eb = spawn(world, 15.0f, 0.0f, box(20, 20), &b);
    run(world, sys, 1.0f);
    REQUIRE(body(world, eb).velocityX > 0.0f);
    REQUIRE(at(world, eb).x - at(world, ea).x >= 19.0f);  // no longer overlapping
}

TEST_CASE("PhysicsSystem2D - an elastic hit on a static box bounces back", "[physics]") {
    World world;
    PhysicsSystem2D sys;
    sys.setGravity({ 0.0f, 0.0f });
    RigidBody2D dyn{}; dyn.restitution = 1.0f; dyn.friction = 0.0f; dyn.velocityX = 100.0f;
    Entity d = spawn(world, 0.0f, 0.0f, box(20, 20), &dyn);
    spawn(world, 15.0f, 0.0f, box(20, 20, true));
    run(world, sys, 0.2f);
    REQUIRE(body(world, d).velocityX < -50.0f);
}

TEST_CASE("PhysicsSystem2D - two circles collide", "[physics]") {
    World world;
    PhysicsSystem2D sys;
    sys.setGravity({ 0.0f, 0.0f });
    RigidBody2D a{}; a.restitution = 1.0f; a.friction = 0.0f; a.velocityX = 100.0f;
    RigidBody2D b{}; b.restitution = 1.0f; b.friction = 0.0f;
    Entity ea = spawn(world, 0.0f, 0.0f, circle(16), &a);
    Entity eb = spawn(world, 25.0f, 0.0f, circle(16), &b);
    run(world, sys, 0.3f);
    // Equal masses, elastic: the moving one stops and gives its speed to the other.
    REQUIRE(body(world, eb).velocityX == Approx(100.0f).margin(10.0f));
    REQUIRE(std::fabs(body(world, ea).velocityX) < 10.0f);
    REQUIRE(at(world, eb).x - at(world, ea).x >= 31.0f);
}

TEST_CASE("PhysicsSystem2D - momentum is shared by mass", "[physics]") {
    World world;
    PhysicsSystem2D sys;
    sys.setGravity({ 0.0f, 0.0f });
    RigidBody2D heavy{}; heavy.mass = 9.0f; heavy.restitution = 0.0f; heavy.friction = 0.0f; heavy.velocityX = 10.0f;
    RigidBody2D light{}; light.mass = 1.0f; light.restitution = 0.0f; light.friction = 0.0f;
    Entity h = spawn(world, 0.0f, 0.0f, box(20, 20), &heavy);
    Entity l = spawn(world, 15.0f, 0.0f, box(20, 20), &light);
    run(world, sys, 0.5f);
    // Perfectly inelastic: both end at (9 * 10) / 10 = 9.
    REQUIRE(body(world, h).velocityX == Approx(9.0f).margin(0.5f));
    REQUIRE(body(world, l).velocityX == Approx(9.0f).margin(0.5f));
}

TEST_CASE("PhysicsSystem2D - a box lands and rests on a floor that is only a collider", "[physics]") {
    World world;
    PhysicsSystem2D sys;
    sys.setGravity({ 0.0f, -500.0f });
    floorOf(world);
    RigidBody2D rb{}; rb.restitution = 0.0f;
    Entity e = spawn(world, 0.0f, 200.0f, box(20, 20), &rb);
    run(world, sys, 3.0f);
    REQUIRE(at(world, e).y == Approx(20.0f).margin(1.0f));  // floor top 10 + half the box
    REQUIRE(std::fabs(body(world, e).velocityY) < 15.0f);
}

TEST_CASE("PhysicsSystem2D - a circle rests on a box whichever was created first", "[physics]") {
    for (const bool floorFirst : { true, false }) {
        World world;
        PhysicsSystem2D sys;
        sys.setGravity({ 0.0f, -500.0f });
        RigidBody2D rb{}; rb.restitution = 0.0f;
        Entity ball;
        if (floorFirst) {
            floorOf(world);
            ball = spawn(world, 0.0f, 150.0f, circle(12), &rb);
        } else {
            ball = spawn(world, 0.0f, 150.0f, circle(12), &rb);
            floorOf(world);
        }
        run(world, sys, 3.0f);
        INFO("floor first: " << floorFirst);
        REQUIRE(at(world, ball).y == Approx(22.0f).margin(1.0f));
    }
}

TEST_CASE("PhysicsSystem2D - a box lands on a circle without being thrown sideways wrongly", "[physics]") {
    World world;
    PhysicsSystem2D sys;
    sys.setGravity({ 0.0f, -500.0f });
    floorOf(world);
    RigidBody2D ballBody{}; ballBody.restitution = 0.0f;
    Entity ball = spawn(world, 0.0f, 30.0f, circle(20), &ballBody);
    RigidBody2D crate{}; crate.restitution = 0.0f;
    Entity c = spawn(world, 0.0f, 120.0f, box(20, 20), &crate);
    run(world, sys, 3.0f);
    // The crate stays above the ball: the contact normal points the right way for both orders of the pair.
    REQUIRE(at(world, c).y > at(world, ball).y);
}

TEST_CASE("PhysicsSystem2D - a circle with its centre inside a box is pushed out by the nearest side", "[physics]") {
    World world;
    PhysicsSystem2D sys;
    sys.setGravity({ 0.0f, 0.0f });
    spawn(world, 0.0f, 0.0f, box(100, 100, true));
    RigidBody2D rb{}; rb.restitution = 0.0f;
    Entity ball = spawn(world, 40.0f, 5.0f, circle(10), &rb);  // 10 from the right side
    run(world, sys, 1.0f);
    REQUIRE(at(world, ball).x >= 59.0f);
    REQUIRE(std::fabs(at(world, ball).y - 5.0f) < 5.0f);
}

TEST_CASE("PhysicsSystem2D - restitution decides the bounce", "[physics]") {
    auto peakAfterBounce = [](f32 restitution) {
        World world;
        PhysicsSystem2D sys;
        sys.setGravity({ 0.0f, -500.0f });
        floorOf(world);
        RigidBody2D rb{}; rb.restitution = restitution;
        Entity e = spawn(world, 0.0f, 210.0f, circle(10), &rb);  // drop of 190
        bool landed = false;
        f32 peak = 0.0f;
        for (int i = 0; i < 300; ++i) {
            sys.onUpdate(world, kStep);
            const f32 y = at(world, e).y;
            if (body(world, e).velocityY > 0.0f) landed = true;
            if (landed) peak = std::max(peak, y);
        }
        return peak > 0.0f ? peak - 20.0f : 0.0f;  // height above the resting position
    };
    REQUIRE(peakAfterBounce(0.0f) < 2.0f);
    const f32 half = peakAfterBounce(0.5f);
    REQUIRE(half > 20.0f);
    REQUIRE(half < 95.0f);  // ideal 190 * 0.25 = 47
    REQUIRE(peakAfterBounce(0.9f) > half);
}

TEST_CASE("PhysicsSystem2D - friction slows a sliding box, ice does not", "[physics]") {
    auto slide = [](f32 friction) {
        World world;
        PhysicsSystem2D sys;
        sys.setGravity({ 0.0f, -500.0f });
        floorOf(world);
        RigidBody2D rb{}; rb.friction = friction; rb.restitution = 0.0f; rb.velocityX = 200.0f;
        Entity e = spawn(world, 0.0f, 20.0f, box(20, 20), &rb);
        run(world, sys, 1.0f);
        return at(world, e).x;
    };
    const f32 rough = slide(1.0f);
    const f32 ice = slide(0.0f);
    REQUIRE(ice > rough + 20.0f);
    REQUIRE(ice == Approx(200.0f).margin(15.0f));
}

TEST_CASE("PhysicsSystem2D - a stack of boxes stays stacked", "[physics]") {
    World world;
    PhysicsSystem2D sys;
    sys.setGravity({ 0.0f, -500.0f });
    floorOf(world);
    RigidBody2D rb{}; rb.restitution = 0.0f;
    Entity boxes[3];
    for (int i = 0; i < 3; ++i) boxes[i] = spawn(world, 0.0f, 20.0f + static_cast<f32>(i) * 21.0f, box(20, 20), &rb);
    run(world, sys, 4.0f);
    for (int i = 0; i < 3; ++i) {
        INFO("box " << i);
        REQUIRE(at(world, boxes[i]).y == Approx(20.0f + 20.0f * static_cast<f32>(i)).margin(3.0f));
    }
}

TEST_CASE("PhysicsSystem2D - a fast body does not tunnel through a thin wall", "[physics]") {
    World world;
    PhysicsSystem2D sys;
    sys.setGravity({ 0.0f, 0.0f });
    spawn(world, 100.0f, 0.0f, box(10, 400, true));
    RigidBody2D rb{}; rb.restitution = 0.0f; rb.velocityX = 1500.0f;  // 25 units per step: more than the wall's width
    Entity bullet = spawn(world, 0.0f, 0.0f, box(10, 10), &rb);
    run(world, sys, 0.5f);
    REQUIRE(at(world, bullet).x < 100.0f);
}

TEST_CASE("PhysicsSystem2D - bodies on layers that do not meet pass through", "[physics]") {
    World world;
    PhysicsSystem2D sys;
    sys.setGravity({ 0.0f, -500.0f });
    Collider2D floorCol = box(2000, 20, true);
    floorCol.layer = 1;
    floorCol.layerMask = (1u << 1);
    spawn(world, 0.0f, 0.0f, floorCol);
    Collider2D ghost = box(20, 20);
    ghost.layer = 2;
    ghost.layerMask = (1u << 2);
    RigidBody2D rb{};
    Entity e = spawn(world, 0.0f, 100.0f, ghost, &rb);
    run(world, sys, 1.5f);
    REQUIRE(at(world, e).y < -50.0f);
}

TEST_CASE("PhysicsSystem2D - a layer index above 31 neither crashes nor collides", "[physics]") {
    World world;
    PhysicsSystem2D sys;
    sys.setGravity({ 0.0f, 0.0f });
    Collider2D odd = box(20, 20);
    odd.layer = 40;
    RigidBody2D rb{};
    spawn(world, 0.0f, 0.0f, odd, &rb);
    spawn(world, 5.0f, 0.0f, odd, &rb);
    run(world, sys, 0.1f);
}

TEST_CASE("PhysicsSystem2D - trigger does not push bodies and reports enter and exit once", "[physics]") {
    World world;
    Events::EventBus bus;
    PhysicsSystem2D sys(&bus);
    sys.setGravity({ 0.0f, 0.0f });
    int entered = 0, exited = 0;
    u32 reportedTrigger = 0;
    bus.subscribe<Events::OnTrigger2D>([&](const Events::OnTrigger2D& ev) {
        (ev.entered ? entered : exited)++;
        reportedTrigger = ev.triggerEntity;
    });
    Collider2D zone = box(60, 60, true);
    zone.isTrigger = true;
    Entity z = spawn(world, 100.0f, 0.0f, zone);
    RigidBody2D rb{}; rb.velocityX = 100.0f;
    Entity walker = spawn(world, 0.0f, 0.0f, box(20, 20), &rb);
    for (int i = 0; i < 180; ++i) {
        sys.onUpdate(world, kStep);
        bus.dispatch();
    }
    REQUIRE(entered == 1);
    REQUIRE(exited == 1);
    REQUIRE(reportedTrigger == z.id());
    REQUIRE(body(world, walker).velocityX == Approx(100.0f).margin(0.5f));  // never pushed
    REQUIRE(at(world, walker).x > 200.0f);
}

TEST_CASE("PhysicsSystem2D - a one-way platform holds from above and lets through from below", "[physics]") {
    auto build = [](World& world, bool platformFirst, f32 startY, f32 vy) {
        Collider2D platform = box(200, 10, true);
        platform.isOneWay = true;
        RigidBody2D rb{}; rb.restitution = 0.0f; rb.velocityY = vy;
        if (platformFirst) {
            spawn(world, 0.0f, 0.0f, platform);
            return spawn(world, 0.0f, startY, box(20, 20), &rb);
        }
        Entity e = spawn(world, 0.0f, startY, box(20, 20), &rb);
        spawn(world, 0.0f, 0.0f, platform);
        return e;
    };
    for (const bool platformFirst : { true, false }) {
        INFO("platform first: " << platformFirst);
        {  // landing from above
            World world;
            PhysicsSystem2D sys;
            sys.setGravity({ 0.0f, -500.0f });
            Entity e = build(world, platformFirst, 80.0f, 0.0f);
            run(world, sys, 2.0f);
            REQUIRE(at(world, e).y == Approx(15.0f).margin(2.0f));
        }
        {  // jumping up through it from below
            World world;
            PhysicsSystem2D sys;
            sys.setGravity({ 0.0f, -500.0f });
            Entity e = build(world, platformFirst, -40.0f, 420.0f);
            run(world, sys, 0.6f);
            REQUIRE(at(world, e).y > 40.0f);
        }
    }
}

TEST_CASE("PhysicsSystem2D - body falls asleep when it stops, and a push or a force wakes it", "[physics]") {
    World world;
    PhysicsSystem2D sys;
    sys.setGravity({ 0.0f, 0.0f });
    RigidBody2D rb{}; rb.velocityX = 0.1f; rb.linearDamping = 2.0f;
    Entity e = spawn(world, 0.0f, 0.0f, box(10, 10), &rb);
    run(world, sys, 3.0f);
    REQUIRE(body(world, e).isSleeping);

    sys.applyForce(e, { 50.0f, 0.0f });  // a force on a sleeping body used to be lost
    sys.onUpdate(world, kStep);
    REQUIRE_FALSE(body(world, e).isSleeping);
    REQUIRE(body(world, e).velocityX > 0.5f);

    run(world, sys, 3.0f);
    REQUIRE(body(world, e).isSleeping);
    sys.applyImpulse(world, e, { 100.0f, 0.0f });
    REQUIRE_FALSE(body(world, e).isSleeping);
    sys.onUpdate(world, kStep);
    REQUIRE(body(world, e).velocityX == Approx(100.0f).margin(5.0f));
}

TEST_CASE("PhysicsSystem2D - gravity keeps bodies awake", "[physics]") {
    World world;
    PhysicsSystem2D sys;
    sys.setGravity({ 0.0f, -500.0f });
    floorOf(world);
    RigidBody2D rb{}; rb.restitution = 0.0f;
    Entity e = spawn(world, 0.0f, 20.0f, box(20, 20), &rb);
    run(world, sys, 3.0f);
    REQUIRE_FALSE(body(world, e).isSleeping);
}

TEST_CASE("PhysicsSystem2D - teleport moves entity and wakes it", "[physics]") {
    World world;
    PhysicsSystem2D sys;
    RigidBody2D rb{}; rb.isSleeping = true;
    Entity e = spawn(world, 0.0f, 0.0f, box(10, 10), &rb);
    sys.teleport(world, e, { 500.0f, 250.0f });
    REQUIRE(approxEq(at(world, e).x, 500.0f));
    REQUIRE(approxEq(at(world, e).y, 250.0f));
    REQUIRE_FALSE(body(world, e).isSleeping);
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

TEST_CASE("PhysicsSystem2D - raycast hits boxes and circles with the right distance and normal", "[physics]") {
    World world;
    PhysicsSystem2D sys;
    spawn(world, 100.0f, 0.0f, box(40, 40));
    auto hit = sys.raycast(world, { 0.0f, 0.0f }, { 1.0f, 0.0f }, 200.0f);
    REQUIRE(hit.hit);
    REQUIRE(hit.distance == Approx(80.0f).margin(0.1f));
    REQUIRE(hit.normal.x == Approx(-1.0f));
    REQUIRE(hit.point.x == Approx(80.0f).margin(0.1f));

    World other;
    spawn(other, 50.0f, 0.0f, circle(20));
    auto c = sys.raycast(other, { 0.0f, 0.0f }, { 1.0f, 0.0f }, 200.0f);
    REQUIRE(c.hit);
    REQUIRE(c.distance == Approx(30.0f).margin(0.1f));
    REQUIRE(c.normal.x == Approx(-1.0f).margin(0.01f));
}

TEST_CASE("PhysicsSystem2D - raycast misses, respects range and layer mask", "[physics]") {
    World world;
    PhysicsSystem2D sys;
    Collider2D col = box(30, 30);
    col.layer = 3;
    col.layerMask = (1u << 3);
    spawn(world, 50.0f, 0.0f, col);
    REQUIRE_FALSE(sys.raycast(world, { 0.0f, 0.0f }, { -1.0f, 0.0f }, 200.0f).hit);
    REQUIRE_FALSE(sys.raycast(world, { 0.0f, 0.0f }, { 1.0f, 0.0f }, 20.0f).hit);
    REQUIRE_FALSE(sys.raycast(world, { 0.0f, 0.0f }, { 1.0f, 0.0f }, 200.0f, (1u << 5)).hit);
    REQUIRE(sys.raycast(world, { 0.0f, 0.0f }, { 1.0f, 0.0f }, 200.0f, (1u << 3)).hit);
}

TEST_CASE("PhysicsSystem2D - overlap queries", "[physics]") {
    World world;
    PhysicsSystem2D sys;
    Entity near = spawn(world, 10.0f, 0.0f, box(20, 20));
    spawn(world, 500.0f, 500.0f, box(20, 20));
    auto circles = sys.overlapCircle(world, { 0.0f, 0.0f }, 30.0f);
    REQUIRE(circles.size() == 1);
    REQUIRE(circles[0].id() == near.id());
    Rect2D rect{ Vec2{ 0.0f, 0.0f }, Vec2{ 30.0f, 30.0f } };
    auto boxes = sys.overlapAABB(world, rect);
    REQUIRE(boxes.size() == 1);
    REQUIRE(sys.overlapCircle(world, { 2000.0f, 0.0f }, 30.0f).empty());
}

TEST_CASE("PhysicsSystem2D - collision event is published once per pair per frame", "[physics]") {
    World world;
    Events::EventBus bus;
    PhysicsSystem2D sys(&bus);
    sys.setGravity({ 0.0f, 0.0f });
    int received = 0;
    bus.subscribe<Events::OnCollision2D>([&](const Events::OnCollision2D&) { ++received; });
    RigidBody2D a{}; a.velocityX = 100.0f;
    RigidBody2D b{};
    spawn(world, 0.0f, 0.0f, box(30, 30), &a);
    spawn(world, 20.0f, 0.0f, box(30, 30), &b);
    sys.onUpdate(world, kStep);
    bus.dispatch();
    REQUIRE(received == 1);
}

TEST_CASE("PhysicsSystem2D - many bodies all fall and none sinks into the floor", "[physics]") {
    World world;
    PhysicsSystem2D sys;
    sys.setGravity({ 0.0f, -500.0f });
    floorOf(world);
    RigidBody2D rb{}; rb.restitution = 0.0f;
    std::vector<Entity> all;
    for (int i = 0; i < 60; ++i) {
        all.push_back(spawn(world, static_cast<f32>(i) * 30.0f - 900.0f, 100.0f + static_cast<f32>(i % 5) * 40.0f, box(10, 10), &rb));
    }
    run(world, sys, 4.0f);
    for (const Entity e : all) REQUIRE(at(world, e).y == Approx(15.0f).margin(2.0f));
}

TEST_CASE("PhysicsSystem2D - the same scene gives the same result every time", "[physics]") {
    auto simulate = [] {
        World world;
        PhysicsSystem2D sys;
        sys.setGravity({ 0.0f, -500.0f });
        floorOf(world);
        RigidBody2D rb{};
        std::vector<Entity> all;
        for (int i = 0; i < 12; ++i) all.push_back(spawn(world, static_cast<f32>(i % 4) * 12.0f, 60.0f + static_cast<f32>(i) * 22.0f, circle(10), &rb));
        run(world, sys, 3.0f);
        std::vector<f32> out;
        for (const Entity e : all) { out.push_back(at(world, e).x); out.push_back(at(world, e).y); }
        return out;
    };
    REQUIRE(simulate() == simulate());
}

TEST_CASE("PhysicsSystem2D - a fall, a force and a landing are the same at any frame rate", "[physics][framerate]") {
    auto fall = [](f32 frame) {
        World world;
        PhysicsSystem2D sys;
        sys.setGravity({ 0.0f, -100.0f });
        RigidBody2D rb{};
        Entity e = spawn(world, 0.0f, 1000.0f, box(10, 10), &rb);
        run(world, sys, 1.0f, frame);
        return 1000.0f - at(world, e).y;
    };
    auto push = [](f32 frame) {
        World world;
        PhysicsSystem2D sys;
        sys.setGravity({ 0.0f, 0.0f });
        RigidBody2D rb{}; rb.mass = 2.0f;
        Entity e = spawn(world, 0.0f, 1000.0f, box(10, 10), &rb);
        const int frames = static_cast<int>(std::lround(1.0f / frame));
        for (int i = 0; i < frames; ++i) {
            sys.applyForce(e, { 20.0f, 0.0f });
            sys.onUpdate(world, frame);
        }
        return body(world, e).velocityX;
    };
    auto rest = [](f32 frame) {
        World world;
        PhysicsSystem2D sys;
        sys.setGravity({ 0.0f, -500.0f });
        floorOf(world);
        RigidBody2D rb{}; rb.restitution = 0.0f;
        Entity e = spawn(world, 0.0f, 150.0f, box(20, 20), &rb);
        run(world, sys, 3.0f, frame);
        return at(world, e).y;
    };
    const f32 f60 = fall(1.0f / 60.0f), p60 = push(1.0f / 60.0f), r60 = rest(1.0f / 60.0f);
    REQUIRE(f60 == Approx(50.0f).margin(1.5f));
    REQUIRE(p60 == Approx(10.0f).margin(0.3f));  // 20 N on 2 kg for one second
    for (const f32 frame : { 1.0f / 240.0f, 1.0f / 144.0f, 1.0f / 30.0f, 1.0f / 15.0f }) {
        INFO("frame length " << frame);
        REQUIRE(fall(frame) == Approx(f60).margin(1.5f));
        REQUIRE(push(frame) == Approx(p60).margin(0.3f));
        REQUIRE(rest(frame) == Approx(r60).margin(1.0f));
    }
}

TEST_CASE("PhysicsSystem2D - a body under a moved parent lives in world space", "[physics][hierarchy]") {
    World world;
    PhysicsSystem2D sys;
    sys.setGravity({ 0.0f, -500.0f });
    spawn(world, 500.0f, 0.0f, box(400.0f, 20.0f, true));  // floor: world top at y = 10
    Entity group = world.create();
    world.add<Transform>(group).position = Vec3(500.0f, 100.0f, 0.0f);
    RigidBody2D rb{}; rb.restitution = 0.0f;
    Entity child = spawn(world, 0.0f, 200.0f, box(20.0f, 20.0f), &rb);  // world (500, 300)
    world.add<Scene::Parent>(child).parent = group;
    run(world, sys, 3.0f);
    const Vec3 worldPos = Scene::computeWorldMatrix(world, child).transformPoint(Vec3(0.0f, 0.0f, 0.0f));
    REQUIRE(worldPos.x == Approx(500.0f).margin(1.0f));
    REQUIRE(worldPos.y == Approx(20.0f).margin(1.0f));     // landed on the floor in world space
    REQUIRE(at(world, child).y == Approx(-80.0f).margin(1.0f));  // and the local value is relative to the parent
    REQUIRE(at(world, child).x == Approx(0.0f).margin(1.0f));
}

TEST_CASE("PhysicsSystem2D - a scaled and turned parent changes local moves, not world speed", "[physics][hierarchy]") {
    World world;
    PhysicsSystem2D sys;
    sys.setGravity({ 0.0f, 0.0f });
    Entity group = world.create();
    auto& t = world.add<Transform>(group);
    t.scale = Vec3(2.0f, 2.0f, 1.0f);
    t.rotation = Vec3(0.0f, 0.0f, 90.0f);  // local +x points up in the world
    RigidBody2D rb{}; rb.isKinematic = true; rb.velocityX = 100.0f;  // world speed: along world +x
    Entity child = spawn(world, 0.0f, 0.0f, box(10.0f, 10.0f), &rb);
    world.add<Scene::Parent>(child).parent = group;
    run(world, sys, 1.0f);
    const Vec3 worldPos = Scene::computeWorldMatrix(world, child).transformPoint(Vec3(0.0f, 0.0f, 0.0f));
    REQUIRE(worldPos.x == Approx(100.0f).margin(2.0f));
    REQUIRE(worldPos.y == Approx(0.0f).margin(2.0f));
}

TEST_CASE("PhysicsSystem2D - teleport and queries use the world position of a child", "[physics][hierarchy]") {
    World world;
    PhysicsSystem2D sys;
    Entity group = world.create();
    world.add<Transform>(group).position = Vec3(1000.0f, 0.0f, 0.0f);
    RigidBody2D rb{};
    Entity child = spawn(world, 0.0f, 0.0f, box(40.0f, 40.0f), &rb);
    world.add<Scene::Parent>(child).parent = group;
    // At world x = 1000: a ray from the left finds it, and an overlap there sees it.
    auto hit = sys.raycast(world, { 900.0f, 0.0f }, { 1.0f, 0.0f }, 200.0f);
    REQUIRE(hit.hit);
    REQUIRE(hit.distance == Approx(80.0f).margin(0.1f));
    REQUIRE(sys.overlapCircle(world, { 1000.0f, 0.0f }, 5.0f).size() == 1);
    REQUIRE(sys.overlapCircle(world, { 0.0f, 0.0f }, 5.0f).empty());
    sys.teleport(world, child, { 1500.0f, 300.0f });
    const Vec3 worldPos = Scene::computeWorldMatrix(world, child).transformPoint(Vec3(0.0f, 0.0f, 0.0f));
    REQUIRE(worldPos.x == Approx(1500.0f).margin(0.1f));
    REQUIRE(worldPos.y == Approx(300.0f).margin(0.1f));
    REQUIRE(at(world, child).x == Approx(500.0f).margin(0.1f));
}
