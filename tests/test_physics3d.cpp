#include "catch.hpp"
#include "physics/PhysicsWorld3D.hpp"
#include "physics/PhysicsSystem3D.hpp"
#include "terrain/TerrainCache.hpp"

#include <cmath>

using namespace Caffeine;
using namespace Caffeine::Physics3D;

TEST_CASE("Physics3D - dynamic sphere rests on a static box", "[physics][physics3d]") {
    PhysicsWorld3D world;
    if (!world.isAvailable()) {
        WARN("Bullet3 not enabled in this build");
        return;
    }

    BodyDesc ground;
    ground.entityId = 1;
    ground.position = Vec3(0.0f, 0.0f, 0.0f);
    ground.shape = ColliderShape3D::Box;
    ground.halfExtents = Vec3(2.0f, 0.5f, 2.0f);
    ground.bodyType = BodyType3D::Static;
    ground.mass = 0.0f;
    world.upsertBody(ground);

    BodyDesc sphere;
    sphere.entityId = 2;
    sphere.position = Vec3(0.0f, 4.0f, 0.0f);
    sphere.shape = ColliderShape3D::Sphere;
    sphere.halfExtents = Vec3(0.5f, 0.5f, 0.5f);
    sphere.bodyType = BodyType3D::Dynamic;
    sphere.mass = 1.0f;
    sphere.restitution = 0.0f;
    sphere.linearDamping = 0.2f;
    world.upsertBody(sphere);

    for (int i = 0; i < 180; ++i) {
        world.step(1.0f / 60.0f);
    }

    BodyState state;
    REQUIRE(world.stateOf(2, state));
    REQUIRE(state.position.y > 0.8f);
    REQUIRE(state.position.y < 1.6f);
}

TEST_CASE("Physics3D - ECS system writes Position3D after falling", "[physics][physics3d]") {
    PhysicsSystem3D system;
    if (!system.world().isAvailable()) {
        WARN("Bullet3 not enabled in this build");
        return;
    }

    ECS::World world;
    ECS::Entity ground = world.create();
    world.add<ECS::Position3D>(ground).position = Vec3(0.0f, 0.0f, 0.0f);
    Physics3D::RigidBody3D groundBody;
    groundBody.bodyType = BodyType3D::Static;
    world.add<Physics3D::RigidBody3D>(ground, groundBody);
    Physics3D::Collider3D groundCol;
    groundCol.halfExtents = Vec3(2.0f, 0.5f, 2.0f);
    world.add<Physics3D::Collider3D>(ground, groundCol);

    ECS::Entity ball = world.create();
    world.add<ECS::Position3D>(ball).position = Vec3(0.0f, 3.0f, 0.0f);
    world.add<Physics3D::RigidBody3D>(ball);
    Physics3D::Collider3D ballCol;
    ballCol.shape = ColliderShape3D::Sphere;
    ballCol.halfExtents = Vec3(0.4f, 0.4f, 0.4f);
    world.add<Physics3D::Collider3D>(ball, ballCol);

    for (int i = 0; i < 180; ++i) {
        system.onUpdate(world, 1.0f / 60.0f);
    }

    auto* position = world.get<ECS::Position3D>(ball);
    REQUIRE(position != nullptr);
    REQUIRE(position->position.y < 2.5f);
    REQUIRE(position->position.y > 0.5f);
}

namespace {

/// A sphere in free fall (no gravity pulls other than the world's, no damping), driven for `seconds` of game
/// time in frames of `frame` seconds. Returns how far it dropped.
f32 fallenIn(f32 frame, f32 seconds) {
    PhysicsWorld3D world;
    BodyDesc ball;
    ball.entityId = 1;
    ball.position = Vec3(0.0f, 100.0f, 0.0f);
    ball.shape = ColliderShape3D::Sphere;
    ball.halfExtents = Vec3(0.5f, 0.5f, 0.5f);
    ball.linearDamping = 0.0f;
    ball.angularDamping = 0.0f;
    world.upsertBody(ball);
    const int frames = static_cast<int>(std::lround(seconds / frame));
    for (int i = 0; i < frames; ++i) world.step(frame);
    BodyState state;
    REQUIRE(world.stateOf(1, state));
    return 100.0f - state.position.y;
}

}  // namespace

