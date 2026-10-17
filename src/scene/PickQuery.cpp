#include "scene/PickQuery.hpp"

#include "scene/HierarchySystem.hpp"
#include "scene/PlayMode2D.hpp"
#include "scene/SceneComponents.hpp"
#include "ecs/ComponentQuery.hpp"
#include "ecs/CameraComponents.hpp"
#include "ecs/Components.hpp"

#include <algorithm>
#include <cmath>

namespace Caffeine::Scene {

ECS::Entity pickEntityAtWorld(ECS::World& world, Vec2 worldPos) {
    ECS::Entity best = ECS::Entity::INVALID;
    f32 bestDistSq = 1e10f;

    ECS::ComponentQuery query;
    query.with<ECS::Transform>();

    world.forEach<ECS::Transform>(query, [&](ECS::Entity entity, ECS::Transform& pos) {
        if (isEffectivelyDisabled(world, entity)) return;

        Vec3 worldPosition = pos.position;
        f32 scaleX = std::max(0.1f, pos.scale.x);
        f32 scaleY = std::max(0.1f, pos.scale.y);

        if (auto* wt = world.get<WorldTransform>(entity)) {
            worldPosition = Vec3(wt->matrix(0, 3), wt->matrix(1, 3), wt->matrix(2, 3));
            scaleX = std::max(0.1f, sqrtf(wt->matrix(0, 0) * wt->matrix(0, 0) +
                                           wt->matrix(1, 0) * wt->matrix(1, 0)));
            scaleY = std::max(0.1f, sqrtf(wt->matrix(0, 1) * wt->matrix(0, 1) +
                                           wt->matrix(1, 1) * wt->matrix(1, 1)));
        }

        f32 halfW = std::max(0.25f, 0.5f * scaleX);
        f32 halfH = std::max(0.25f, 0.5f * scaleY);
        if (world.has<ECS::Sprite>(entity)) {
            halfW = std::max(0.5f, halfW);
            halfH = std::max(0.5f, halfH);
        }

        if (worldPos.x < worldPosition.x - halfW || worldPos.x > worldPosition.x + halfW ||
            worldPos.y < worldPosition.y - halfH || worldPos.y > worldPosition.y + halfH) {
            return;
        }

        const f32 dx = worldPos.x - worldPosition.x;
        const f32 dy = worldPos.y - worldPosition.y;
        const f32 distSq = dx * dx + dy * dy;
        if (distSq < bestDistSq) {
            bestDistSq = distSq;
            best = entity;
        }
    });

    return best;
}

PickQueryRegistry& PickQueryRegistry::instance() {
    static PickQueryRegistry s;
    return s;
}

void PickQueryRegistry::clear() {
    m_custom.clear();
    m_nextHandle = 1;
}

u32 PickQueryRegistry::registerPicker(CustomPickFn fn) {
    return registerScriptPicker(std::move(fn));
}

u32 PickQueryRegistry::registerScriptPicker(CustomPickFn fn) {
    const u32 handle = m_nextHandle++;
    m_custom.push_back({handle, std::move(fn)});
    return handle;
}

void PickQueryRegistry::unregisterPicker(u32 handle) {
    for (usize i = 0; i < m_custom.size(); ++i) {
        if (m_custom[i].first == handle) {
            m_custom.erase(m_custom.begin() + static_cast<std::ptrdiff_t>(i));
            return;
        }
    }
}

PickHit PickQueryRegistry::pickAtWorld(ECS::World& world, Vec2 worldPos) const {
    PickHit hit;
    hit.kind = PickHit::Kind::None;

    for (const auto& [id, fn] : m_custom) {
        u32 customId = 0;
        f32 distSq = 0.0f;
        if (fn && fn(worldPos, customId, distSq)) {
            if (hit.kind == PickHit::Kind::None || distSq < hit.distanceSq) {
                hit.kind = PickHit::Kind::Custom;
                hit.customId = customId;
                hit.distanceSq = distSq;
                hit.entity = ECS::Entity::INVALID;
            }
        }
    }

    ECS::Entity entity = pickEntityAtWorld(world, worldPos);
    if (entity.isValid()) {
        f32 entityDistSq = 0.0f;
        if (auto* t = world.get<ECS::Transform>(entity)) {
            const f32 edx = worldPos.x - t->position.x;
            const f32 edy = worldPos.y - t->position.y;
            entityDistSq = edx * edx + edy * edy;
        }
        if (hit.kind == PickHit::Kind::None || entityDistSq <= hit.distanceSq) {
            hit.kind = PickHit::Kind::Entity;
            hit.entity = entity;
            hit.customId = 0;
            hit.distanceSq = entityDistSq;
        }
    }

    return hit;
}

PickHit PickQueryRegistry::pickAtScreen(ECS::World& world, Vec2 screenPos,
                                        const Render::Camera2D& camera) const {
    return pickAtWorld(world, camera.screenToWorld(screenPos));
}

PlayRenderContext& PlayRenderContext::instance() {
    static PlayRenderContext s;
    return s;
}

void PlayRenderContext::set2D(const Render::Rect2D& viewport, const Render::Camera2D& camera) {
    m_viewport = viewport;
    m_camera = camera;
    m_has2D = true;
}

void PlayRenderContext::clear() {
    m_has2D = false;
}

Vec2 PlayRenderContext::screenToWorld(Vec2 screen) const {
    if (!m_has2D) return screen;
    return m_camera.screenToWorld(screen);
}

Render::Camera2D buildCamera2DFromEntity(ECS::World& world, ECS::Entity cameraEntity,
                                         const Render::Rect2D& viewport) {
    Render::Camera2D cam;
    cam.setViewport(viewport);
    if (const auto* c2 = world.get<ECS::Camera2DComponent>(cameraEntity)) {
        cam.setZoom(c2->zoom);
    }
    Vec3 worldPos{0.0f, 0.0f, 0.0f};
    if (auto* wt = world.get<WorldTransform>(cameraEntity)) {
        worldPos = wt->matrix.transformPoint(Vec3(0.0f, 0.0f, 0.0f));
    } else if (auto* transform = world.get<ECS::Transform>(cameraEntity)) {
        worldPos = transform->position;
    }
    cam.setPosition({worldPos.x, worldPos.y});
    return cam;
}

}  // namespace Caffeine::Scene
