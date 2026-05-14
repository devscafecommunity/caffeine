#pragma once

#include "core/Types.hpp"
#include "ecs/World.hpp"
#include "ecs/Entity.hpp"
#include "editor/EditorContext.hpp"

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

#include <glm/glm.hpp>

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
    // Input
    void handleInput(EditorContext& ctx);
    bool isKeyPressed(ImGuiKey key) const;

    // Rendering
    void renderTranslate(const glm::vec2& screenPos, float handleLen);
    void renderRotate(const glm::vec2& screenPos, float handleLen);
    void renderScale(const glm::vec2& screenPos, float handleLen);

    // 3D rendering (2.5D approach)
    void renderTranslate3D(const glm::vec2& screenPos, float handleLen, float rotation);
    void renderRotate3D(const glm::vec2& screenPos, float handleLen);
    void renderScale3D(const glm::vec2& screenPos, float handleLen);

    // Intersection testing
    GizmoAxis intersectTest(const glm::vec2& mousePos, const glm::vec2& screenPos,
                            float handleLen, EditorContext::GizmoMode mode);

    // Transform application
    void applyTranslate(ECS::World& world, ECS::Entity entity, const glm::vec2& screenDelta,
                       GizmoAxis axis, bool snapEnabled, float zoom);
    void applyRotate(ECS::World& world, ECS::Entity entity, float deltaX, bool snapEnabled);
    void applyScale(ECS::World& world, ECS::Entity entity, const glm::vec2& screenDelta,
                   GizmoAxis axis, bool snapEnabled, float zoom);

    // Snapping
    float applySnap(float value, float snapInterval);

    // Visual constants
    static constexpr float AXIS_LINE_LENGTH = 60.0f;
    static constexpr float AXIS_LINE_WIDTH = 3.0f;
    static constexpr float HANDLE_SIZE = 8.0f;
    static constexpr float HOVER_THRESHOLD = 8.0f;
    static constexpr float ARROW_SIZE = 8.0f;
    static constexpr float BOX_SIZE = 6.0f;

    // Colors (ImGui color format)
    static constexpr u32 COLOR_X_AXIS = IM_COL32(255, 50, 50, 255);
    static constexpr u32 COLOR_Y_AXIS = IM_COL32(50, 255, 50, 255);
    static constexpr u32 COLOR_Z_AXIS = IM_COL32(50, 100, 255, 255);
    static constexpr u32 COLOR_HOVERED = IM_COL32(255, 255, 50, 255);
    static constexpr u32 COLOR_DRAGGING = IM_COL32(255, 255, 255, 255);

    // Snap values
    float m_snapTranslate = 16.0f;  // pixels
    float m_snapRotate = 45.0f;     // degrees
    float m_snapScale = 0.5f;       // units

    // State
    GizmoAxis m_hoveredAxis = GizmoAxis::None;
    GizmoAxis m_dragAxis = GizmoAxis::None;
    bool m_isDragging = false;
    glm::vec2 m_dragStartMouse = {0.f, 0.f};
    glm::vec2 m_entityStartPos = {0.f, 0.f};
};

} // namespace Caffeine::Editor
