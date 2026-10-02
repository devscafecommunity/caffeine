#include "catch.hpp"
#include "physics/PhysicsWorld3D.hpp"
#include "physics/PhysicsSystem3D.hpp"

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
