#include "catch.hpp"
#include "../src/Caffeine.hpp"
#include "../src/scene/SceneComponents.hpp"
#include "../src/scene/SceneSerializer.hpp"
#include "../src/scene/SceneManager.hpp"

#include <cmath>
#include <cstring>
#include <vector>

using namespace Caffeine;
using namespace Caffeine::ECS;
using namespace Caffeine::Scene;

static constexpr f32 kEps = 0.001f;

static bool approxEq(f32 a, f32 b) {
    return fabsf(a - b) < kEps;
}

TEST_CASE("Parent - default construction", "[scene]") {
    Parent p{};
    REQUIRE_FALSE(p.parent.isValid());
    REQUIRE(p.dirty == true);
}

TEST_CASE("WorldTransform - default is identity", "[scene]") {
    WorldTransform wt{};
    Mat4 identity = Mat4::identity();
    const f32* a = wt.matrix.data();
    const f32* b = identity.data();
    for (int i = 0; i < 16; ++i) {
        REQUIRE(approxEq(a[i], b[i]));
    }
}

TEST_CASE("SceneSerializer - serialize empty world to memory returns true", "[scene]") {
    World world;
    SceneSerializer s(world);
    std::vector<u8> bytes;
    REQUIRE(s.serializeToMemory(bytes));
    REQUIRE(bytes.size() >= sizeof(CafHeader));
}

TEST_CASE("SceneSerializer - memory output has valid CafHeader magic", "[scene]") {
    World world;
    SceneSerializer s(world);
    std::vector<u8> bytes;
    s.serializeToMemory(bytes);
    CafHeader hdr{};
    memcpy(&hdr, bytes.data(), sizeof(hdr));
    REQUIRE(hdr.magic == CafHeader::kMagic);
    REQUIRE(hdr.type  == AssetType::Scene);
}

TEST_CASE("SceneSerializer - round-trip Position2D", "[scene]") {
    World world;
    Entity e = world.create();
    world.add<Position2D>(e);
    world.get<Position2D>(e)->x = 3.0f;
    world.get<Position2D>(e)->y = 7.0f;

    std::vector<u8> bytes;
    SceneSerializer(world).serializeToMemory(bytes);

    World world2;
    REQUIRE(SceneSerializer(world2).deserializeFromMemory(bytes));

    bool found = false;
    ComponentQuery q; q.with<Position2D>();
    world2.forEach<Position2D>(q, [&](Entity, Position2D& p) {
        REQUIRE(approxEq(p.x, 3.0f));
        REQUIRE(approxEq(p.y, 7.0f));
        found = true;
    });
    REQUIRE(found);
}

TEST_CASE("SceneSerializer - round-trip Health", "[scene]") {
    World world;
    Entity e = world.create();
    world.add<Health>(e);
    world.get<Health>(e)->current = 50;
    world.get<Health>(e)->max     = 200;

    std::vector<u8> bytes;
    SceneSerializer(world).serializeToMemory(bytes);

    World world2;
    SceneSerializer(world2).deserializeFromMemory(bytes);

    bool found = false;
    ComponentQuery q; q.with<Health>();
    world2.forEach<Health>(q, [&](Entity, Health& h) {
        REQUIRE(h.current == 50u);
        REQUIRE(h.max     == 200u);
        found = true;
    });
    REQUIRE(found);
}

TEST_CASE("SceneSerializer - round-trip entity count", "[scene]") {
    World world;
    for (int i = 0; i < 5; ++i) {
        Entity e = world.create();
        world.add<Position2D>(e);
    }

    std::vector<u8> bytes;
    SceneSerializer(world).serializeToMemory(bytes);

    World world2;
    SceneSerializer(world2).deserializeFromMemory(bytes);

    REQUIRE(world2.entityCount() == 5u);
}

TEST_CASE("SceneSerializer - round-trip multiple component types on same entity", "[scene]") {
    World world;
    Entity e = world.create();
    world.add<Position2D>(e);
    world.get<Position2D>(e)->x = 1.0f;
    world.get<Position2D>(e)->y = 2.0f;
    world.add<Health>(e);
    world.get<Health>(e)->current = 75;
    world.get<Health>(e)->max     = 100;

    std::vector<u8> bytes;
    SceneSerializer(world).serializeToMemory(bytes);

    World world2;
    SceneSerializer(world2).deserializeFromMemory(bytes);

    bool found = false;
    ComponentQuery q; q.with<Position2D, Health>();
    world2.forEach<Position2D, Health>(q, [&](Entity, Position2D& p, Health& h) {
        REQUIRE(approxEq(p.x, 1.0f));
        REQUIRE(h.current == 75u);
        found = true;
    });
    REQUIRE(found);
}