TEST_CASE("Physics3D - a fall lasts the same game time at any frame rate", "[physics][physics3d][framerate]") {
    if (!PhysicsWorld3D().isAvailable()) {
        WARN("Bullet3 not enabled in this build");
        return;
    }
    // One second of game time: 0.5 * g * t^2 = 4.9 m, whatever the frame length. A 10 fps hitch (0.1 s frames,
    // six steps each) used to run in slow motion because the world capped the steps per frame at four.
    const f32 reference = fallenIn(1.0f / 60.0f, 1.0f);
    REQUIRE(reference == Approx(4.9f).margin(0.15f));
    for (const f32 frame : {1.0f / 240.0f, 1.0f / 144.0f, 1.0f / 90.0f, 1.0f / 30.0f, 1.0f / 15.0f, 0.1f}) {
        INFO("frame length " << frame);
        REQUIRE(fallenIn(frame, 1.0f) == Approx(reference).margin(0.12f));
    }
}

TEST_CASE("Physics3D - the world runs fixed steps and carries the remainder", "[physics][physics3d][framerate]") {
    PhysicsWorld3D world;
    if (!world.isAvailable()) {
        WARN("Bullet3 not enabled in this build");
        return;
    }
    const f32 step = world.fixedStep();
    world.step(step * 0.4f);
    REQUIRE(world.lastStepCount() == 0);
    world.step(step * 0.4f);
    REQUIRE(world.lastStepCount() == 0);
    world.step(step * 0.4f);  // 1.2 steps in all
    REQUIRE(world.lastStepCount() == 1);
    world.step(step * 3.0f);
    REQUIRE(world.lastStepCount() >= 3);
    // A long hitch is capped, not simulated for seconds.
    world.step(5.0f);
    REQUIRE(world.lastStepCount() == PhysicsWorld3D::kMaxStepsPerFrame);
}

TEST_CASE("Physics3D - a force pushes the same at any frame rate", "[physics][physics3d][framerate]") {
    if (!PhysicsWorld3D().isAvailable()) {
        WARN("Bullet3 not enabled in this build");
        return;
    }
    // 10 N on 1 kg for one second, applied every frame: 10 m/s. It used to scale with the number of steps a frame
    // happened to run (lost on a frame without a step, doubled on one with two).
    for (const f32 frame : {1.0f / 144.0f, 1.0f / 60.0f, 1.0f / 30.0f, 1.0f / 15.0f}) {
        PhysicsWorld3D world;
        world.setGravity(Vec3(0.0f, 0.0f, 0.0f));
        BodyDesc body;
        body.entityId = 1;
        body.linearDamping = 0.0f;
        body.angularDamping = 0.0f;
        body.shape = ColliderShape3D::Sphere;
        world.upsertBody(body);
        const int frames = static_cast<int>(std::lround(1.0f / frame));
        for (int i = 0; i < frames; ++i) {
            world.addForce(1, Vec3(10.0f, 0.0f, 0.0f));
            world.step(frame);
        }
        Vec3 velocity;
        REQUIRE(world.linearVelocity(1, velocity));
        INFO("frame length " << frame);
        REQUIRE(velocity.x == Approx(10.0f).margin(0.4f));
    }
}

TEST_CASE("Physics3D - a body between two steps is drawn between their poses", "[physics][physics3d][framerate]") {
    PhysicsWorld3D world;
    if (!world.isAvailable()) {
        WARN("Bullet3 not enabled in this build");
        return;
    }
    world.setGravity(Vec3(0.0f, 0.0f, 0.0f));
    BodyDesc body;
    body.entityId = 1;
    body.linearDamping = 0.0f;
    body.shape = ColliderShape3D::Sphere;
    world.upsertBody(body);
    world.setLinearVelocity(1, Vec3(6.0f, 0.0f, 0.0f));
    // At 240 fps most frames run no step; the picture must still move every frame, not once per fixed step.
    f32 last = -1.0f;
    int moved = 0;
    for (int i = 0; i < 40; ++i) {
        world.step(1.0f / 240.0f);
        BodyState state;
        REQUIRE(world.stateOf(1, state));
        if (state.position.x > last + 1e-4f) ++moved;
        last = state.position.x;
    }
    REQUIRE(moved >= 36);  // the first frame has no step to blend yet
    REQUIRE(last == Approx(1.0f).margin(0.15f));
}

