#include "editor/TransformGizmo.hpp"
#include "ecs/Components.hpp"
#include "ecs/Components3D.hpp"
#include <algorithm>
#include <cmath>

namespace Caffeine::Editor {

// Forward declare helper function
static float pointToLineDistance(const glm::vec2& point, const glm::vec2& lineStart, const glm::vec2& lineEnd);

void TransformGizmo::onImGuiRender(ECS::World& world, ECS::Entity entity, EditorContext& ctx) {
#ifdef CF_HAS_IMGUI
    if (!entity.isValid()) return;
    if (ctx.gizmoMode == EditorContext::GizmoMode::None) return;

    // Handle keyboard input for mode switching
    handleInput(ctx);

    // Get entity position in world space
    glm::vec2 screenPos(0.f, 0.f);
    float entityRotation = 0.f;
    bool is3D = false;

    // Get viewport size for world-to-screen conversion
    ImVec2 vpSize = ImGui::GetContentRegionAvail();
    if (vpSize.x < 1 || vpSize.y < 1) return;

    ImVec2 vpMin = ImGui::GetItemRectMin();
    ImVec2 vpMax = ImGui::GetItemRectMax();

    // Try 2D components first
    auto* pos2D = world.get<ECS::Position2D>(entity);
    if (pos2D) {
        float worldToScreen = ctx.viewportZoom * 50.0f;
        // World to screen: screenPos = viewportCenter + (worldPos + pan) * worldToScreen
        screenPos.x = vpMin.x + vpSize.x * 0.5f + (pos2D->x + ctx.viewportPanX / worldToScreen) * worldToScreen;
        screenPos.y = vpMin.y + vpSize.y * 0.5f + (pos2D->y + ctx.viewportPanY / worldToScreen) * worldToScreen;
    }

    // Check for 3D components
    if (world.get<ECS::Position3D>(entity)) {
        is3D = true;
        // For 3D, we'd need proper camera transform - for now use screen center
        // A proper implementation would project the 3D position using camera matrices
        auto* pos3D = world.get<ECS::Position3D>(entity);
        if (pos3D) {
            float worldToScreen = ctx.viewportZoom * 50.0f;
            screenPos.x = vpMin.x + vpSize.x * 0.5f + (pos3D->position.x + ctx.viewportPanX / worldToScreen) * worldToScreen;
            screenPos.y = vpMin.y + vpSize.y * 0.5f + (pos3D->position.y + ctx.viewportPanY / worldToScreen) * worldToScreen;
        }
    }

    // Get rotation if available
    if (auto* rot = world.get<ECS::Rotation>(entity)) {
        entityRotation = rot->angle;
    }

    float handleLen = 30.0f * ctx.viewportZoom;

    // Get mouse position relative to viewport
    ImVec2 mousePos = ImGui::GetMousePos();
    bool mouseInViewport = (mousePos.x >= vpMin.x && mousePos.x <= vpMax.x &&
                            mousePos.y >= vpMin.y && mousePos.y <= vpMax.y);

    // Render the appropriate gizmo based on mode
    if (mouseInViewport) {
        switch (ctx.gizmoMode) {
            case EditorContext::GizmoMode::Translate:
                if (is3D) {
                    renderTranslate3D(screenPos, handleLen, entityRotation);
                } else {
                    renderTranslate(screenPos, handleLen);
                }
                break;
            case EditorContext::GizmoMode::Rotate:
                if (is3D) {
                    renderRotate3D(screenPos, handleLen);
                } else {
                    renderRotate(screenPos, handleLen);
                }
                break;
            case EditorContext::GizmoMode::Scale:
                if (is3D) {
                    renderScale3D(screenPos, handleLen);
                } else {
                    renderScale(screenPos, handleLen);
                }
                break;
            default: break;
        }

        // Test intersection and handle dragging
        if (ImGui::IsWindowFocused()) {
            glm::vec2 mousePosGlm(mousePos.x, mousePos.y);
            m_hoveredAxis = intersectTest(mousePosGlm, screenPos, handleLen, ctx.gizmoMode);

            // Handle drag start
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && !m_isDragging) {
                if (m_hoveredAxis != GizmoAxis::None) {
                    m_isDragging = true;
                    m_dragAxis = m_hoveredAxis;
                    m_dragStartMouse = {mousePos.x, mousePos.y};
                    if (pos2D) {
                        m_entityStartPos = {pos2D->x, pos2D->y};
                    } else if (auto* pos3D = world.get<ECS::Position3D>(entity)) {
                        m_entityStartPos = {pos3D->position.x, pos3D->position.y};
                    }
                }
            }

            // Handle drag continue
            if (m_isDragging && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                glm::vec2 delta = {
                    mousePos.x - m_dragStartMouse.x,
                    mousePos.y - m_dragStartMouse.y
                };

                bool snapEnabled = isKeyPressed(ImGuiKey_LeftShift) || isKeyPressed(ImGuiKey_RightShift);

                switch (ctx.gizmoMode) {
                    case EditorContext::GizmoMode::Translate:
                        applyTranslate(world, entity, delta, m_dragAxis, snapEnabled, ctx.viewportZoom);
                        break;
                    case EditorContext::GizmoMode::Rotate:
                        applyRotate(world, entity, delta.x, snapEnabled);
                        break;
                    case EditorContext::GizmoMode::Scale:
                        applyScale(world, entity, delta, m_dragAxis, snapEnabled, ctx.viewportZoom);
                        break;
                    default: break;
                }

                ctx.isDirty = true;
            } else if (m_isDragging && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                m_isDragging = false;
                m_dragAxis = GizmoAxis::None;
            }
        }
    }
#endif
}

