#pragma once

#include "core/Types.hpp"
#include "ecs/CameraComponents.hpp"
#include "ecs/Components.hpp"
#include "ecs/Entity.hpp"
#include "ecs/World.hpp"
#include "ecs/ComponentQuery.hpp"

namespace Caffeine::Editor {
struct EditorContext;
}

namespace Caffeine::Scene {

ECS::Entity findActiveCamera2DEntity(ECS::World& world);

void syncViewportFromCamera2D(Editor::EditorContext& ctx, const ECS::Transform& transform,
                              const ECS::Camera2DComponent& camera);

}  // namespace Caffeine::Scene