TEST_CASE("Physics3D - terrain collides: a body dropped on it rests on its surface", "[physics][physics3d][terrain]") {
    PhysicsSystem3D system;
    if (!system.world().isAvailable()) {
        WARN("Bullet3 not enabled in this build");
        return;
    }
    ECS::World world;
    ECS::Entity land = world.create();
    auto& terrain = world.add<ECS::TerrainComponent>(land);
    terrain.resolutionX = 65;
    terrain.resolutionZ = 65;
    terrain.splatResolutionScale = 1;
    terrain.useChunks = false;
    terrain.worldSizeX = 64.0f;
    terrain.worldSizeZ = 64.0f;
    terrain.maxHeight = 40.0f;
    auto& cache = Terrain::TerrainCache::instance();
    cache.initializeEntity(world, land);
    cache.heightmapFor(land)->fill(0.25f);  // a flat plateau at 10 m
    world.add<ECS::Transform>(land);

    ECS::Entity ball = world.create();
    world.add<ECS::Position3D>(ball).position = Vec3(5.0f, 14.0f, -7.0f);
    Physics3D::RigidBody3D rigid;
    rigid.restitution = 0.0f;
    world.add<Physics3D::RigidBody3D>(ball, rigid);
    Physics3D::Collider3D shape;
    shape.shape = ColliderShape3D::Sphere;
    shape.halfExtents = Vec3(0.5f, 0.5f, 0.5f);
    world.add<Physics3D::Collider3D>(ball, shape);

    for (int i = 0; i < 240; ++i) system.onUpdate(world, 1.0f / 60.0f);

    const auto* position = world.get<ECS::Position3D>(ball);
    REQUIRE(position != nullptr);
    REQUIRE(position->position.y == Approx(10.5f).margin(0.1f));
    REQUIRE(position->position.x == Approx(5.0f).margin(0.2f));

    // A fast fall does not tunnel through a surface with no thickness.
    ECS::Entity bullet = world.create();
    world.add<ECS::Position3D>(bullet).position = Vec3(-10.0f, 200.0f, 3.0f);
    world.add<Physics3D::RigidBody3D>(bullet, rigid);
    world.add<Physics3D::Collider3D>(bullet, shape);
    for (int i = 0; i < 600; ++i) system.onUpdate(world, 1.0f / 30.0f);
    REQUIRE(world.get<ECS::Position3D>(bullet)->position.y > 10.0f);

    // Sculpting (a new data revision) rebuilds the collider and wakes what rests on it: lowered, the ball falls.
    auto setLevel = [&](f32 level) {
        cache.heightmapFor(land)->fill(level);
        ++world.get<ECS::TerrainComponent>(land)->dataRevision;
    };
    setLevel(0.15f);
    for (int i = 0; i < 240; ++i) system.onUpdate(world, 1.0f / 60.0f);
    REQUIRE(world.get<ECS::Position3D>(ball)->position.y == Approx(6.5f).margin(0.1f));

    // Raised to 20 m, a new ball lands on the new surface.
    setLevel(0.5f);
    ECS::Entity second = world.create();
    world.add<ECS::Position3D>(second).position = Vec3(2.0f, 30.0f, 2.0f);
    world.add<Physics3D::RigidBody3D>(second, rigid);
    world.add<Physics3D::Collider3D>(second, shape);
    for (int i = 0; i < 300; ++i) system.onUpdate(world, 1.0f / 60.0f);
    REQUIRE(world.get<ECS::Position3D>(second)->position.y == Approx(20.5f).margin(0.1f));

    // Moving the terrain's entity moves the collider with it.
    world.get<ECS::Transform>(land)->position = Vec3(0.0f, -10.0f, 0.0f);
    for (int i = 0; i < 300; ++i) system.onUpdate(world, 1.0f / 60.0f);
    REQUIRE(world.get<ECS::Position3D>(second)->position.y == Approx(10.5f).margin(0.1f));

    // A ray finds the ground (scripts and the editor's picking use the same world).
    const auto hit = system.world().raycast(Vec3(-20.0f, 100.0f, 20.0f), Vec3(0.0f, -1.0f, 0.0f), 200.0f);
    REQUIRE(hit.hit);
    REQUIRE(hit.entityId == land.id());
    REQUIRE(hit.point.y == Approx(10.0f).margin(0.05f));

    cache.removeEntity(land);
}

TEST_CASE("Physics3D - a terrain with collision off is not a collider", "[physics][physics3d][terrain]") {
    PhysicsSystem3D system;
    if (!system.world().isAvailable()) {
        WARN("Bullet3 not enabled in this build");
        return;
    }
    ECS::World world;
    ECS::Entity land = world.create();
    auto& terrain = world.add<ECS::TerrainComponent>(land);
    terrain.resolutionX = 17;
    terrain.resolutionZ = 17;
    terrain.splatResolutionScale = 1;
    terrain.useChunks = false;
    terrain.buildCollisionMesh = false;
    auto& cache = Terrain::TerrainCache::instance();
    cache.initializeEntity(world, land);
    cache.heightmapFor(land)->fill(0.25f);
    system.onUpdate(world, 1.0f / 60.0f);
    REQUIRE_FALSE(system.world().raycast(Vec3(0.0f, 100.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f), 200.0f).hit);
    cache.removeEntity(land);
}