void TransformGizmo::handleInput(EditorContext& ctx) {
#ifdef CF_HAS_IMGUI
    if (ImGui::IsKeyPressed(ImGuiKey_W)) ctx.gizmoMode = EditorContext::GizmoMode::Translate;
    if (ImGui::IsKeyPressed(ImGuiKey_E)) ctx.gizmoMode = EditorContext::GizmoMode::Rotate;
    if (ImGui::IsKeyPressed(ImGuiKey_R)) ctx.gizmoMode = EditorContext::GizmoMode::Scale;
    if (ImGui::IsKeyPressed(ImGuiKey_Q)) ctx.gizmoMode = EditorContext::GizmoMode::None;
#endif
}

bool TransformGizmo::isKeyPressed(int key) const {
#ifdef CF_HAS_IMGUI
    return ImGui::IsKeyDown(key);
#else
    return false;
#endif
}

void TransformGizmo::renderTranslate(const glm::vec2& screenPos, float handleLen) {
#ifdef CF_HAS_IMGUI
    ImDrawList* dl = ImGui::GetWindowDrawList();
    
    // X axis (red)
    u32 xColor = (m_hoveredAxis == GizmoAxis::X || m_dragAxis == GizmoAxis::X) ? COLOR_HOVERED : COLOR_X_AXIS;
    dl->AddLine(ImVec2(screenPos.x, screenPos.y), 
                ImVec2(screenPos.x + handleLen, screenPos.y), xColor, AXIS_LINE_WIDTH);
    dl->AddTriangleFilled(
        ImVec2(screenPos.x + handleLen + ARROW_SIZE, screenPos.y),
        ImVec2(screenPos.x + handleLen, screenPos.y - ARROW_SIZE * 0.6f),
        ImVec2(screenPos.x + handleLen, screenPos.y + ARROW_SIZE * 0.6f),
        xColor);

    // Y axis (green)
    u32 yColor = (m_hoveredAxis == GizmoAxis::Y || m_dragAxis == GizmoAxis::Y) ? COLOR_HOVERED : COLOR_Y_AXIS;
    dl->AddLine(ImVec2(screenPos.x, screenPos.y),
                ImVec2(screenPos.x, screenPos.y - handleLen), yColor, AXIS_LINE_WIDTH);
    dl->AddTriangleFilled(
        ImVec2(screenPos.x, screenPos.y - handleLen - ARROW_SIZE),
        ImVec2(screenPos.x - ARROW_SIZE * 0.6f, screenPos.y - handleLen),
        ImVec2(screenPos.x + ARROW_SIZE * 0.6f, screenPos.y - handleLen),
        yColor);
#endif
}

