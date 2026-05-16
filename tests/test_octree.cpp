#include "catch.hpp"
#include "../src/Caffeine.hpp"

#include <cmath>

using namespace Caffeine;
using namespace Caffeine::Spatial;
using namespace Caffeine::ECS;

static constexpr f32 kEps = 0.001f;

// ============================================================================
// AABB3D Tests
// ============================================================================

TEST_CASE("AABB3D - center and size", "[spatial][aabb]") {
    AABB3D box{{0, 0, 0}, {4, 6, 8}};
    Vec3 c = box.center();
    REQUIRE(c.x == Approx(2.0f));
    REQUIRE(c.y == Approx(3.0f));
    REQUIRE(c.z == Approx(4.0f));

    Vec3 s = box.size();
    REQUIRE(s.x == Approx(4.0f));
    REQUIRE(s.y == Approx(6.0f));
    REQUIRE(s.z == Approx(8.0f));
}

TEST_CASE("AABB3D - contains point", "[spatial][aabb]") {
    AABB3D box{{-2, -2, -2}, {2, 2, 2}};
    REQUIRE(box.contains({0, 0, 0}));
    REQUIRE(box.contains({2, 2, 2}));
    REQUIRE(box.contains({-2, -2, -2}));
    REQUIRE_FALSE(box.contains({3, 0, 0}));
    REQUIRE_FALSE(box.contains({0, 5, 0}));
    REQUIRE_FALSE(box.contains({0, 0, -3}));
}

TEST_CASE("AABB3D - contains AABB", "[spatial][aabb]") {
    AABB3D outer{{-5, -5, -5}, {5, 5, 5}};
    AABB3D inner{{-1, -1, -1}, {1, 1, 1}};
    REQUIRE(outer.contains(inner));
    REQUIRE_FALSE(inner.contains(outer));
}

TEST_CASE("AABB3D - intersects", "[spatial][aabb]") {
    AABB3D a{{0, 0, 0}, {4, 4, 4}};
    AABB3D overlapping{{2, 2, 2}, {6, 6, 6}};
    AABB3D separate{{10, 10, 10}, {14, 14, 14}};
    AABB3D touching{{4, 0, 0}, {8, 4, 4}};

    REQUIRE(a.intersects(overlapping));
    REQUIRE_FALSE(a.intersects(separate));
    REQUIRE(a.intersects(touching));
}

TEST_CASE("AABB3D - intersectsRay", "[spatial][aabb]") {
    AABB3D box{{-1, -1, -1}, {1, 1, 1}};

    f32 t = -1.0f;
    REQUIRE(box.intersectsRay({-5, 0, 0}, {1, 0, 0}, &t));
    REQUIRE(t == Approx(4.0f));

    REQUIRE_FALSE(box.intersectsRay({-5, 10, 0}, {1, 0, 0}));

    REQUIRE(box.intersectsRay({0, 0, 0}, {1, 0, 0}, &t));
    REQUIRE(t == Approx(0.0f));
}

// ============================================================================
// Frustum Tests
// ============================================================================

TEST_CASE("Frustum - contains point", "[spatial][frustum]") {
    f32 aspect = 16.0f / 9.0f;
    Frustum f = Frustum::fromCamera(
        {0, 0, -5}, {0, 0, 0}, {0, 1, 0},
        Math::degToRad(60.0f), aspect, 0.1f, 100.0f
    );

    REQUIRE(f.contains({0, 0, 0}));
    REQUIRE(f.contains({0, 0, -2}));
    REQUIRE(f.contains({0, 0, 10}));
    REQUIRE_FALSE(f.contains({0, 0, 200}));
    REQUIRE_FALSE(f.contains({100, 0, 0}));
}

TEST_CASE("Frustum - intersects AABB", "[spatial][frustum]") {
    f32 aspect = 16.0f / 9.0f;
    Frustum f = Frustum::fromCamera(
        {0, 0, -5}, {0, 0, 0}, {0, 1, 0},
        Math::degToRad(60.0f), aspect, 0.1f, 100.0f
    );

    REQUIRE(f.intersects(AABB3D{{-1, -1, -1}, {1, 1, 1}}));
    REQUIRE_FALSE(f.intersects(AABB3D{{-1, -1, 150}, {1, 1, 200}}));
    REQUIRE(f.intersects(AABB3D{{-200, -200, -2}, {200, 200, 2}}));
}

TEST_CASE("Frustum - containsSphere", "[spatial][frustum]") {
    f32 aspect = 16.0f / 9.0f;
    Frustum f = Frustum::fromCamera(
        {0, 0, -5}, {0, 0, 0}, {0, 1, 0},
        Math::degToRad(60.0f), aspect, 0.1f, 100.0f
    );

    REQUIRE(f.containsSphere({0, 0, 0}, 1.0f));
    REQUIRE_FALSE(f.containsSphere({0, 0, 200}, 10.0f));
}