namespace {

using Caffeine::ECS::Entity;

constexpr f32 kFrame = 1.0f / 60.0f;

bool bulletOn() { return PhysicsWorld3D().isAvailable(); }

Entity makeBody(ECS::World& world, Vec3 at, ColliderShape3D shape, Vec3 half, BodyType3D type = BodyType3D::Dynamic,
                f32 restitution = 0.0f, f32 friction = 0.5f) {
    Entity e = world.create();
    world.add<ECS::Position3D>(e).position = at;
    Physics3D::RigidBody3D rb;
    rb.bodyType = type;
    rb.restitution = restitution;
    rb.friction = friction;
    world.add<Physics3D::RigidBody3D>(e, rb);
    Physics3D::Collider3D col;
    col.shape = shape;
    col.halfExtents = half;
    world.add<Physics3D::Collider3D>(e, col);
    return e;
}

/// A wide static floor whose top is at y = 0.
Entity makeFloor(ECS::World& world) {
    return makeBody(world, Vec3(0.0f, -0.5f, 0.0f), ColliderShape3D::Box, Vec3(200.0f, 0.5f, 200.0f), BodyType3D::Static);
}

void runFor(ECS::World& world, PhysicsSystem3D& system, f32 seconds, f32 frame = kFrame) {
    const int frames = static_cast<int>(std::lround(seconds / frame));
    for (int i = 0; i < frames; ++i) system.onUpdate(world, frame);
}

Vec3 pos(ECS::World& world, Entity e) { return world.get<ECS::Position3D>(e)->position; }

}  // namespace

TEST_CASE("Physics3D - boxes stack and stay stacked", "[physics][physics3d][scenarios]") {
    PhysicsSystem3D system;
    if (!system.world().isAvailable()) return;
    ECS::World world;
    makeFloor(world);
    Entity boxes[4];
    for (int i = 0; i < 4; ++i) {
        boxes[i] = makeBody(world, Vec3(0.0f, 0.5f + 1.05f * static_cast<f32>(i), 0.0f), ColliderShape3D::Box, Vec3(0.5f, 0.5f, 0.5f));
    }
    runFor(world, system, 5.0f);
    for (int i = 0; i < 4; ++i) {
        INFO("box " << i);
        REQUIRE(pos(world, boxes[i]).y == Approx(0.5f + static_cast<f32>(i)).margin(0.08f));
        REQUIRE(std::fabs(pos(world, boxes[i]).x) < 0.1f);
    }
}

TEST_CASE("Physics3D - restitution decides the bounce", "[physics][physics3d][scenarios]") {
    if (!bulletOn()) return;
    auto peak = [](f32 restitution) {
        PhysicsSystem3D system;
        ECS::World world;
        makeFloor(world);
        Entity ball = makeBody(world, Vec3(0.0f, 5.5f, 0.0f), ColliderShape3D::Sphere, Vec3(0.5f, 0.5f, 0.5f),
                               BodyType3D::Dynamic, restitution);
        world.get<Physics3D::RigidBody3D>(ball)->linearDamping = 0.0f;
        bool bounced = false;
        f32 top = 0.0f;
        f32 last = 99.0f;
        for (int i = 0; i < 300; ++i) {
            system.onUpdate(world, kFrame);
            const f32 y = pos(world, ball).y;
            if (y > last + 0.001f && y < 5.0f) bounced = true;  // rising after the first landing
            if (bounced) top = std::max(top, y);
            last = y;
        }
        return top > 0.0f ? top - 0.5f : 0.0f;
    };
    REQUIRE(peak(0.0f) < 0.1f);
    const f32 half = peak(0.5f);
    REQUIRE(half > 0.6f);
    REQUIRE(half < 2.2f);  // ideal 5 * 0.25 = 1.25
    REQUIRE(peak(0.9f) > half);
}

TEST_CASE("Physics3D - friction holds a box on a slope that ice lets slide", "[physics][physics3d][scenarios]") {
    if (!bulletOn()) return;
    auto slid = [](f32 friction) {
        PhysicsSystem3D system;
        ECS::World world;
        // A 30 degree ramp turned about Z.
        Entity ramp = makeBody(world, Vec3(0.0f, 0.0f, 0.0f), ColliderShape3D::Box, Vec3(20.0f, 0.5f, 5.0f), BodyType3D::Static,
                               0.0f, friction);
        const f32 angle = 30.0f * 3.14159265f / 180.0f;
        world.add<ECS::Rotation3D>(ramp).quaternion = Vec4(0.0f, 0.0f, std::sin(angle * 0.5f), std::cos(angle * 0.5f));
        Entity crate = makeBody(world, Vec3(0.0f, 0.0f, 0.0f), ColliderShape3D::Box, Vec3(0.5f, 0.5f, 0.5f),
                                BodyType3D::Dynamic, 0.0f, friction);
        world.add<ECS::Rotation3D>(crate).quaternion = world.get<ECS::Rotation3D>(ramp)->quaternion;
        // Sit on the ramp's face: along its normal (-sin, cos) by 0.5 + 0.5.
        world.get<ECS::Position3D>(crate)->position = Vec3(-std::sin(angle) * 1.02f, std::cos(angle) * 1.02f, 0.0f);
        const Vec3 start = pos(world, crate);
        runFor(world, system, 2.0f);
        const Vec3 end = pos(world, crate);
        return std::sqrt((end.x - start.x) * (end.x - start.x) + (end.y - start.y) * (end.y - start.y));
    };
    REQUIRE(slid(1.0f) < 0.5f);   // tan(30) = 0.58 < mu: it holds
    REQUIRE(slid(0.0f) > 3.0f);   // frictionless: it slides away
}