void TransformGizmo::renderRotate(const glm::vec2& screenPos, float handleLen) {
#ifdef CF_HAS_IMGUI
    ImDrawList* dl = ImGui::GetWindowDrawList();
    
    // Main circle
    u32 color = (m_hoveredAxis != GizmoAxis::None || m_dragAxis != GizmoAxis::None) ? COLOR_HOVERED : COLOR_Y_AXIS;
    dl->AddCircle(ImVec2(screenPos.x, screenPos.y), handleLen, color, 32, AXIS_LINE_WIDTH);
    
    // Angle indicator line
    dl->AddLine(ImVec2(screenPos.x, screenPos.y),
                ImVec2(screenPos.x + handleLen, screenPos.y), color, 2.0f);
#endif
}

void TransformGizmo::renderScale(const glm::vec2& screenPos, float handleLen) {
#ifdef CF_HAS_IMGUI
    ImDrawList* dl = ImGui::GetWindowDrawList();
    
    // X axis (red box)
    u32 xColor = (m_hoveredAxis == GizmoAxis::X || m_dragAxis == GizmoAxis::X) ? COLOR_HOVERED : COLOR_X_AXIS;
    dl->AddLine(ImVec2(screenPos.x, screenPos.y),
                ImVec2(screenPos.x + handleLen, screenPos.y), xColor, AXIS_LINE_WIDTH);
    dl->AddRectFilled(ImVec2(screenPos.x + handleLen - BOX_SIZE, screenPos.y - BOX_SIZE),
                      ImVec2(screenPos.x + handleLen + BOX_SIZE, screenPos.y + BOX_SIZE), xColor);

    // Y axis (green box)
    u32 yColor = (m_hoveredAxis == GizmoAxis::Y || m_dragAxis == GizmoAxis::Y) ? COLOR_HOVERED : COLOR_Y_AXIS;
    dl->AddLine(ImVec2(screenPos.x, screenPos.y),
                ImVec2(screenPos.x, screenPos.y - handleLen), yColor, AXIS_LINE_WIDTH);
    dl->AddRectFilled(ImVec2(screenPos.x - BOX_SIZE, screenPos.y - handleLen - BOX_SIZE),
                      ImVec2(screenPos.x + BOX_SIZE, screenPos.y - handleLen + BOX_SIZE), yColor);
#endif
}

// 3D Gizmos (2.5D with Z axis going diagonally)
void TransformGizmo::renderTranslate3D(const glm::vec2& screenPos, float handleLen, float rotation) {
#ifdef CF_HAS_IMGUI
    (void)rotation; // Rotation not yet used for 3D gizmos
    ImDrawList* dl = ImGui::GetWindowDrawList();
    
    // Calculate Z axis endpoint (going into screen at 45 degrees)
    float zOffset = handleLen * 0.7f;
    
    // X axis
    u32 xColor = (m_hoveredAxis == GizmoAxis::X || m_dragAxis == GizmoAxis::X) ? COLOR_HOVERED : COLOR_X_AXIS;
    dl->AddLine(ImVec2(screenPos.x, screenPos.y),
                ImVec2(screenPos.x + handleLen, screenPos.y), xColor, AXIS_LINE_WIDTH);
    dl->AddTriangleFilled(
        ImVec2(screenPos.x + handleLen + ARROW_SIZE, screenPos.y),
        ImVec2(screenPos.x + handleLen, screenPos.y - ARROW_SIZE * 0.6f),
        ImVec2(screenPos.x + handleLen, screenPos.y + ARROW_SIZE * 0.6f), xColor);

    // Y axis
    u32 yColor = (m_hoveredAxis == GizmoAxis::Y || m_dragAxis == GizmoAxis::Y) ? COLOR_HOVERED : COLOR_Y_AXIS;
    dl->AddLine(ImVec2(screenPos.x, screenPos.y),
                ImVec2(screenPos.x, screenPos.y - handleLen), yColor, AXIS_LINE_WIDTH);
    dl->AddTriangleFilled(
        ImVec2(screenPos.x, screenPos.y - handleLen - ARROW_SIZE),
        ImVec2(screenPos.x - ARROW_SIZE * 0.6f, screenPos.y - handleLen),
        ImVec2(screenPos.x + ARROW_SIZE * 0.6f, screenPos.y - handleLen), yColor);

    // Z axis (diagonal, going "into" screen)
    u32 zColor = (m_hoveredAxis == GizmoAxis::Z || m_dragAxis == GizmoAxis::Z) ? COLOR_HOVERED : COLOR_Z_AXIS;
    dl->AddLine(ImVec2(screenPos.x, screenPos.y),
                ImVec2(screenPos.x - zOffset, screenPos.y + zOffset), zColor, AXIS_LINE_WIDTH);
    dl->AddTriangleFilled(
        ImVec2(screenPos.x - zOffset - ARROW_SIZE, screenPos.y + zOffset + ARROW_SIZE),
        ImVec2(screenPos.x - zOffset - ARROW_SIZE, screenPos.y + zOffset - ARROW_SIZE),
        ImVec2(screenPos.x - zOffset + ARROW_SIZE, screenPos.y + zOffset), zColor);
#endif
}

