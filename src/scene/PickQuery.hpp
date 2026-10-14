#pragma once

#include "core/Types.hpp"
#include "ecs/Entity.hpp"
#include "ecs/World.hpp"
#include "math/Vec2.hpp"
#include "math/Vec3.hpp"
#include "render/Camera2D.hpp"
#include "render/RenderTypes.hpp"

#include <functional>
#include <string>
#include <vector>

namespace Caffeine::Scene {

/// Closest entity under a 2D world point (Transform / Sprite AABB).
ECS::Entity pickEntityAtWorld(ECS::World& world, Vec2 worldPos);

f32 rayIntersectsAABB(const Vec3& rayOrigin, const Vec3& rayDir, const Vec3& aabbMin,
                      const Vec3& aabbMax);

/// 3D ray pick (meshes, terrain, colliders). Shared by editor and scripts.
ECS::Entity raycastSelectEntity(const Vec3& rayOrigin, const Vec3& rayDir, ECS::World& world,
                                const std::string& projectRoot = "");

struct PickHit {
    enum class Kind : u8 { None, Entity, Custom };

    Kind kind = Kind::None;
    ECS::Entity entity = ECS::Entity::INVALID;
    u32 customId = 0;
    f32 distanceSq = 0.0f;
};

using CustomPickFn = std::function<bool(Vec2 worldPos, u32& outCustomId, f32& outDistanceSq)>;

/// Optional script/game pick layers (cleared when play mode resets).
class PickQueryRegistry {
public:
    static PickQueryRegistry& instance();

    void clear();
    u32 registerPicker(CustomPickFn fn);
    u32 registerScriptPicker(CustomPickFn fn);
    void unregisterPicker(u32 handle);

    PickHit pickAtWorld(ECS::World& world, Vec2 worldPos) const;
    PickHit pickAtScreen(ECS::World& world, Vec2 screenPos, const Render::Camera2D& camera) const;

private:
    std::vector<std::pair<u32, CustomPickFn>> m_custom;
    u32 m_nextHandle = 1;
};

/// Per-frame viewport + camera for screen picking from Lua (set by runtime / editor play).
class PlayRenderContext {
public:
    static PlayRenderContext& instance();

    void set2D(const Render::Rect2D& viewport, const Render::Camera2D& camera);
    void clear();

    bool has2D() const { return m_has2D; }
    const Render::Rect2D& viewport() const { return m_viewport; }
    const Render::Camera2D& camera2D() const { return m_camera; }

    Vec2 screenToWorld(Vec2 screen) const;

private:
    Render::Rect2D m_viewport{};
    Render::Camera2D m_camera{};
    bool m_has2D = false;
};

Render::Camera2D buildCamera2DFromEntity(ECS::World& world, ECS::Entity cameraEntity,
                                         const Render::Rect2D& viewport);

}  // namespace Caffeine::Scene
