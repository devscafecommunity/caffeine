#include "scene/PlayMode2D.hpp"

#include "editor/EditorContext.hpp"

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

void syncViewportFromCamera2D(Editor::EditorContext& ctx, const ECS::Transform& transform,
                              const ECS::Camera2DComponent& camera) {
    ctx.viewportZoom = std::clamp(camera.zoom, 0.05f, 10.0f);
    const f32 pixelsPerUnit = ctx.viewportZoom * 50.0f;
    ctx.viewportPanX = -transform.position.x * pixelsPerUnit;
    ctx.viewportPanY = transform.position.y * pixelsPerUnit;
}

}  // namespace Caffeine::Scene