void TransformGizmo::renderRotate3D(const glm::vec2& screenPos, float handleLen) {
#ifdef CF_HAS_IMGUI
    ImDrawList* dl = ImGui::GetWindowDrawList();
    
    // X circle
    u32 xColor = (m_hoveredAxis == GizmoAxis::X || m_dragAxis == GizmoAxis::X) ? COLOR_HOVERED : COLOR_X_AXIS;
    dl->AddCircle(ImVec2(screenPos.x, screenPos.y), handleLen, xColor, 32, AXIS_LINE_WIDTH * 0.8f);
    
    // Y circle (offset to suggest depth)
    u32 yColor = (m_hoveredAxis == GizmoAxis::Y || m_dragAxis == GizmoAxis::Y) ? COLOR_HOVERED : COLOR_Y_AXIS;
    dl->AddCircle(ImVec2(screenPos.x + 4, screenPos.y + 4), handleLen * 1.1f, yColor, 32, AXIS_LINE_WIDTH * 0.6f);
    
    // Z circle (smallest, suggesting forward direction)
    u32 zColor = (m_hoveredAxis == GizmoAxis::Z || m_dragAxis == GizmoAxis::Z) ? COLOR_HOVERED : COLOR_Z_AXIS;
    dl->AddCircle(ImVec2(screenPos.x - 3, screenPos.y - 3), handleLen * 0.9f, zColor, 32, AXIS_LINE_WIDTH * 0.6f);
#endif
}

void TransformGizmo::renderScale3D(const glm::vec2& screenPos, float handleLen) {
#ifdef CF_HAS_IMGUI
    ImDrawList* dl = ImGui::GetWindowDrawList();
    
    float zOffset = handleLen * 0.7f;
    
    // X axis with cube
    u32 xColor = (m_hoveredAxis == GizmoAxis::X || m_dragAxis == GizmoAxis::X) ? COLOR_HOVERED : COLOR_X_AXIS;
    dl->AddLine(ImVec2(screenPos.x, screenPos.y),
                ImVec2(screenPos.x + handleLen, screenPos.y), xColor, AXIS_LINE_WIDTH);
    dl->AddRectFilled(ImVec2(screenPos.x + handleLen - BOX_SIZE, screenPos.y - BOX_SIZE),
                      ImVec2(screenPos.x + handleLen + BOX_SIZE, screenPos.y + BOX_SIZE), xColor);

    // Y axis with cube
    u32 yColor = (m_hoveredAxis == GizmoAxis::Y || m_dragAxis == GizmoAxis::Y) ? COLOR_HOVERED : COLOR_Y_AXIS;
    dl->AddLine(ImVec2(screenPos.x, screenPos.y),
                ImVec2(screenPos.x, screenPos.y - handleLen), yColor, AXIS_LINE_WIDTH);
    dl->AddRectFilled(ImVec2(screenPos.x - BOX_SIZE, screenPos.y - handleLen - BOX_SIZE),
                      ImVec2(screenPos.x + BOX_SIZE, screenPos.y - handleLen + BOX_SIZE), yColor);

    // Z axis with cube
    u32 zColor = (m_hoveredAxis == GizmoAxis::Z || m_dragAxis == GizmoAxis::Z) ? COLOR_HOVERED : COLOR_Z_AXIS;
    dl->AddLine(ImVec2(screenPos.x, screenPos.y),
                ImVec2(screenPos.x - zOffset, screenPos.y + zOffset), zColor, AXIS_LINE_WIDTH);
    dl->AddRectFilled(ImVec2(screenPos.x - zOffset - BOX_SIZE, screenPos.y + zOffset - BOX_SIZE),
                      ImVec2(screenPos.x - zOffset + BOX_SIZE, screenPos.y + zOffset + BOX_SIZE), zColor);
#endif
}