// ============================================================================
// Octree Tests
// ============================================================================

TEST_CASE("Octree - construction and empty queries", "[spatial][octree]") {
    Octree octree({{{-100, -100, -100}, {100, 100, 100}}});
    REQUIRE(octree.nodeCount() == 1);
    REQUIRE(octree.entityCount() == 0);
    REQUIRE(octree.depth() == 0);

    std::vector<ECS::Entity> result;
    octree.queryAABB({{-10, -10, -10}, {10, 10, 10}}, result);
    REQUIRE(result.empty());

    octree.queryFrustum(Frustum{}, result);
    REQUIRE(result.empty());

    octree.queryRay({0, 0, 0}, {1, 0, 0}, 100.0f, result);
    REQUIRE(result.empty());

    octree.queryRadius({0, 0, 0}, 10.0f, result);
    REQUIRE(result.empty());
}

TEST_CASE("Octree - insert and entityCount", "[spatial][octree]") {
    Octree octree({{{-100, -100, -100}, {100, 100, 100}}, 4, 8});

    ECS::Entity e1(1, nullptr);
    ECS::Entity e2(2, nullptr);
    ECS::Entity e3(3, nullptr);

    octree.insert(e1, {{-10, -10, -10}, {10, 10, 10}});
    REQUIRE(octree.entityCount() == 1);

    octree.insert(e2, {{20, 20, 20}, {30, 30, 30}});
    REQUIRE(octree.entityCount() == 2);

    octree.insert(e3, {{-50, -50, -50}, {-40, -40, -40}});
    REQUIRE(octree.entityCount() == 3);
}

TEST_CASE("Octree - subdivision", "[spatial][octree]") {
    Octree octree({{{-100, -100, -100}, {100, 100, 100}}, 4, 4});

    std::vector<ECS::Entity> entities;
    for (u32 i = 0; i < 20; ++i) {
        entities.emplace_back(i, nullptr);
        f32 x = (f32)(i * 10) - 50.0f;
        octree.insert(entities.back(), {{x - 2, -2, -2}, {x + 2, 2, 2}});
    }

    REQUIRE(octree.entityCount() == 20);
    REQUIRE(octree.nodeCount() > 1);
    REQUIRE(octree.depth() >= 1);
}

TEST_CASE("Octree - remove", "[spatial][octree]") {
    Octree octree({{{-100, -100, -100}, {100, 100, 100}}, 4, 8});

    ECS::Entity e1(1, nullptr);
    ECS::Entity e2(2, nullptr);
    octree.insert(e1, {{-5, -5, -5}, {5, 5, 5}});
    octree.insert(e2, {{10, 10, 10}, {20, 20, 20}});
    REQUIRE(octree.entityCount() == 2);

    octree.remove(e1);
    REQUIRE(octree.entityCount() == 1);

    octree.remove(ECS::Entity(999, nullptr));
    REQUIRE(octree.entityCount() == 1);
}

TEST_CASE("Octree - update", "[spatial][octree]") {
    Octree octree({{{-100, -100, -100}, {100, 100, 100}}, 4, 8});

    ECS::Entity e(1, nullptr);
    octree.insert(e, {{-10, -10, -10}, {10, 10, 10}});
    REQUIRE(octree.entityCount() == 1);

    octree.update(e, {{50, 50, 50}, {60, 60, 60}});
    REQUIRE(octree.entityCount() == 1);

    std::vector<ECS::Entity> result;
    octree.queryAABB({{-15, -15, -15}, {15, 15, 15}}, result);
    REQUIRE(result.empty());

    octree.queryAABB({{45, 45, 45}, {65, 65, 65}}, result);
    REQUIRE(result.size() == 1);
}

TEST_CASE("Octree - queryAABB", "[spatial][octree]") {
    Octree octree({{{-100, -100, -100}, {100, 100, 100}}, 4, 8});

    ECS::Entity e1(1, nullptr);
    ECS::Entity e2(2, nullptr);
    octree.insert(e1, {{-2, -2, -2}, {2, 2, 2}});
    octree.insert(e2, {{80, 80, 80}, {90, 90, 90}});

    std::vector<ECS::Entity> result;
    octree.queryAABB({{-5, -5, -5}, {5, 5, 5}}, result);
    REQUIRE(result.size() == 1);
    REQUIRE(result[0] == e1);

    result.clear();
    octree.queryAABB({{70, 70, 70}, {95, 95, 95}}, result);
    REQUIRE(result.size() == 1);
    REQUIRE(result[0] == e2);

    result.clear();
    octree.queryAABB({{-100, -100, -100}, {100, 100, 100}}, result);
    REQUIRE(result.size() == 2);
}

