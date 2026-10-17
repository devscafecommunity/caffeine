#include "scene/SpriteLod2DSystem.hpp"

#include "ecs/ComponentQuery.hpp"
#include "ecs/Components.hpp"
#include "ecs/LodComponents.hpp"
#include "scene/HierarchySystem.hpp"

#include <algorithm>

namespace Caffeine::Scene {

void applySpriteLod2D(ECS::World& world, f32 cameraZoom) {
    ECS::ComponentQuery q;
    q.with<ECS::SpriteLod2DComponent>();
    q.with<ECS::Transform>();

    world.forEach<ECS::SpriteLod2DComponent, ECS::Transform>(
        q, [&](ECS::Entity entity, ECS::SpriteLod2DComponent& lod, ECS::Transform& transform) {
            if (isEffectivelyDisabled(world, entity)) return;

            const f32 z = std::max(0.001f, cameraZoom);
            f32 factor = 1.0f;
            if (z <= lod.zoomMinimal) {
                factor = lod.minimalScaleFactor;
            } else if (z < lod.zoomFullDetail) {
                const f32 t = (lod.zoomFullDetail - z) / (lod.zoomFullDetail - lod.zoomMinimal);
                factor = 1.0f + (lod.minimalScaleFactor - 1.0f) * std::clamp(t, 0.0f, 1.0f);
            }

            if (lod.hideWhenMinimal && z <= lod.zoomMinimal) {
                transform.scale.x = 0.0f;
                transform.scale.y = 0.0f;
                return;
            }

            transform.scale.x = lod.baseScale.x * factor;
            transform.scale.y = lod.baseScale.y * factor;
        });
}

}  // namespace Caffeine::Scene