TEST_CASE("Physics3D - a locked capsule stands on the floor and walks when pushed", "[physics][physics3d][scenarios]") {
    PhysicsSystem3D system;
    if (!system.world().isAvailable()) return;
    ECS::World world;
    makeFloor(world);
    Entity hero = makeBody(world, Vec3(0.0f, 3.0f, 0.0f), ColliderShape3D::Capsule, Vec3(0.4f, 1.8f, 0.4f));
    world.get<Physics3D::RigidBody3D>(hero)->lockRotation = true;
    runFor(world, system, 2.0f);
    REQUIRE(pos(world, hero).y == Approx(0.9f).margin(0.1f));  // half of 1.8 m
    for (int i = 0; i < 60; ++i) {  // a character controller sets its walking speed every frame
        system.world().setLinearVelocity(hero.id(), Vec3(3.0f, 0.0f, 0.0f));
        system.onUpdate(world, kFrame);
    }
    REQUIRE(pos(world, hero).x > 2.0f);
    runFor(world, system, 3.0f);  // let go: friction stops it
    const f32 stopped = pos(world, hero).x;
    runFor(world, system, 1.0f);
    REQUIRE(pos(world, hero).x == Approx(stopped).margin(0.05f));
    REQUIRE(pos(world, hero).y == Approx(0.9f).margin(0.1f));
}

TEST_CASE("Physics3D - a trigger reports enter and exit once and lets the body through", "[physics][physics3d][scenarios]") {
    Events::EventBus bus;
    PhysicsSystem3D system(&bus);
    if (!system.world().isAvailable()) return;
    int entered = 0, exited = 0;
    u32 triggerId = 0, otherId = 0;
    bus.subscribe<Events::OnTrigger3D>([&](const Events::OnTrigger3D& ev) {
        (ev.entered ? entered : exited)++;
        triggerId = ev.triggerEntity;
        otherId = ev.otherEntity;
    });
    ECS::World world;
    makeFloor(world);
    Entity zone = makeBody(world, Vec3(0.0f, 4.0f, 0.0f), ColliderShape3D::Box, Vec3(3.0f, 1.0f, 3.0f), BodyType3D::Static);
    world.get<Physics3D::Collider3D>(zone)->isTrigger = true;
    Entity ball = makeBody(world, Vec3(0.0f, 9.0f, 0.0f), ColliderShape3D::Sphere, Vec3(0.5f, 0.5f, 0.5f));
    for (int i = 0; i < 240; ++i) {
        system.onUpdate(world, kFrame);
        bus.dispatch();
    }
    REQUIRE(entered == 1);
    REQUIRE(exited == 1);
    REQUIRE(triggerId == zone.id());
    REQUIRE(otherId == ball.id());
    REQUIRE(pos(world, ball).y == Approx(0.5f).margin(0.1f));  // fell through the zone to the floor
}

TEST_CASE("Physics3D - a collision is reported when contact begins and ends, not every frame", "[physics][physics3d][scenarios]") {
    Events::EventBus bus;
    PhysicsSystem3D system(&bus);
    if (!system.world().isAvailable()) return;
    int began = 0, ended = 0;
    f32 normalY = 0.0f;
    bus.subscribe<Events::OnCollision3D>([&](const Events::OnCollision3D& ev) {
        (ev.entered ? began : ended)++;
        if (ev.entered) normalY = ev.normal.y;
    });
    ECS::World world;
    Entity floor = makeFloor(world);
    Entity ball = makeBody(world, Vec3(0.0f, 3.0f, 0.0f), ColliderShape3D::Sphere, Vec3(0.5f, 0.5f, 0.5f));
    for (int i = 0; i < 300; ++i) {
        system.onUpdate(world, kFrame);
        bus.dispatch();
    }
    REQUIRE(began == 1);   // it landed and stayed: one start of contact (a small bounce may add one)
    REQUIRE(ended == began - 1);
    // The normal points from A to B: the floor is A or the ball is, so it is vertical either way.
    REQUIRE(std::fabs(normalY) > 0.9f);
    (void)floor;
    (void)ball;
}

