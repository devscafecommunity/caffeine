#include "catch.hpp"

#include "ecs/ForwardRenderComponents.hpp"
#include "ecs/World.hpp"
#include "render/ForwardRenderFeatures.hpp"
#include "render/GpuEnvironmentMap.hpp"

#include <vector>

using namespace Caffeine;
using namespace Caffeine::ECS;
using namespace Caffeine::Render;

TEST_CASE("Forward render features resolve from scene component", "[forwardrender]") {
    World world;
    const RenderFeatureSettings defaults = budgetSafeForwardDefaults();
    REQUIRE(defaults.instancingEnabled);
    REQUIRE(defaults.maxInstancesPerBatch == 256u);
    REQUIRE(defaults.iblEnabled);
    REQUIRE(defaults.iblDiffuse == Approx(1.0f));
    REQUIRE(defaults.iblSpecular == Approx(1.0f));
    REQUIRE(defaults.occlusion == OcclusionMode::Off);
    REQUIRE(defaults.reflections == ReflectionMode::Probe);
    REQUIRE(defaults.screenSpaceTrace);
    REQUIRE(defaults.maxReflectionProbes >= 1u);
    REQUIRE(defaults.volumetrics == VolumetricQuality::Off);

    Entity env = world.create();
    auto& fx = world.add<ForwardRenderFeaturesComponent>(env);
    fx.reflections.enabled = true;
    fx.reflections.mode = ReflectionMode::Planar;
    fx.reflections.planeY = 0.0f;
    fx.occlusion.enabled = true;

    const RenderFeatureSettings resolved = resolveForwardRenderFeatures(world);
    REQUIRE(resolved.reflections == ReflectionMode::Planar);
    REQUIRE(resolved.occlusion == OcclusionMode::Coarse);
}

TEST_CASE("Environment mip chain keeps a directional sky/ground gradient", "[forwardrender]") {
    // 16x8 equirect: bright sky on the top half, dark ground on the bottom half.
    constexpr u32 w = 16;
    constexpr u32 h = 8;
    std::vector<u8> pixels(w * h * 4);
    for (u32 y = 0; y < h; ++y) {
        const u8 v = y < h / 2 ? 200 : 40;
        for (u32 x = 0; x < w; ++x) {
            u8* p = &pixels[(y * w + x) * 4];
            p[0] = v;
            p[1] = v;
            p[2] = v;
            p[3] = 255;
        }
    }

    const auto chain = buildEnvironmentMipChain(pixels.data(), w, h, 1024, 4);
    REQUIRE(chain.size() == 3);
    REQUIRE(chain[0].width == 16);
    REQUIRE(chain[1].width == 8);
    REQUIRE(chain.back().width == 4);
    REQUIRE(chain.back().height == 2);
    REQUIRE(chain.back().rgba[0] == 200);
    REQUIRE(chain.back().rgba[(1 * 4 + 0) * 4] == 40);
    REQUIRE(averageEnvironmentColor(chain.back()).x == Approx(120.0f / 255.0f).margin(0.01f));

    const auto capped = buildEnvironmentMipChain(pixels.data(), w, h, 8, 4);
    REQUIRE(capped.front().width == 8);

    f32 u = 0.0f;
    f32 v = 0.0f;
    directionToEnvironmentUV(Vec3(0.0f, 1.0f, 0.0f), u, v);
    REQUIRE(v == Approx(0.0f).margin(1e-4f));
    directionToEnvironmentUV(Vec3(0.0f, 0.0f, 1.0f), u, v);
    REQUIRE(u == Approx(0.5f));
    REQUIRE(v == Approx(0.5f));
}
