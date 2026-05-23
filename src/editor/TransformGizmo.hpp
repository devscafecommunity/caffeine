#pragma once

#include "core/Types.hpp"
#include "ecs/World.hpp"
#include "ecs/Entity.hpp"
#include "editor/EditorContext.hpp"
#include "math/Vec2.hpp"
#include "math/Vec3.hpp"
#include "math/Mat4.hpp"

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

namespace Caffeine::Editor {
namespace ECS = Caffeine::ECS;

enum class GizmoAxis {
    None,
    X,
    Y,
    Z,
    Center
};

class TransformGizmo {
public:
    TransformGizmo() = default;
    ~TransformGizmo() = default;

    void onImGuiRender(ECS::World& world, ECS::Entity entity, EditorContext& ctx);

private:
    void handleInput(EditorContext& ctx);
    bool isKeyPressed(int key) const;

    void renderTranslate(const Vec2& screenPos, float handleLen);
    void renderRotate(const Vec2& screenPos, float handleLen);
    void renderScale(const Vec2& screenPos, float handleLen);

    void renderTranslate3D(const Vec2& screenPos,
                           ImVec2 end0, ImVec2 end1, ImVec2 end2, bool zDimmed,
                           bool col0, bool col1, bool col2,
                           float alpha0, float alpha1, float alpha2,
                           int axis0, int axis1, int axis2);
    void renderRotate3D(const Vec2& screenPos, float handleLen, bool zDimmed);
    void renderScale3D(const Vec2& screenPos,
                       ImVec2 end0, ImVec2 end1, ImVec2 end2, bool zDimmed,
                       bool col0, bool col1, bool col2,
                       float alpha0, float alpha1, float alpha2,
                       int axis0, int axis1, int axis2);

    struct Ray3D {
        Vec3 origin;
        Vec3 direction;  // normalized
    };

    struct RayAxisTest {
        f32 distance;
        f32 t_ray;      // parameter on ray where closest point lies
        f32 s_axis;     // parameter on axis [0, axisLength]
    };

    // Convert screen coordinates to 3D world-space ray via VP⁻¹
    Ray3D screenToWorldRay(const Vec2& screenPos, const Mat4& vpInverse, 
                           const ImVec2& viewportSize, const Vec3& camPos);

    // Compute closest distance from ray to finite axis segment
    RayAxisTest rayToAxisSegmentDistance(
        const Ray3D& ray, 
        const Vec3& axisOrigin, const Vec3& axisDir, f32 axisLength);

    GizmoAxis intersectTest(const Vec2& mousePos, const Vec2& screenPos,
                            ImVec2 endX, ImVec2 endY, ImVec2 endZ,
                            float handleLen, EditorContext::GizmoMode mode, bool zDimmed,
                            bool xCollapsed, bool yCollapsed, bool zCollapsed);

    void applyTranslate(ECS::World& world, ECS::Entity entity,
                       Vec3 newWorldPos, bool snapEnabled, float snapInterval);
    void applyRotate(ECS::World& world, ECS::Entity entity,
                    float totalDeltaX, bool snapEnabled);
    void applyScale(ECS::World& world, ECS::Entity entity, const Vec2& screenDelta,
                   GizmoAxis axis, bool snapEnabled, float zoom);

    float applySnap(float value, float snapInterval);

    static constexpr float AXIS_LINE_LENGTH = 60.0f;
    static constexpr float AXIS_LINE_WIDTH = 3.0f;
    static constexpr float HANDLE_SIZE = 8.0f;
    static constexpr float HOVER_THRESHOLD = 8.0f;
    static constexpr float ARROW_SIZE = 8.0f;
    static constexpr float BOX_SIZE = 6.0f;

#ifdef CF_HAS_IMGUI
    static constexpr u32 COLOR_X_AXIS    = IM_COL32(255, 50, 50, 255);
    static constexpr u32 COLOR_Y_AXIS    = IM_COL32(50, 255, 50, 255);
    static constexpr u32 COLOR_Z_AXIS    = IM_COL32(50, 100, 255, 255);
    static constexpr u32 COLOR_Z_AXIS_DIM= IM_COL32(50, 100, 255, 80);
    static constexpr u32 COLOR_HOVERED   = IM_COL32(255, 255, 50, 255);
    static constexpr u32 COLOR_DRAGGING  = IM_COL32(255, 255, 255, 255);
#else
    static constexpr u32 COLOR_X_AXIS    = 0;
    static constexpr u32 COLOR_Y_AXIS    = 0;
    static constexpr u32 COLOR_Z_AXIS    = 0;
    static constexpr u32 COLOR_Z_AXIS_DIM= 0;
    static constexpr u32 COLOR_HOVERED   = 0;
    static constexpr u32 COLOR_DRAGGING  = 0;
#endif

    float m_snapTranslate = 16.0f;
    float m_snapRotate = 45.0f;
    float m_snapScale = 0.5f;

    GizmoAxis m_hoveredAxis = GizmoAxis::None;
    GizmoAxis m_dragAxis = GizmoAxis::None;
    bool m_isDragging = false;
    Vec2 m_dragStartMouse = {0.f, 0.f};
    Vec3 m_entityStartPos3D = {0.f, 0.f, 0.f};
    float m_entityStartRotZ = 0.f;
};

} // namespace Caffeine::Editor
