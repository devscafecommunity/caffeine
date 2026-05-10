#pragma once
#include "core/Types.hpp"
#include "ecs/Entity.hpp"
#include "ecs/World.hpp"
#include <cstring>

namespace Caffeine::Editor {
using namespace Caffeine;

// ============================================================================
// @brief  Name component — stores the editor-facing name of an entity.
// ============================================================================
struct NameComponent {
    char name[64] = "Entity";
};

// ============================================================================
// @brief  Global state shared across Scene Editor panels.
// ============================================================================
struct EditorContext {
    ECS::Entity selectedEntity = ECS::Entity::INVALID;
    ECS::Entity hoveredEntity  = ECS::Entity::INVALID;
    bool        isDirty        = false;

    enum class GizmoMode : u8 { None, Translate, Rotate, Scale };
    enum class GizmoSpace : u8 { Local, World };

    GizmoMode  gizmoMode  = GizmoMode::Translate;
    GizmoSpace gizmoSpace = GizmoSpace::World;

    // Viewport pan/zoom state
    f32 viewportPanX = 0.0f;
    f32 viewportPanY = 0.0f;
    f32 viewportZoom = 1.0f;

    // Panel visibility toggles
    bool hierarchyOpen = true;
    bool inspectorOpen = true;
    bool viewportOpen  = true;
    bool assetsOpen    = true;
};

// ============================================================================
// @brief  Helper: get or set entity name via NameComponent.
// ============================================================================
inline const char* getEntityName(ECS::World& world, ECS::Entity e) {
    auto* nc = world.get<NameComponent>(e);
    return nc ? nc->name : "Unnamed";
}

inline void setEntityName(ECS::World& world, ECS::Entity e, const char* name) {
    auto& nc = world.add<NameComponent>(e);
    usize len = strlen(name);
    if (len >= sizeof(nc.name)) len = sizeof(nc.name) - 1;
    memcpy(nc.name, name, len);
    nc.name[len] = '\0';
}

} // namespace Caffeine::Editor
