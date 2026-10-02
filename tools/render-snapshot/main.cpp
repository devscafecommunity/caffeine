// Renders a reference scene through GpuSceneRenderer (HDR scene, probes, SSR, post stack) and
// writes the display image to a PNG. Used to check rendering changes without the editor.
//
//   caffeine-render-snapshot [out.png] [width] [height] [frames] [skyboxIndex] [view 0|1|2]

#include "assets/MaterialCache.hpp"
#include "assets/MaterialPresets.hpp"
#include "ecs/Components3D.hpp"
#include "ecs/LightComponents.hpp"
#include "ecs/MeshComponents.hpp"
#include "ecs/World.hpp"
#include "math/Quat.hpp"
#include "render/GpuSceneRenderer.hpp"
#include "rhi/CommandBuffer.hpp"
#include "rhi/RenderDevice.hpp"
#include "scene/EnvironmentSystem.hpp"
#include "scene/LightingSystem.hpp"

#include <SDL3/SDL.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace Caffeine;

namespace {

const std::string kRoot = ".";

Assets::MaterialSurface preset(const char* name) {
    for (const auto& p : Assets::materialPresets()) {
        if (p.name == name) return p.surface;
    }
    std::fprintf(stderr, "unknown preset %s\n", name);
    Assets::MaterialSurface s;
    s.valid = true;
    return s;
}

void addMesh(ECS::World& world, ECS::MeshPrimitive primitive, const Vec3& pos, const Vec3& scale,
             const std::string& materialKey, const Assets::MaterialSurface& surface) {
    Assets::MaterialSurface published = surface;
    published.valid = true;
    Assets::MaterialCache::instance().publish(materialKey, kRoot, published);
    const ECS::Entity e = world.create(materialKey.c_str());
    ECS::Position3D p;
    p.position = pos;
    world.add<ECS::Position3D>(e, p);
    world.add<ECS::Rotation3D>(e);
    ECS::Scale3D s;
    s.scale = scale;
    world.add<ECS::Scale3D>(e, s);
    ECS::MeshFilterComponent filter;
    filter.primitive = primitive;
    filter.customMaterialPath = materialKey;
    world.add<ECS::MeshFilterComponent>(e, filter);
    world.add<ECS::MeshRendererComponent>(e);
}

void buildScene(ECS::World& world) {
    Assets::MaterialSurface floor;
    floor.albedo = Vec4(0.55f, 0.55f, 0.57f, 1.0f);
    floor.roughness = 0.35f;
    addMesh(world, ECS::MeshPrimitive::Plane, Vec3(0, 0, 0), Vec3(24, 1, 24), "floor.mat", floor);

    addMesh(world, ECS::MeshPrimitive::Sphere, Vec3(0.0f, 0.75f, 0.0f), Vec3(1.5f, 1.5f, 1.5f),
            "mirror.mat", preset("Mirror"));
    addMesh(world, ECS::MeshPrimitive::Sphere, Vec3(-1.9f, 0.5f, 0.4f), Vec3(1, 1, 1), "gold.mat",
            preset("Gold"));
    addMesh(world, ECS::MeshPrimitive::Sphere, Vec3(1.9f, 0.5f, 0.4f), Vec3(1, 1, 1), "glass.mat",
            preset("Clear Glass"));

    Assets::MaterialSurface red;
    red.albedo = Vec4(0.85f, 0.12f, 0.10f, 1.0f);
    red.roughness = 0.5f;
    addMesh(world, ECS::MeshPrimitive::Cube, Vec3(0.0f, 0.5f, -2.2f), Vec3(1, 1, 1), "red.mat", red);
    Assets::MaterialSurface green;
    green.albedo = Vec4(0.15f, 0.7f, 0.2f, 1.0f);
    green.roughness = 0.6f;
    addMesh(world, ECS::MeshPrimitive::Cube, Vec3(-2.6f, 0.5f, -1.6f), Vec3(1, 1, 1), "green.mat", green);
    Assets::MaterialSurface blue;
    blue.albedo = Vec4(0.12f, 0.25f, 0.85f, 1.0f);
    blue.roughness = 0.4f;
    addMesh(world, ECS::MeshPrimitive::Cube, Vec3(2.6f, 0.5f, -1.6f), Vec3(1, 1, 1), "blue.mat", blue);
    addMesh(world, ECS::MeshPrimitive::Cube, Vec3(0.0f, 0.4f, 2.6f), Vec3(0.8f, 0.8f, 0.8f),
            "plastic.mat", preset("Glossy Plastic"));

    const ECS::Entity sun = world.create("Sun");
    world.add<ECS::Position3D>(sun);
    const Vec3 travel = Vec3(-0.45f, -0.72f, -0.52f).normalized();
    const Quat aim = Quat::lookAt(-1.0f * travel, Vec3(0.0f, 1.0f, 0.0f));
    ECS::Rotation3D rot;
    rot.quaternion = Vec4(aim.x, aim.y, aim.z, aim.w);
    world.add<ECS::Rotation3D>(sun, rot);
    ECS::LightComponent light;
    light.intensity = 2.5f;
    world.add<ECS::LightComponent>(sun, light);
    ECS::DirectionalLightComponent directional;
    directional.shadowDistance = 20.0f;
    world.add<ECS::DirectionalLightComponent>(sun, directional);
}

}  // namespace