GizmoAxis TransformGizmo::intersectTest(const glm::vec2& mousePos, const glm::vec2& screenPos,
                                        float handleLen, EditorContext::GizmoMode mode) {
    // Check center first (for uniform/free manipulation)
    float centerDist = std::sqrt((mousePos.x - screenPos.x) * (mousePos.x - screenPos.x) +
                                 (mousePos.y - screenPos.y) * (mousePos.y - screenPos.y));
    if (centerDist < HOVER_THRESHOLD) {
        return GizmoAxis::Center;
    }

    // Check each axis
    float zOffset = handleLen * 0.7f;

    // X axis line test
    if (pointToLineDistance(mousePos, screenPos, 
        glm::vec2(screenPos.x + handleLen, screenPos.y)) < HOVER_THRESHOLD) {
        return GizmoAxis::X;
    }

    // Y axis line test
    if (pointToLineDistance(mousePos, screenPos,
        glm::vec2(screenPos.x, screenPos.y - handleLen)) < HOVER_THRESHOLD) {
        return GizmoAxis::Y;
    }

    // Z axis line test (for 3D modes)
    if (mode != EditorContext::GizmoMode::None) {
        if (pointToLineDistance(mousePos, screenPos,
            glm::vec2(screenPos.x - zOffset, screenPos.y + zOffset)) < HOVER_THRESHOLD) {
            return GizmoAxis::Z;
        }
    }

    return GizmoAxis::None;
}

void TransformGizmo::applyTranslate(ECS::World& world, ECS::Entity entity, const glm::vec2& screenDelta,
                                     GizmoAxis axis, bool snapEnabled, float zoom) {
    // Convert screen delta to world delta
    float pixelsPerUnit = zoom * 50.0f;
    float worldDeltaX = screenDelta.x / pixelsPerUnit;
    float worldDeltaY = -screenDelta.y / pixelsPerUnit; // Y is inverted in screen space

    // Try 2D component
    auto* pos2D = world.get<ECS::Position2D>(entity);
    if (pos2D) {
        float newX = pos2D->x;
        float newY = pos2D->y;

        if (axis == GizmoAxis::X || axis == GizmoAxis::Center || axis == GizmoAxis::None) {
            newX += worldDeltaX;
            if (snapEnabled) {
                float snapWorld = m_snapTranslate / pixelsPerUnit;
                newX = applySnap(newX, snapWorld);
            }
        }
        if (axis == GizmoAxis::Y || axis == GizmoAxis::Center || axis == GizmoAxis::None) {
            newY += worldDeltaY;
            if (snapEnabled) {
                float snapWorld = m_snapTranslate / pixelsPerUnit;
                newY = applySnap(newY, snapWorld);
            }
        }

        pos2D->x = newX;
        pos2D->y = newY;
        return;
    }

    // Try 3D component
    auto* pos3D = world.get<ECS::Position3D>(entity);
    if (pos3D) {
        if (axis == GizmoAxis::X || axis == GizmoAxis::Center || axis == GizmoAxis::None) {
            float snapWorld = snapEnabled ? m_snapTranslate / pixelsPerUnit : 0.0f;
            pos3D->position.x += worldDeltaX;
            if (snapEnabled) pos3D->position.x = applySnap(pos3D->position.x, snapWorld);
        }
        if (axis == GizmoAxis::Y || axis == GizmoAxis::Center || axis == GizmoAxis::None) {
            float snapWorld = snapEnabled ? m_snapTranslate / pixelsPerUnit : 0.0f;
            pos3D->position.y += worldDeltaY;
            if (snapEnabled) pos3D->position.y = applySnap(pos3D->position.y, snapWorld);
        }
        if (axis == GizmoAxis::Z) {
            float delta = worldDeltaX * 0.5f; // Z movement on diagonal
            float snapWorld = snapEnabled ? m_snapTranslate / pixelsPerUnit : 0.0f;
            pos3D->position.z += delta;
            if (snapEnabled) pos3D->position.z = applySnap(pos3D->position.z, snapWorld);
        }
    }
}

