#include "scene/NaturalFluidSystem.hpp"

#include "caffeine/environment/NaturalFluids.hpp"
#include "ecs/ComponentQuery.hpp"
#include "ecs/MeshComponents.hpp"
#include "ecs/MeshGeometry.hpp"
#include "ecs/World.hpp"
#include "math/Math.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Caffeine::Scene {
namespace {

using ECS::NaturalFluidMode;
using ECS::NaturalFluidPreset;
using Fluids::FluidKind;
using Fluids::FluidWorld;
using Fluids::NaturalFluidSettings;
using Fluids::SurfaceMesh;
using Fluids::WaterQuality;

struct FluidRuntime {
    FluidWorld fluid;
    u64 settingsHash = 0;
    u32 meshRevision = 0;
    bool initialized = false;
};

std::unordered_map<u32, FluidRuntime> g_runtime;

u64 hashComponent(const ECS::NaturalFluidComponent& component) {
    const u8* bytes = reinterpret_cast<const u8*>(&component);
    u64 hash = 1469598103934665603ull;
    for (size_t i = 0; i < sizeof(component); ++i) {
        hash ^= static_cast<u64>(bytes[i]);
        hash *= 1099511628211ull;
    }
    return hash;
}

NaturalFluidSettings settingsFrom(const ECS::NaturalFluidComponent& component) {
    NaturalFluidSettings settings;

    const int mode = std::clamp(static_cast<int>(component.mode), 0, 3);
    settings.waves.quality = static_cast<WaterQuality>(std::min(mode, 2));
    settings.waves.windSpeed = component.windSpeed;
    settings.waves.windDirectionDeg = component.windDirectionDeg;
    settings.waves.amplitudeScale = component.amplitudeScale;
    settings.waves.choppiness = component.choppiness;
    settings.waves.wavelengthScale = component.wavelengthScale;
    settings.waves.foamThreshold = component.foamThreshold;
    settings.waves.depth = component.depth;
    settings.waves.seed = component.seed;
    settings.waves.waveCount = mode == 2 ? 14 : (mode == 1 ? 5 : 0);

    settings.shallow.resolution =
        std::clamp(static_cast<int>(component.simulationResolution), 8, 192);
    settings.shallow.sizeX = component.sizeX;
    settings.shallow.sizeZ = component.sizeZ;
    settings.shallow.baseDepth = std::max(component.shallowDepth, 0.05f);
    settings.shallow.windSpeed = component.windSpeed;
    settings.shallow.windDirectionDeg = component.windDirectionDeg;
    settings.shallow.windStrength = 0.00025f;

    settings.fluid2D.resolution =
        std::clamp(static_cast<int>(component.simulationResolution), 16, 192);
    settings.fluid2D.width = component.sizeX;
    settings.fluid2D.height = component.sizeZ;

    const int fluidRes = std::clamp(static_cast<int>(component.fluidResolution), 8, 48);
    settings.volume.resolutionX = fluidRes;
    settings.volume.resolutionY = fluidRes;
    settings.volume.resolutionZ = fluidRes;
    settings.volume.sizeX = component.sizeX;
    settings.volume.sizeY = component.sizeY;
    settings.volume.sizeZ = component.sizeZ;

    settings.simulateShallowWater = component.shallowPhysics != 0 && mode != 0;
    settings.simulateFluid2D = component.simulateFluid2D != 0;
    settings.simulateVolume = mode == 3;
    settings.fluidPreset =
        static_cast<FluidKind>(std::clamp(static_cast<int>(component.preset), 0, 3));
    settings.timeScale = component.timeScale;
    return settings;
}

void ensureRenderComponents(ECS::World& world, ECS::Entity entity,
                            const ECS::NaturalFluidComponent& component) {
    if (!world.has<ECS::MeshFilterComponent>(entity)) {
        auto& filter = world.add<ECS::MeshFilterComponent>(entity);
        filter.primitive = ECS::MeshPrimitive::Custom;
    }
    if (!world.has<ECS::MeshRendererComponent>(entity)) {
        auto& renderer = world.add<ECS::MeshRendererComponent>(entity);
        renderer.meshPath.clear();
        renderer.materialPath.clear();
    }
    if (auto* renderer = world.get<ECS::MeshRendererComponent>(entity)) {
        const bool volume = component.mode == static_cast<u8>(ECS::NaturalFluidMode::FluidVolume);
        renderer->castShadows = component.castShadows != 0 && !volume;
        renderer->receiveShadows = component.receiveShadows != 0;
    }
}

ECS::MeshGeometryComponent& ensureGeometry(ECS::World& world, ECS::Entity entity) {
    if (ECS::MeshGeometryComponent* geometry = world.get<ECS::MeshGeometryComponent>(entity)) {
        return *geometry;
    }
    return world.add<ECS::MeshGeometryComponent>(entity);
}

void updateSurfaceMesh(ECS::World& world, ECS::Entity entity,
                       const ECS::NaturalFluidComponent& component, FluidRuntime& runtime) {
    SurfaceMesh mesh;
    const int resolution = std::clamp(static_cast<int>(component.surfaceResolution), 8, 256);
    runtime.fluid.buildSurfaceMesh(resolution, component.sizeX, component.sizeZ, mesh);

    ECS::MeshGeometryComponent& geometry = ensureGeometry(world, entity);
    geometry.positions = std::move(mesh.positions);
    for (Vec3& position : geometry.positions) position.y += component.waterLevel;
    geometry.normals = std::move(mesh.normals);
    geometry.texcoords = std::move(mesh.texcoords);
    geometry.indices = std::move(mesh.indices);
    geometry.revision = ++runtime.meshRevision;
}

void addCube(std::vector<Vec3>& positions, std::vector<Vec3>& normals,
             std::vector<Vec2>& texcoords, std::vector<u32>& indices, const Vec3& center,
             f32 half) {
    const Vec3 p000(center.x - half, center.y - half, center.z - half);
    const Vec3 p100(center.x + half, center.y - half, center.z - half);
    const Vec3 p010(center.x - half, center.y + half, center.z - half);
    const Vec3 p110(center.x + half, center.y + half, center.z - half);
    const Vec3 p001(center.x - half, center.y - half, center.z + half);
    const Vec3 p101(center.x + half, center.y - half, center.z + half);
    const Vec3 p011(center.x - half, center.y + half, center.z + half);
    const Vec3 p111(center.x + half, center.y + half, center.z + half);

    auto face = [&](const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d,
                    const Vec3& normal) {
        const u32 base = static_cast<u32>(positions.size());
        positions.push_back(a);
        positions.push_back(b);
        positions.push_back(c);
        positions.push_back(d);
        for (int i = 0; i < 4; ++i) normals.push_back(normal);
        texcoords.push_back(Vec2(0.0f, 0.0f));
        texcoords.push_back(Vec2(1.0f, 0.0f));
        texcoords.push_back(Vec2(1.0f, 1.0f));
        texcoords.push_back(Vec2(0.0f, 1.0f));
        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 0);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
    };