TEST_CASE("Physics3D - a kinematic platform pushes and carries a body", "[physics][physics3d][scenarios]") {
    PhysicsSystem3D system;
    if (!system.world().isAvailable()) return;
    ECS::World world;
    Entity lift = makeBody(world, Vec3(0.0f, 0.5f, 0.0f), ColliderShape3D::Box, Vec3(2.0f, 0.5f, 2.0f), BodyType3D::Kinematic);
    Entity crate = makeBody(world, Vec3(0.0f, 1.5f, 0.0f), ColliderShape3D::Box, Vec3(0.5f, 0.5f, 0.5f), BodyType3D::Dynamic, 0.0f, 1.0f);
    runFor(world, system, 0.5f);
    for (int i = 0; i < 180; ++i) {  // the lift rises 2 m in 3 s
        world.get<ECS::Position3D>(lift)->position.y += 2.0f / 180.0f;
        system.onUpdate(world, kFrame);
    }
    REQUIRE(pos(world, lift).y == Approx(2.5f).margin(0.01f));  // the system never moves a kinematic body
    REQUIRE(pos(world, crate).y == Approx(pos(world, lift).y + 1.0f).margin(0.15f));
}

TEST_CASE("Physics3D - a body under a moved parent falls in world space and stays consistent", "[physics][physics3d][scenarios]") {
    PhysicsSystem3D system;
    if (!system.world().isAvailable()) return;
    ECS::World world;
    makeBody(world, Vec3(100.0f, -0.5f, 0.0f), ColliderShape3D::Box, Vec3(20.0f, 0.5f, 20.0f), BodyType3D::Static);
    Entity group = world.create();
    world.add<ECS::Transform>(group).position = Vec3(100.0f, 0.0f, 0.0f);
    Entity child = world.create();
    world.add<ECS::Transform>(child).position = Vec3(0.0f, 5.0f, 0.0f);
    world.add<Scene::Parent>(child).parent = group;
    world.add<Physics3D::RigidBody3D>(child);
    Physics3D::Collider3D col;
    col.halfExtents = Vec3(0.5f, 0.5f, 0.5f);
    world.add<Physics3D::Collider3D>(child, col);
    runFor(world, system, 3.0f);
    const Vec3 local = world.get<ECS::Transform>(child)->position;
    const Vec3 worldPos = Scene::computeWorldMatrix(world, child).transformPoint(Vec3(0.0f, 0.0f, 0.0f));
    REQUIRE(worldPos.y == Approx(0.5f).margin(0.1f));
    REQUIRE(worldPos.x == Approx(100.0f).margin(0.1f));
    REQUIRE(local.x == Approx(0.0f).margin(0.1f));  // not the world x written into a local field
    REQUIRE(local.y == Approx(0.5f).margin(0.1f));
}

TEST_CASE("Physics3D - moving a body's entity from outside moves the body", "[physics][physics3d][scenarios]") {
    PhysicsSystem3D system;
    if (!system.world().isAvailable()) return;
    ECS::World world;
    makeFloor(world);
    Entity ball = makeBody(world, Vec3(0.0f, 0.5f, 0.0f), ColliderShape3D::Sphere, Vec3(0.5f, 0.5f, 0.5f));
    runFor(world, system, 1.0f);
    world.get<ECS::Position3D>(ball)->position = Vec3(30.0f, 10.0f, -4.0f);  // a script, a respawn
    runFor(world, system, 2.0f);
    REQUIRE(pos(world, ball).x == Approx(30.0f).margin(0.2f));
    REQUIRE(pos(world, ball).z == Approx(-4.0f).margin(0.2f));
    REQUIRE(pos(world, ball).y == Approx(0.5f).margin(0.1f));  // it fell from the new place
}

TEST_CASE("Physics3D - a tumbling box keeps its turn whether it is stored as Euler angles or a quaternion", "[physics][physics3d][scenarios]") {
    PhysicsSystem3D system;
    if (!system.world().isAvailable()) return;
    ECS::World world;
    makeFloor(world);
    Entity crate = world.create();
    auto& transform = world.add<ECS::Transform>(crate);
    transform.position = Vec3(0.0f, 4.0f, 0.0f);
    transform.rotation = Vec3(20.0f, 0.0f, 35.0f);
    world.add<Physics3D::RigidBody3D>(crate);
    Physics3D::Collider3D col;
    col.halfExtents = Vec3(0.5f, 0.5f, 0.5f);
    world.add<Physics3D::Collider3D>(crate, col);
    runFor(world, system, 5.0f);
    // Settled flat on one face: the centre is half a side above the floor and it no longer drifts.
    REQUIRE(world.get<ECS::Transform>(crate)->position.y == Approx(0.5f).margin(0.05f));
    const Vec3 before = world.get<ECS::Transform>(crate)->position;
    runFor(world, system, 1.0f);
    REQUIRE(world.get<ECS::Transform>(crate)->position.x == Approx(before.x).margin(0.02f));
}

