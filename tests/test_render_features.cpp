#include "catch.hpp"
#include "editor/DenseOutdoorBenchmark.hpp"
#include "editor/SceneSerializer.hpp"
#include "math/Mat4.hpp"
#include "render/CoarseOcclusion.hpp"
#include "render/InstanceBatch.hpp"

#include <algorithm>
#include <filesystem>

using namespace Caffeine;
using namespace Caffeine::Render;
using namespace Caffeine::Editor;

TEST_CASE("Coarse occlusion hides a box fully behind a closer occluder", "[render][occlusion]") {
    const Mat4 view = Mat4::lookAt(Vec3(0, 0, 0), Vec3(0, 0, 10), Vec3(0, 1, 0));
    const Mat4 proj = Mat4::perspective(60.0f * 3.14159265f / 180.0f, 1.0f, 0.1f, 100.0f);
    const Mat4 vp = proj * view;

    std::vector<OcclusionPrimitive> prims(3);
    prims[0].aabbMin = Vec3(-2, -2, 4);
    prims[0].aabbMax = Vec3(2, 2, 6);
    prims[0].canOcclude = true;
    prims[0].canBeOccluded = true;
    prims[1].aabbMin = Vec3(-0.2f, -0.2f, 8);
    prims[1].aabbMax = Vec3(0.2f, 0.2f, 9);
    prims[1].canOcclude = false;
    prims[1].canBeOccluded = true;
    prims[2].aabbMin = Vec3(6, -0.2f, 8);
    prims[2].aabbMax = Vec3(7, 0.2f, 9);
    prims[2].canOcclude = false;
    prims[2].canBeOccluded = true;

    const std::vector<u32> visible =
        visibleAfterCoarseOcclusion(prims, vp, Vec3(0, 0, 0), Vec3(0, 0, 1), 4);
    REQUIRE(std::find(visible.begin(), visible.end(), 0u) != visible.end());
    REQUIRE(std::find(visible.begin(), visible.end(), 1u) == visible.end());
    REQUIRE(std::find(visible.begin(), visible.end(), 2u) != visible.end());
}

TEST_CASE("Instance batches split repeated meshes and skip unique ones", "[render][instancing]") {
    std::vector<InstanceBatchItem> items(6);
    items[0] = {1, true};
    items[1] = {1, true};
    items[2] = {1, true};
    items[3] = {2, true};
    items[4] = {1, false};
    items[5] = {1, true};
    const std::vector<InstanceBatchGroup> groups = buildInstanceBatches(items, 2);
    u32 grouped = 0;
    u32 pairs = 0;
    for (const InstanceBatchGroup& group : groups) {
        if (group.members.size() < 2) continue;
        grouped += static_cast<u32>(group.members.size());
        if (group.members.size() == 2) ++pairs;
        REQUIRE(group.members.size() <= 2);
    }
    REQUIRE(grouped == 4);
    REQUIRE(pairs == 2);
}

TEST_CASE("Dense outdoor benchmark has the planned prop and light counts", "[render][benchmark]") {
    ECS::World world;
    populateDenseOutdoorBenchmark(world);

    ECS::ComponentQuery meshes;
    meshes.with<ECS::MeshFilterComponent>();
    u32 meshCount = 0;
    u32 spheres = 0;
    u32 cubes = 0;
    world.forEach<ECS::MeshFilterComponent>(meshes, [&](ECS::Entity, ECS::MeshFilterComponent& filter) {
        ++meshCount;
        if (filter.primitive == ECS::MeshPrimitive::Sphere) ++spheres;
        if (filter.primitive == ECS::MeshPrimitive::Cube) ++cubes;
    });
    REQUIRE(spheres == kDenseOutdoorTreeCount);
    REQUIRE(cubes == kDenseOutdoorRockCount);
    REQUIRE(meshCount == kDenseOutdoorTreeCount + kDenseOutdoorRockCount + 1);

    ECS::ComponentQuery suns;
    suns.with<ECS::DirectionalLightComponent>();
    u32 sunCount = 0;
    world.forEach<ECS::DirectionalLightComponent>(suns, [&](ECS::Entity, ECS::DirectionalLightComponent&) {
        ++sunCount;
    });
    REQUIRE(sunCount == 1);

    const std::filesystem::path out =
        std::filesystem::path(__FILE__).parent_path().parent_path() / "assets/benchmarks/dense_outdoor.caf";
    std::filesystem::create_directories(out.parent_path());
    REQUIRE(SceneSerializer(world).serialize(out.string()));
    REQUIRE(std::filesystem::exists(out));
}