    face(p100, p110, p111, p101, Vec3(1, 0, 0));   // +X
    face(p001, p011, p010, p000, Vec3(-1, 0, 0));  // -X
    face(p010, p011, p111, p110, Vec3(0, 1, 0));   // +Y
    face(p000, p100, p101, p001, Vec3(0, -1, 0));  // -Y
    face(p001, p101, p111, p011, Vec3(0, 0, 1));   // +Z
    face(p000, p010, p110, p100, Vec3(0, 0, -1));  // -Z
}

void updateVolumeMesh(ECS::World& world, ECS::Entity entity,
                      const ECS::NaturalFluidComponent& component, FluidRuntime& runtime) {
    const Fluids::FluidVolume& volume = runtime.fluid.volume();
    const int nx = volume.resolutionX();
    const int ny = volume.resolutionY();
    const int nz = volume.resolutionZ();
    const float dx = component.sizeX / static_cast<f32>(nx);
    const float dy = component.sizeY / static_cast<f32>(ny);
    const float dz = component.sizeZ / static_cast<f32>(nz);
    const float half = 0.42f * std::min({dx, dy, dz});

    ECS::MeshGeometryComponent& geometry = ensureGeometry(world, entity);
    geometry.positions.clear();
    geometry.normals.clear();
    geometry.texcoords.clear();
    geometry.indices.clear();

    const bool hot = component.preset == static_cast<u8>(NaturalFluidPreset::Fire) ||
                     component.preset == static_cast<u8>(NaturalFluidPreset::Smoke);
    const f32 threshold = 0.035f;

    for (int k = 0; k < nz; ++k) {
        for (int j = 0; j < ny; ++j) {
            for (int i = 0; i < nx; ++i) {
                const f32 density = volume.densityAt(i, j, k);
                const f32 temperature = volume.temperatureAt(i, j, k);
                const f32 activation = density + (hot ? temperature * 0.30f : 0.0f);
                if (activation < threshold) continue;
                const Vec3 center(-component.sizeX * 0.5f + (static_cast<f32>(i) + 0.5f) * dx,
                                  -component.sizeY * 0.5f + (static_cast<f32>(j) + 0.5f) * dy,
                                  -component.sizeZ * 0.5f + (static_cast<f32>(k) + 0.5f) * dz);
                addCube(geometry.positions, geometry.normals, geometry.texcoords,
                        geometry.indices, center, half);
            }
        }
    }
    geometry.revision = ++runtime.meshRevision;
}