void TransformGizmo::applyRotate(ECS::World& world, ECS::Entity entity, float deltaX, bool snapEnabled) {
    float deltaAngle = deltaX * 0.01f; // Sensitivity

    // Try 2D rotation
    auto* rot = world.get<ECS::Rotation>(entity);
    if (rot) {
        if (snapEnabled) {
            float snapRad = m_snapRotate * 3.14159265f / 180.0f;
            deltaAngle = applySnap(deltaAngle, snapRad);
        }
        rot->angle += deltaAngle;
        return;
    }

    // Try 3D rotation
    auto* rot3D = world.get<ECS::Rotation3D>(entity);
    if (rot3D) {
        // Simplified 3D rotation - in a full implementation, this would
        // multiply quaternions properly for rotation around world axes
        (void)rot3D;
    }
}

void TransformGizmo::applyScale(ECS::World& world, ECS::Entity entity, const glm::vec2& screenDelta,
                                GizmoAxis axis, bool snapEnabled, float zoom) {
    float pixelsPerUnit = zoom * 50.0f;
    float deltaX = screenDelta.x / pixelsPerUnit;
    float deltaY = -screenDelta.y / pixelsPerUnit;

    // Try 2D scale
    auto* scl2D = world.get<ECS::Scale2D>(entity);
    if (scl2D) {
        float factor = 1.0f;
        if (axis == GizmoAxis::X || axis == GizmoAxis::Center || axis == GizmoAxis::None) {
            factor = 1.0f + deltaX * 0.5f;
        }
        if (axis == GizmoAxis::Y || axis == GizmoAxis::Center || axis == GizmoAxis::None) {
            float yFactor = 1.0f + deltaY * 0.5f;
            factor = axis == GizmoAxis::Center ? (factor + yFactor) * 0.5f : yFactor;
        }

        if (axis == GizmoAxis::X || axis == GizmoAxis::Center || axis == GizmoAxis::None) {
            float newX = scl2D->x * factor;
            if (snapEnabled) newX = applySnap(newX, m_snapScale);
            scl2D->x = std::max(0.01f, newX);
        }
        if (axis == GizmoAxis::Y || axis == GizmoAxis::Center || axis == GizmoAxis::None) {
            float newY = scl2D->y * factor;
            if (snapEnabled) newY = applySnap(newY, m_snapScale);
            scl2D->y = std::max(0.01f, newY);
        }
        return;
    }

    // Try 3D scale
    auto* scl3D = world.get<ECS::Scale3D>(entity);
    if (scl3D) {
        if (axis == GizmoAxis::X || axis == GizmoAxis::Center || axis == GizmoAxis::None) {
            float newX = scl3D->scale.x * (1.0f + deltaX * 0.5f);
            if (snapEnabled) newX = applySnap(newX, m_snapScale);
            scl3D->scale.x = std::max(0.01f, newX);
        }
        if (axis == GizmoAxis::Y || axis == GizmoAxis::Center || axis == GizmoAxis::None) {
            float newY = scl3D->scale.y * (1.0f + deltaY * 0.5f);
            if (snapEnabled) newY = applySnap(newY, m_snapScale);
            scl3D->scale.y = std::max(0.01f, newY);
        }
        if (axis == GizmoAxis::Z) {
            float newZ = scl3D->scale.z * (1.0f + deltaX * 0.5f);
            if (snapEnabled) newZ = applySnap(newZ, m_snapScale);
            scl3D->scale.z = std::max(0.01f, newZ);
        }
    }
}

float TransformGizmo::applySnap(float value, float snapInterval) {
    if (snapInterval <= 0.0f) return value;
    return std::round(value / snapInterval) * snapInterval;
}

// Helper function - point to line segment distance
static float pointToLineDistance(const glm::vec2& point, const glm::vec2& lineStart, const glm::vec2& lineEnd) {
    glm::vec2 lineDir = lineEnd - lineStart;
    float lineLengthSq = lineDir.x * lineDir.x + lineDir.y * lineDir.y;
    
    if (lineLengthSq < 0.0001f) {
        return std::sqrt((point.x - lineStart.x) * (point.x - lineStart.x) +
                         (point.y - lineStart.y) * (point.y - lineStart.y));
    }

    float t = ((point.x - lineStart.x) * lineDir.x + (point.y - lineStart.y) * lineDir.y) / lineLengthSq;
    t = std::max(0.0f, std::min(1.0f, t));
    
    float projX = lineStart.x + t * lineDir.x;
    float projY = lineStart.y + t * lineDir.y;
    return std::sqrt((point.x - projX) * (point.x - projX) + (point.y - projY) * (point.y - projY));
}

} // namespace Caffeine::Editor