int main(int argc, char** argv) {
    const std::string outPath = argc > 1 ? argv[1] : "snapshot.png";
    const u32 width = argc > 2 ? static_cast<u32>(std::atoi(argv[2])) : 1280;
    const u32 height = argc > 3 ? static_cast<u32>(std::atoi(argv[3])) : 720;
    const int frames = argc > 4 ? std::atoi(argv[4]) : 24;
    const int skybox = argc > 5 ? std::atoi(argv[5]) : 0;
    const int viewPreset = argc > 6 ? std::atoi(argv[6]) : 0;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow("snapshot", 64, 64, SDL_WINDOW_HIDDEN);
    RHI::RenderDevice device;
    if (!window || !device.init(window)) {
        std::fprintf(stderr, "GPU init failed: %s\n", SDL_GetError());
        return 1;
    }

    {
        Render::GpuSceneRenderer renderer;
        if (!renderer.init(&device)) {
            std::fprintf(stderr, "renderer init failed\n");
            return 1;
        }

        RHI::TextureDesc colorDesc;
        colorDesc.width = width;
        colorDesc.height = height;
        colorDesc.format = RHI::TextureFormat::R8G8B8A8_UNORM;
        colorDesc.usage = RHI::TextureUsage::Sampler | RHI::TextureUsage::ColorTarget;
        RHI::Texture* color = device.createTexture(colorDesc);

        ECS::World world;
        buildScene(world);
        {
            Scene::LightingData lights;
            Scene::collectSceneLights(world, lights);
            for (const auto& d : lights.directionals) {
                std::printf("sun dir (%.2f %.2f %.2f) intensity %.2f color (%.2f %.2f %.2f)\n",
                            d.direction.x, d.direction.y, d.direction.z, d.intensity, d.color.x,
                            d.color.y, d.color.z);
            }
        }

        Render::GpuSceneCamera camera;
        camera.position = Vec3(0.0f, 1.9f, 5.4f);
        camera.focus = Vec3(0.0f, 0.6f, 0.0f);
        if (viewPreset == 1) camera.position = Vec3(3.5f, 7.0f, 5.0f);         // high, shows shadows
        if (viewPreset == 2) camera.position = Vec3(-60.0f, 25.0f, 120.0f);    // distance / aliasing
        camera.fovRad = 50.0f * 3.14159265f / 180.0f;
        camera.nearClip = 0.1f;
        camera.farClip = 2000.0f;
        camera.view = Mat4::lookAt(camera.position, camera.focus, Vec3(0, 1, 0));
        camera.proj = Mat4::perspective(camera.fovRad, static_cast<f32>(width) / static_cast<f32>(height),
                                        camera.nearClip, camera.farClip);

        Render::GpuSceneRenderOptions options;
        options.environmentPath = Scene::resolveBuiltinSkyboxPath(skybox).string();
        options.directionalCascadeCount = 2;
        options.grid.enabled = false;
        if (std::getenv("SNAP_NO_IBL")) {
            options.resolveFeaturesFromScene = false;
            options.features.iblEnabled = false;
        }
        if (std::getenv("SNAP_NO_SHADOWS")) options.enableShadows = false;

        for (int i = 0; i < frames; ++i) {
            RHI::CommandBuffer* cmd = device.beginOffscreen();
            if (!cmd) return 1;
            renderer.renderWithCamera(cmd, world, camera, color, nullptr, width, height, kRoot, options);
            device.endOffscreen(cmd);
        }

        std::vector<u8> pixels;
        if (!device.readTexture(color, pixels)) {
            std::fprintf(stderr, "readback failed\n");
            return 1;
        }
        stbi_write_png(outPath.c_str(), static_cast<int>(width), static_cast<int>(height), 4,
                       pixels.data(), static_cast<int>(width * 4));
        std::printf("wrote %s (%ux%u, %d frames, settling=%d)\n", outPath.c_str(), width, height,
                    frames, renderer.needsAnotherFrame() ? 1 : 0);

        device.destroyTexture(color);
        renderer.shutdown();
    }
    device.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