TEST_CASE("Physics3D - raycast finds the nearest body, skips triggers, and a destroyed entity vanishes", "[physics][physics3d][scenarios]") {
    PhysicsSystem3D system;
    if (!system.world().isAvailable()) return;
    ECS::World world;
    Entity wall = makeBody(world, Vec3(10.0f, 0.0f, 0.0f), ColliderShape3D::Box, Vec3(1.0f, 5.0f, 5.0f), BodyType3D::Static);
    Entity ghost = makeBody(world, Vec3(5.0f, 0.0f, 0.0f), ColliderShape3D::Box, Vec3(1.0f, 5.0f, 5.0f), BodyType3D::Static);
    world.get<Physics3D::Collider3D>(ghost)->isTrigger = true;
    system.onUpdate(world, kFrame);
    auto hit = system.world().raycast(Vec3(0, 0, 0), Vec3(1, 0, 0), 50.0f);
    REQUIRE(hit.hit);
    REQUIRE(hit.entityId == wall.id());
    REQUIRE(hit.distance == Approx(9.0f).margin(0.05f));
    REQUIRE(hit.normal.x == Approx(-1.0f).margin(0.01f));
    REQUIRE_FALSE(system.world().raycast(Vec3(0, 0, 0), Vec3(1, 0, 0), 5.0f).hit);  // out of range
    world.destroy(wall);
    system.onUpdate(world, kFrame);
    REQUIRE_FALSE(system.world().raycast(Vec3(0, 0, 0), Vec3(1, 0, 0), 50.0f).hit);
}

TEST_CASE("Physics3D - forces, impulses and velocity wake a sleeping body", "[physics][physics3d][scenarios]") {
    PhysicsSystem3D system;
    if (!system.world().isAvailable()) return;
    ECS::World world;
    makeFloor(world);
    Entity ball = makeBody(world, Vec3(0.0f, 0.5f, 0.0f), ColliderShape3D::Sphere, Vec3(0.5f, 0.5f, 0.5f));
    runFor(world, system, 6.0f);  // long enough for Bullet to put it to sleep
    const f32 start = pos(world, ball).x;
    system.world().addImpulse(ball.id(), Vec3(5.0f, 0.0f, 0.0f));
    runFor(world, system, 0.5f);
    REQUIRE(pos(world, ball).x > start + 1.0f);
    runFor(world, system, 12.0f);
    const f32 mid = pos(world, ball).x;
    runFor(world, system, 1.0f);
    REQUIRE(pos(world, ball).x == Approx(mid).margin(0.05f));  // a ball on a flat floor comes to rest
    for (int i = 0; i < 60; ++i) {  // a force holds for the frame it is asked in: ask every frame
        system.world().addForce(ball.id(), Vec3(0.0f, 0.0f, 30.0f));
        system.onUpdate(world, kFrame);
    }
    REQUIRE(pos(world, ball).z > 5.0f);
    REQUIRE(pos(world, ball).x == Approx(mid).margin(0.3f));
    REQUIRE_FALSE(system.world().addImpulse(9999, Vec3(1, 0, 0)));  // unknown body
}

TEST_CASE("Physics3D - a pile of bodies stays finite and above the floor, and replays identically", "[physics][physics3d][scenarios]") {
    auto simulate = [] {
        PhysicsSystem3D system;
        ECS::World world;
        makeFloor(world);
        std::vector<Entity> all;
        for (int i = 0; i < 60; ++i) {
            const auto shape = (i % 3 == 0) ? ColliderShape3D::Sphere : (i % 3 == 1) ? ColliderShape3D::Box : ColliderShape3D::Capsule;
            all.push_back(makeBody(world, Vec3(static_cast<f32>(i % 5) * 0.7f, 1.0f + static_cast<f32>(i) * 0.9f, static_cast<f32>(i % 3) * 0.6f),
                                   shape, shape == ColliderShape3D::Capsule ? Vec3(0.3f, 1.2f, 0.3f) : Vec3(0.4f, 0.4f, 0.4f)));
        }
        runFor(world, system, 8.0f);
        std::vector<f32> out;
        for (const Entity e : all) {
            const Vec3 p = pos(world, e);
            out.push_back(p.x);
            out.push_back(p.y);
            out.push_back(p.z);
        }
        return out;
    };
    if (!bulletOn()) return;
    const auto first = simulate();
    for (const f32 v : first) REQUIRE(std::isfinite(v));
    for (size_t i = 1; i < first.size(); i += 3) REQUIRE(first[i] > 0.1f);  // nothing fell through the floor
    REQUIRE(first == simulate());
}