void clearMesh(ECS::World& world, ECS::Entity entity) {
    if (ECS::MeshGeometryComponent* geometry = world.get<ECS::MeshGeometryComponent>(entity)) {
        if (!geometry->positions.empty()) {
            geometry->positions.clear();
            geometry->normals.clear();
            geometry->texcoords.clear();
            geometry->indices.clear();
            ++geometry->revision;
        }
    }
}

}  // namespace

void tickNaturalFluids(ECS::World& world, f32 dt, bool editorPreview) {
    dt = std::clamp(dt, 0.0f, 0.05f);

    struct Item {
        u32 id = 0;
        ECS::NaturalFluidComponent component;
    };
    std::vector<Item> items;
    {
        ECS::ComponentQuery query;
        query.with<ECS::NaturalFluidComponent>();
        world.forEach<ECS::NaturalFluidComponent>(
            query, [&](ECS::Entity entity, ECS::NaturalFluidComponent& component) {
                items.push_back({entity.id(), component});
            });
    }

    std::unordered_set<u32> alive;
    alive.reserve(items.size());
    for (const Item& item : items) {
        alive.insert(item.id);
        ECS::Entity entity(item.id, &world);
        if (!entity.isValid() || !world.isEntityAlive(entity)) continue;

        if (!item.component.enabled) {
            clearMesh(world, entity);
            continue;
        }

        ensureRenderComponents(world, entity, item.component);
        FluidRuntime& runtime = g_runtime[item.id];
        const u64 hash = hashComponent(item.component);
        if (!runtime.initialized || hash != runtime.settingsHash) {
            const NaturalFluidSettings settings = settingsFrom(item.component);
            runtime.fluid.configure(settings);
            runtime.fluid.reset();

            const f32 windRadians = Math::degToRad(item.component.fluidWindDirectionDeg);
            const f32 wind = item.component.fluidWindSpeed * 0.02f;
            runtime.fluid.fluid2D().setWind(
                Vec2(std::cos(windRadians) * wind, std::sin(windRadians) * wind));
            runtime.fluid.volume().setWind(
                Vec3(std::cos(windRadians) * wind, 0.0f, std::sin(windRadians) * wind));

            runtime.settingsHash = hash;
            runtime.initialized = true;
        }

        const bool advance = !editorPreview || item.component.previewInEditor != 0;
        const f32 step = advance ? dt : 0.0f;

        if (item.component.mode == static_cast<u8>(NaturalFluidMode::FluidVolume)) {
            if (step > 0.0f) {
                const f32 sourceX = item.component.sizeX * 0.5f;
                const f32 sourceY = item.component.sizeY * 0.16f;
                const f32 sourceZ = item.component.sizeZ * 0.5f;
                runtime.fluid.addVolumeSource(sourceX, sourceY, sourceZ,
                                              item.component.fluidSourceRate * step,
                                              item.component.fluidSourceRadius);
                const int preset = std::clamp(static_cast<int>(item.component.preset), 0, 3);
                if (preset == static_cast<int>(NaturalFluidPreset::Fire) ||
                    preset == static_cast<int>(NaturalFluidPreset::Smoke)) {
                    runtime.fluid.volume().addTemperature(
                        sourceX, sourceY, sourceZ,
                        item.component.fluidSourceTemperature * step * 3.0f,
                        item.component.fluidSourceRadius);
                }
                runtime.fluid.update(step);
            }
            if (item.component.renderVolume != 0) {
                updateVolumeMesh(world, entity, item.component, runtime);
            } else {
                clearMesh(world, entity);
            }
        } else {
            if (step > 0.0f) {
                if (item.component.simulateFluid2D != 0) {
                    runtime.fluid.addFluidSource(item.component.sizeX * 0.5f,
                                                 item.component.sizeZ * 0.16f,
                                                 item.component.fluidSourceRate * step,
                                                 item.component.fluidSourceRadius);
                }
                runtime.fluid.update(step);
            }
            updateSurfaceMesh(world, entity, item.component, runtime);
        }
    }

    for (auto it = g_runtime.begin(); it != g_runtime.end();) {
        if (alive.find(it->first) == alive.end()) {
            it = g_runtime.erase(it);
        } else {
            ++it;
        }
    }
}

const Fluids::FluidWorld* naturalFluidWorldFor(ECS::World& world, u32 entityId) {
    (void)world;
    auto it = g_runtime.find(entityId);
    return it != g_runtime.end() ? &it->second.fluid : nullptr;
}

Fluids::FluidWorld* naturalFluidWorldForEdit(ECS::World& world, u32 entityId) {
    (void)world;
    auto it = g_runtime.find(entityId);
    return it != g_runtime.end() ? &it->second.fluid : nullptr;
}

void resetNaturalFluid(ECS::World& world, u32 entityId) {
    (void)world;
    auto it = g_runtime.find(entityId);
    if (it != g_runtime.end()) it->second.initialized = false;
}

void resetNaturalFluidRuntime() {
    g_runtime.clear();
}

}  // namespace Caffeine::Scene