TEST_CASE("Octree - queryAABB with subdivision", "[spatial][octree]") {
    Octree octree({{{-100, -100, -100}, {100, 100, 100}}, 2, 4});

    std::vector<ECS::Entity> entities;
    for (u32 i = 0; i < 8; ++i) {
        entities.emplace_back(i, nullptr);
        f32 off = (f32)(i % 4) * 5.0f;
        octree.insert(entities.back(), {{off, off, off}, {off + 2, off + 2, off + 2}});
    }

    std::vector<ECS::Entity> result;
    octree.queryAABB({{-1, -1, -1}, {3, 3, 3}}, result);
    REQUIRE(result.size() >= 1);
    REQUIRE(octree.entityCount() == 8);
    REQUIRE(octree.nodeCount() > 1);
}

TEST_CASE("Octree - queryFrustum", "[spatial][octree]") {
    Octree octree({{{-100, -100, -100}, {100, 100, 100}}, 4, 8});

    ECS::Entity e1(1, nullptr);
    ECS::Entity e2(2, nullptr);
    octree.insert(e1, {{-1, -1, -1}, {1, 1, 1}});
    octree.insert(e2, {{-50, -50, 50}, {-40, -40, 60}});

    f32 aspect = 16.0f / 9.0f;
    Frustum frustum = Frustum::fromCamera(
        {0, 0, -5}, {0, 0, 0}, {0, 1, 0},
        Math::degToRad(60.0f), aspect, 0.1f, 100.0f
    );

    std::vector<ECS::Entity> result;
    octree.queryFrustum(frustum, result);
    REQUIRE(result.size() >= 1);
}

TEST_CASE("Octree - queryRadius", "[spatial][octree]") {
    Octree octree({{{-100, -100, -100}, {100, 100, 100}}, 4, 8});

    ECS::Entity e1(1, nullptr);
    ECS::Entity e2(2, nullptr);
    octree.insert(e1, {{-1, -1, -1}, {1, 1, 1}});
    octree.insert(e2, {{80, 80, 80}, {90, 90, 90}});

    std::vector<ECS::Entity> result;
    octree.queryRadius({0, 0, 0}, 10.0f, result);
    REQUIRE(result.size() == 1);
    REQUIRE(result[0] == e1);

    result.clear();
    octree.queryRadius({0, 0, 0}, 200.0f, result);
    REQUIRE(result.size() == 2);
}

TEST_CASE("Octree - queryRay", "[spatial][octree]") {
    Octree octree({{{-100, -100, -100}, {100, 100, 100}}, 4, 8});

    ECS::Entity e1(1, nullptr);
    octree.insert(e1, {{-1, -1, -1}, {1, 1, 1}});

    std::vector<ECS::Entity> result;
    octree.queryRay({-10, 0, 0}, {1, 0, 0}, 20.0f, result);
    REQUIRE(result.size() == 1);
    REQUIRE(result[0] == e1);

    result.clear();
    octree.queryRay({10, 0, 0}, {1, 0, 0}, 20.0f, result);
    REQUIRE(result.empty());
}

TEST_CASE("Octree - rebuild", "[spatial][octree]") {
    Octree octree({{{-100, -100, -100}, {100, 100, 100}}, 4, 8});

    std::vector<ECS::Entity> entities;
    for (u32 i = 0; i < 10; ++i) {
        entities.emplace_back(i, nullptr);
        octree.insert(entities.back(), {{-1, -1, -1}, {1, 1, 1}});
    }

    REQUIRE(octree.entityCount() == 10);
    u32 nodesBefore = octree.nodeCount();
    (void)nodesBefore;

    octree.rebuild();
    REQUIRE(octree.entityCount() == 10);
    REQUIRE(octree.nodeCount() >= 1);
}

TEST_CASE("Octree - max depth prevents infinite subdivision", "[spatial][octree]") {
    Octree octree({{{-100, -100, -100}, {100, 100, 100}}, 2, 0});

    for (u32 i = 0; i < 100; ++i) {
        ECS::Entity e(i, nullptr);
        octree.insert(e, {{-1, -1, -1}, {1, 1, 1}});
    }

    REQUIRE(octree.nodeCount() == 1);
    REQUIRE(octree.entityCount() == 100);
}

TEST_CASE("Octree - entityCount consistency after insert/remove", "[spatial][octree]") {
    Octree octree({{{-100, -100, -100}, {100, 100, 100}}, 3, 4});

    ECS::Entity entities[20];
    for (u32 i = 0; i < 20; ++i) {
        entities[i] = ECS::Entity(i, nullptr);
        octree.insert(entities[i], {{-50 + (f32)i * 5, -1, -1}, {-48 + (f32)i * 5, 1, 1}});
    }
    REQUIRE(octree.entityCount() == 20);

    for (u32 i = 0; i < 10; ++i) {
        octree.remove(entities[i]);
    }
    REQUIRE(octree.entityCount() == 10);

    for (u32 i = 10; i < 20; ++i) {
        octree.remove(entities[i]);
    }
    REQUIRE(octree.entityCount() == 0);
}
