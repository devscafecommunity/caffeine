#pragma once

#include "editor/EditorContext.hpp"
#include "editor/EntityPresetUtils.hpp"
#include "ecs/ForwardRenderComponents.hpp"
#include "ecs/LightComponents.hpp"
#include "ecs/SkyboxComponents.hpp"
#include "ecs/Components.hpp"
#include "math/Quat.hpp"
#include "scene/LightingSystem.hpp"
#include "scene/SceneComponents.hpp"
#include <cstring>

namespace Caffeine::Editor {

/// Entities every new project / blank scene starts with:
/// Environment → Skybox + Directional Light, and a Camera 3D.
inline void populateDefaultScene(ECS::World& world) {
    ECS::Entity environment = world.create();
    setEntityName(world, environment, "Environment");
    world.add<ECS::Position3D>(environment);
    world.add<ECS::Rotation3D>(environment);
    world.add<ECS::Scale3D>(environment);
    world.add<ECS::PersistentComponent>(environment);
    auto& features = world.add<ECS::ForwardRenderFeaturesComponent>(environment);
    features.ibl.enabled = false;
    features.ibl.diffuse = 0.0f;
    features.ibl.specular = 1.0f;

    ECS::Entity sky = world.create();
    setEntityName(world, sky, "Skybox");
    ECS::SkyboxComponent skybox;
    skybox.enabled = true;
    skybox.presetIndex = 0;
    skybox.exposure = 1.0f;
    std::strncpy(skybox.customTexturePath, ECS::kDefaultProjectSkyPath, sizeof(skybox.customTexturePath) - 1);
    world.add<ECS::SkyboxComponent>(sky, skybox);
    world.add<ECS::PersistentComponent>(sky);
    EntityPresetUtils::parentEntity(world, sky, environment);

    ECS::Entity sun = world.create();
    setEntityName(world, sun, "Directional Light");
    auto& light = world.add<ECS::LightComponent>(sun);
    light.intensity = 2.5f;
    world.add<ECS::DirectionalLightComponent>(sun);
    world.add<ECS::Position3D>(sun);
    world.add<ECS::Rotation3D>(sun);
    world.add<ECS::Scale3D>(sun);
    Scene::applyDefaultSunOrientation(world, sun);
    EntityPresetUtils::parentEntity(world, sun, environment);

    ECS::Entity camera = world.create();
    setEntityName(world, camera, "Camera");
    world.add<ECS::Camera3DComponent>(camera);
    world.add<ECS::Position3D>(camera, ECS::Position3D{Vec3(0.0f, 2.5f, 8.0f)});
    const Vec3 look = Vec3(0.0f, 0.5f, 0.0f) - Vec3(0.0f, 2.5f, 8.0f);
    const Quat q = Caffeine::Quat::lookAt(look.normalized(), Vec3(0.0f, 1.0f, 0.0f));
    world.add<ECS::Rotation3D>(camera, ECS::Rotation3D{Vec4(q.x, q.y, q.z, q.w)});
    world.add<ECS::Scale3D>(camera);
    world.add<ECS::CameraActiveComponent>(camera, ECS::CameraActiveComponent{false});
}

}  // namespace Caffeine::Editor
