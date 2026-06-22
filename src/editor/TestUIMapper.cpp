#include "editor/TestUIMapper.hpp"
#include "editor/EditorContext.hpp"
#include "ecs/World.hpp"
#include "ecs/MeshComponents.hpp"
#include "scene/SceneComponents.hpp"
#include "math/Mat4.hpp"

namespace Caffeine::Editor {

UIMapResponse TestUIMapper::captureViewportState(
    ECS::World& world,
    EditorContext& ctx,
    f32 viewportX, f32 viewportY,
    f32 viewportWidth, f32 viewportHeight) {
    
    UIMapResponse response;
    response.viewport = ViewportInfo{viewportX, viewportY, viewportWidth, viewportHeight};
    
    ECS::ComponentQuery q;
    world.forEach(q, [&](ECS::Entity e) {
        if (!e.isValid()) return;
        
        auto* mesh = world.get<ECS::MeshFilterComponent>(e);
        if (!mesh) return;
        
        auto* transform = world.get<Scene::WorldTransform>(e);
        if (!transform) return;
        
        Vec3 entityPos = transform->matrix.transformPoint(Vec3(0, 0, 0));
        
        f32 elementWidth = 50.0f;
        f32 elementHeight = 50.0f;
        f32 screenX = viewportX + (entityPos.x + 10.0f) * 20.0f;
        f32 screenY = viewportY + (entityPos.y + 10.0f) * 20.0f;
        
        UIElement elem;
        elem.id = e.id();
        elem.name = "Entity_" + std::to_string(e.id());
        elem.x = screenX;
        elem.y = screenY;
        elem.w = elementWidth;
        elem.h = elementHeight;
        elem.selected = (e.id() == ctx.selectedEntity.id()) || 
                       std::any_of(ctx.selectedEntities.begin(), 
                                  ctx.selectedEntities.end(),
                                  [e](const ECS::Entity& sel) { return sel.id() == e.id(); });
        
        response.entities.push_back(elem);
    });
    
    return response;
}

bool TestUIMapper::clickAtCoordinate(
    ECS::World& world,
    EditorContext& ctx,
    f32 screenX, f32 screenY,
    bool shiftPressed,
    bool doubleClick,
    f32 viewportX, f32 viewportY,
    f32 viewportWidth, f32 viewportHeight) {
    
    f32 relX = screenX - viewportX;
    f32 relY = screenY - viewportY;
    
    if (relX < 0 || relY < 0 || relX >= viewportWidth || relY >= viewportHeight) {
        if (!shiftPressed) {
            ctx.clearSelection();
        }
        return false;
    }
    
    ECS::Entity clickedEntity = ECS::Entity::INVALID;
    
    ECS::ComponentQuery q;
    world.forEach(q, [&](ECS::Entity e) {
        if (!e.isValid()) return;
        auto* mesh = world.get<ECS::MeshFilterComponent>(e);
        if (!mesh) return;
        
        auto* transform = world.get<Scene::WorldTransform>(e);
        if (!transform) return;
        
        Vec3 entityPos = transform->matrix.transformPoint(Vec3(0, 0, 0));
        f32 elementX = (entityPos.x + 10.0f) * 20.0f;
        f32 elementY = (entityPos.y + 10.0f) * 20.0f;
        f32 elementW = 50.0f;
        f32 elementH = 50.0f;
        
        if (relX >= elementX && relX <= elementX + elementW &&
            relY >= elementY && relY <= elementY + elementH) {
            clickedEntity = e;
        }
    });
    
    if (clickedEntity.isValid()) {
        if (shiftPressed) {
            ctx.toggleSelection(clickedEntity);
        } else {
            ctx.selectEntity(clickedEntity);
        }
        
        if (doubleClick) {
            Vec3 pos;
            if (auto* t = world.get<Scene::WorldTransform>(clickedEntity)) {
                pos = t->matrix.transformPoint(Vec3(0, 0, 0));
                ctx.camFocus = pos;
                ctx.camDistance = 5.0f;
            }
        }
        
        return true;
    }
    
    if (!shiftPressed) {
        ctx.clearSelection();
    }
    return false;
}

}