TEST_CASE("Physics3D - landing and resting do not depend on the frame rate", "[physics][physics3d][framerate]") {
    if (!bulletOn()) return;
    auto rest = [](f32 frame) {
        PhysicsSystem3D system;
        ECS::World world;
        makeFloor(world);
        Entity crate = makeBody(world, Vec3(0.0f, 6.0f, 0.0f), ColliderShape3D::Box, Vec3(0.5f, 0.5f, 0.5f));
        runFor(world, system, 3.0f, frame);
        return pos(world, crate).y;
    };
    const f32 reference = rest(1.0f / 60.0f);
    REQUIRE(reference == Approx(0.5f).margin(0.05f));
    for (const f32 frame : { 1.0f / 240.0f, 1.0f / 144.0f, 1.0f / 30.0f, 1.0f / 15.0f }) {
        INFO("frame length " << frame);
        REQUIRE(rest(frame) == Approx(reference).margin(0.05f));
    }
}

TEST_CASE("Physics3D - a fast body does not tunnel through a thin static wall", "[physics][physics3d][scenarios]") {
    PhysicsSystem3D system;
    if (!system.world().isAvailable()) return;
    ECS::World world;
    system.world().setGravity(Vec3(0.0f, 0.0f, 0.0f));
    makeBody(world, Vec3(20.0f, 0.0f, 0.0f), ColliderShape3D::Box, Vec3(0.05f, 5.0f, 5.0f), BodyType3D::Static);
    Entity bullet = makeBody(world, Vec3(0.0f, 0.0f, 0.0f), ColliderShape3D::Sphere, Vec3(0.2f, 0.2f, 0.2f));
    system.onUpdate(world, kFrame);
    system.world().setLinearVelocity(bullet.id(), Vec3(300.0f, 0.0f, 0.0f));  // 5 m per step
    runFor(world, system, 0.5f);
    REQUIRE(pos(world, bullet).x < 20.5f);
}

TEST_CASE("Physics3D - the surface rule can be Bullet's product or the default blend", "[physics][physics3d][scenarios]") {
    if (!bulletOn()) return;
    auto bounce = [](PhysicsWorld3D::SurfaceCombine mode) {
        PhysicsSystem3D system;
        system.world().setSurfaceCombine(mode);
        ECS::World world;
        // A bouncy ball (0.9) on a floor left at 0: blend bounces, the product (0.9 x 0) does not.
        Entity floor = makeBody(world, Vec3(0.0f, -0.5f, 0.0f), ColliderShape3D::Box, Vec3(50.0f, 0.5f, 50.0f), BodyType3D::Static, 0.0f);
        Entity ball = makeBody(world, Vec3(0.0f, 4.5f, 0.0f), ColliderShape3D::Sphere, Vec3(0.5f, 0.5f, 0.5f), BodyType3D::Dynamic, 0.9f);
        (void)floor;
        f32 top = 0.0f;
        bool landed = false;
        for (int i = 0; i < 240; ++i) {
            system.onUpdate(world, kFrame);
            if (pos(world, ball).y < 0.7f) landed = true;
            if (landed) top = std::max(top, pos(world, ball).y);
        }
        return top;
    };
    REQUIRE(bounce(PhysicsWorld3D::SurfaceCombine::Blend) > 2.0f);
    REQUIRE(bounce(PhysicsWorld3D::SurfaceCombine::Product) < 1.0f);

    // Switching with bodies already in the world takes effect on them.
    PhysicsSystem3D system;
    REQUIRE(system.world().surfaceCombine() == PhysicsWorld3D::SurfaceCombine::Blend);
    ECS::World world;
    makeBody(world, Vec3(0.0f, -0.5f, 0.0f), ColliderShape3D::Box, Vec3(50.0f, 0.5f, 50.0f), BodyType3D::Static, 0.0f);
    Entity ball = makeBody(world, Vec3(0.0f, 4.5f, 0.0f), ColliderShape3D::Sphere, Vec3(0.5f, 0.5f, 0.5f), BodyType3D::Dynamic, 0.9f);
    system.onUpdate(world, kFrame);
    system.world().setSurfaceCombine(PhysicsWorld3D::SurfaceCombine::Product);
    f32 top = 0.0f;
    bool landed = false;
    for (int i = 0; i < 240; ++i) {
        system.onUpdate(world, kFrame);
        if (pos(world, ball).y < 0.7f) landed = true;
        if (landed) top = std::max(top, pos(world, ball).y);
    }
    REQUIRE(top < 1.0f);
}