TEST_CASE("SceneSerializer - round-trip WorldTransform", "[scene]") {
    World world;
    Entity e = world.create();
    world.add<WorldTransform>(e);
    world.get<WorldTransform>(e)->matrix = Mat4::translation(5.0f, 10.0f, 0.0f);

    std::vector<u8> bytes;
    SceneSerializer(world).serializeToMemory(bytes);

    World world2;
    SceneSerializer(world2).deserializeFromMemory(bytes);

    bool found = false;
    ComponentQuery q; q.with<WorldTransform>();
    world2.forEach<WorldTransform>(q, [&](Entity, WorldTransform& wt) {
        REQUIRE(approxEq(wt.matrix.data()[12], 5.0f));
        REQUIRE(approxEq(wt.matrix.data()[13], 10.0f));
        found = true;
    });
    REQUIRE(found);
}

TEST_CASE("SceneSerializer - round-trip Parent dirty flag", "[scene]") {
    World world;
    Entity parent = world.create();
    Entity child  = world.create();
    world.add<Parent>(child);
    world.get<Parent>(child)->parent = parent;
    world.get<Parent>(child)->dirty  = false;

    std::vector<u8> bytes;
    SceneSerializer(world).serializeToMemory(bytes);

    World world2;
    SceneSerializer(world2).deserializeFromMemory(bytes);

    bool found = false;
    ComponentQuery q; q.with<Parent>();
    world2.forEach<Parent>(q, [&](Entity, Parent& p) {
        REQUIRE(p.dirty == false);
        REQUIRE(p.parent.isValid());
        found = true;
    });
    REQUIRE(found);
}

TEST_CASE("SceneSerializer - deserializeFromMemory empty data returns false", "[scene]") {
    World world;
    std::vector<u8> empty;
    REQUIRE_FALSE(SceneSerializer(world).deserializeFromMemory(empty));
}

TEST_CASE("SceneSerializer - deserializeFromMemory bad magic returns false", "[scene]") {
    World world;
    std::vector<u8> garbage(64, 0xAB);
    REQUIRE_FALSE(SceneSerializer(world).deserializeFromMemory(garbage));
}

TEST_CASE("SceneManager - construct, activeWorld is nullptr", "[scene]") {
    SceneManager mgr;
    REQUIRE(mgr.activeWorld() == nullptr);
}

TEST_CASE("SceneManager - pushWorld, activeWorld returns correct pointer", "[scene]") {
    SceneManager mgr;
    auto world = std::make_unique<World>();
    World* raw = world.get();
    mgr.pushWorld(std::move(world));
    REQUIRE(mgr.activeWorld() == raw);
}

TEST_CASE("SceneManager - popScene after pushWorld, activeWorld is nullptr", "[scene]") {
    SceneManager mgr;
    mgr.pushWorld(std::make_unique<World>());
    mgr.popScene();
    REQUIRE(mgr.activeWorld() == nullptr);
}

TEST_CASE("SceneManager - pushWorld twice, popScene returns first world", "[scene]") {
    SceneManager mgr;
    auto world1 = std::make_unique<World>();
    World* raw1 = world1.get();
    mgr.pushWorld(std::move(world1));
    mgr.pushWorld(std::make_unique<World>());
    mgr.popScene();
    REQUIRE(mgr.activeWorld() == raw1);
}

TEST_CASE("SceneManager - isTransitioning initially false", "[scene]") {
    SceneManager mgr;
    REQUIRE_FALSE(mgr.isTransitioning());
}

TEST_CASE("SceneManager - update with dt=0 does not crash", "[scene]") {
    SceneManager mgr;
    mgr.pushWorld(std::make_unique<World>());
    mgr.update(0.0);
    REQUIRE(mgr.activeWorld() != nullptr);
}

TEST_CASE("WorldTransform - 3 levels of hierarchy propagated correctly", "[scene]") {
    World world;

    Entity root  = world.create();
    Entity child = world.create();
    Entity grand = world.create();

    world.add<Position2D>(root);   world.get<Position2D>(root)->x  = 10.0f;
    world.add<WorldTransform>(root);

    world.add<Position2D>(child);  world.get<Position2D>(child)->x = 5.0f;
    world.add<WorldTransform>(child);
    world.add<Parent>(child);      world.get<Parent>(child)->parent = root;

    world.add<Position2D>(grand);  world.get<Position2D>(grand)->x = 2.0f;
    world.add<WorldTransform>(grand);
    world.add<Parent>(grand);      world.get<Parent>(grand)->parent = child;

    auto propagate = [&](Entity e) {
        Position2D*    pos = world.get<Position2D>(e);
        WorldTransform* wt = world.get<WorldTransform>(e);
        Parent*        par = world.get<Parent>(e);
        if (!pos || !wt) return;
        Mat4 local = Mat4::translation(pos->x, pos->y, 0.0f);
        if (par && par->parent.isValid()) {
            WorldTransform* pwt = world.get<WorldTransform>(par->parent);
            wt->matrix = pwt ? pwt->matrix * local : local;
        } else {
            wt->matrix = local;
        }
    };

    propagate(root);
    propagate(child);
    propagate(grand);

    WorldTransform* grandWT = world.get<WorldTransform>(grand);
    REQUIRE(grandWT != nullptr);
    REQUIRE(approxEq(grandWT->matrix.data()[12], 17.0f));
    REQUIRE(approxEq(grandWT->matrix.data()[13], 0.0f));
}
