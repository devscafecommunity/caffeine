#include "scene/PlayMode2D.hpp"

#include "editor/EditorContext.hpp"
#include "scene/SceneComponents.hpp"

#include <algorithm>

namespace Caffeine::Scene {

ECS::Entity findActiveCamera2DEntity(ECS::World& world) {
    ECS::Entity active;
    ECS::Entity fallback;

    ECS::ComponentQuery q;
    q.with<ECS::Camera2DComponent>();
    q.with<ECS::Transform>();

    world.forEach<ECS::Camera2DComponent, ECS::Transform>(
        q, [&](ECS::Entity entity, ECS::Camera2DComponent&, ECS::Transform&) {
            if (!fallback.isValid()) {
                fallback = entity;
            }
            if (world.has<ECS::CameraActiveComponent>(entity)) {
                const auto* marker = world.get<ECS::CameraActiveComponent>(entity);
                if (marker && marker->is2D) {
                    active = entity;
                }
            }
        });

    if (active.isValid()) {
        return active;
    }
    return fallback;
}

void syncViewportFromCamera2D(ECS::World& world, ECS::Entity cameraEntity,
                              Editor::EditorContext& ctx, const ECS::Camera2DComponent& camera) {
    ctx.viewportZoom = std::clamp(camera.zoom, 0.05f, 10.0f);
    const f32 pixelsPerUnit = ctx.viewportZoom * 50.0f;

    Vec3 worldPos = Vec3(0.0f, 0.0f, 0.0f);
    if (auto* wt = world.get<WorldTransform>(cameraEntity)) {
        worldPos = wt->matrix.transformPoint(Vec3(0.0f, 0.0f, 0.0f));
    } else if (auto* transform = world.get<ECS::Transform>(cameraEntity)) {
        worldPos = transform->position;
    }

    ctx.viewportPanX = -worldPos.x * pixelsPerUnit;
    ctx.viewportPanY = worldPos.y * pixelsPerUnit;
}

}  // namespace Caffeine::Scene
