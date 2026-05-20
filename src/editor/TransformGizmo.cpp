#include "editor/TransformGizmo.hpp"
#include "editor/SceneViewport.hpp"
#include "ecs/Components.hpp"
#include "ecs/Components3D.hpp"
#include <algorithm>
#include <cmath>

namespace Caffeine::Editor {

static float pointToLineDistance(const Vec2& point, const Vec2& lineStart, const Vec2& lineEnd);

void TransformGizmo::onImGuiRender(ECS::World& world, ECS::Entity entity, EditorContext& ctx) {
#ifdef CF_HAS_IMGUI
    if (!entity.isValid()) return;
    if (ctx.gizmoMode == EditorContext::GizmoMode::None) return;

    handleInput(ctx);

    ImVec2 vpSize = ImGui::GetContentRegionAvail();
    if (vpSize.x < 1 || vpSize.y < 1) return;

    ImVec2 vpMin = ImGui::GetItemRectMin();
    ImVec2 vpMax = ImGui::GetItemRectMax();

    auto* transform = world.get<ECS::Transform>(entity);
    if (!transform) return;

    bool zDimmed = (ctx.viewMode == EditorContext::ViewMode::Mode2D);

    ImVec2 sp2     = SceneViewport::projectToScreen(transform->position, vpMin, vpSize, ctx);
    Vec2 screenPos = Vec2(sp2.x, sp2.y);

    float handleLen = 30.0f * ctx.viewportZoom;

    float handleWorld;
    if (ctx.viewMode == EditorContext::ViewMode::Mode3D) {
        float s    = ctx.viewportZoom * 50.0f;
        float sinY = std::sin(ctx.camYaw),  cosY = std::cos(ctx.camYaw);
        float sinP = std::sin(ctx.camPitch), cosP = std::cos(ctx.camPitch);
        Vec3  wp   = transform->position;
        float rx   = wp.x - ctx.camFocus.x;
        float ry   = wp.y - ctx.camFocus.y;
        float rz   = wp.z - ctx.camFocus.z;
        float vz_c = -sinY * rx + cosY * rz;
        float vz2  = -sinP * ry + cosP * vz_c;
        float dist = std::max(ctx.camDistance, 0.1f);
        float fovScale = s * dist / std::max(dist + vz2, 0.01f);
        handleWorld = handleLen / std::max(fovScale, 0.01f);
    } else {
        handleWorld = handleLen / (ctx.viewportZoom * 50.0f);
    }

    Vec3 wp = transform->position;
    ImVec2 rawEndX = SceneViewport::projectToScreen({wp.x + handleWorld, wp.y, wp.z}, vpMin, vpSize, ctx);
    ImVec2 rawEndY = SceneViewport::projectToScreen({wp.x, wp.y + handleWorld, wp.z}, vpMin, vpSize, ctx);
    ImVec2 rawEndZ = SceneViewport::projectToScreen({wp.x, wp.y, wp.z + handleWorld}, vpMin, vpSize, ctx);

    auto normalizeHandleTo = [&](ImVec2 end, ImVec2 fallback) -> ImVec2 {
        float dx = end.x - sp2.x, dy = end.y - sp2.y;
        float d = std::sqrt(dx*dx + dy*dy);
        if (d < 3.0f) return fallback;
        return ImVec2(sp2.x + dx/d * handleLen, sp2.y + dy/d * handleLen);
    };

    ImVec2 endX = normalizeHandleTo(rawEndX, ImVec2(sp2.x + handleLen, sp2.y));
    ImVec2 endY = normalizeHandleTo(rawEndY, ImVec2(sp2.x, sp2.y - handleLen));
    ImVec2 endZ = normalizeHandleTo(rawEndZ, ImVec2(sp2.x - handleLen * 0.6f, sp2.y + handleLen * 0.6f));
    {
        float dzToY = std::sqrt((endZ.x - endY.x) * (endZ.x - endY.x) + (endZ.y - endY.y) * (endZ.y - endY.y));
        if (dzToY < handleLen * 0.3f)
            endZ = ImVec2(sp2.x - handleLen * 0.6f, sp2.y + handleLen * 0.6f);
    }

    ImVec2 mousePos = ImGui::GetMousePos();
    bool mouseInViewport = (mousePos.x >= vpMin.x && mousePos.x <= vpMax.x &&
                            mousePos.y >= vpMin.y && mousePos.y <= vpMax.y);

        if (mouseInViewport) {
        switch (ctx.gizmoMode) {
            case EditorContext::GizmoMode::Translate:
                renderTranslate3D(screenPos, endX, endY, endZ, zDimmed);
                break;
            case EditorContext::GizmoMode::Rotate:
                renderRotate3D(screenPos, handleLen, zDimmed);
                break;
            case EditorContext::GizmoMode::Scale:
                renderScale3D(screenPos, endX, endY, endZ, zDimmed);
                break;
            default: break;
        }

        if (ImGui::IsWindowFocused()) {
            Vec2 mousePosGlm(mousePos.x, mousePos.y);
            m_hoveredAxis = intersectTest(mousePosGlm, screenPos, endX, endY, endZ, handleLen, ctx.gizmoMode, zDimmed);

            if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && !m_isDragging) {
                if (m_hoveredAxis != GizmoAxis::None) {
                    m_isDragging = true;
                    m_dragAxis = m_hoveredAxis;
                    m_dragStartMouse = {mousePos.x, mousePos.y};
                    auto* t = world.get<ECS::Transform>(entity);
                    if (t) {
                        m_entityStartPos3D = t->position;
                        m_entityStartRotZ  = t->rotation.z;
                    } else {
                        auto* p3 = world.get<ECS::Position3D>(entity);
                        m_entityStartPos3D = p3 ? p3->position : Vec3{0.f, 0.f, 0.f};
                        auto* r3 = world.get<ECS::Rotation3D>(entity);
                        m_entityStartRotZ  = r3 ? 0.f : 0.f;
                    }
                }
            }

            if (m_isDragging && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                Vec2 delta = {
                    mousePos.x - m_dragStartMouse.x,
                    mousePos.y - m_dragStartMouse.y
                };

                bool snapEnabled = isKeyPressed(ImGuiKey_LeftShift) || isKeyPressed(ImGuiKey_RightShift);

                // Project screen-space delta onto the projected axis direction,
                // then convert pixels → world units using handleLen/handleWorld ratio.
                auto projectOnto = [&](ImVec2 axisEnd) -> float {
                    float axDx = axisEnd.x - sp2.x;
                    float axDy = axisEnd.y - sp2.y;
                    float axLen = std::sqrt(axDx * axDx + axDy * axDy);
                    if (axLen < 0.001f) return 0.f;
                    float projected = (delta.x * axDx + delta.y * axDy) / axLen;
                    return projected * handleWorld / handleLen;
                };

                switch (ctx.gizmoMode) {
                    case EditorContext::GizmoMode::Translate: {
                        Vec3 newPos = m_entityStartPos3D;
                        switch (m_dragAxis) {
                            case GizmoAxis::X:
                                newPos.x += projectOnto(endX);
                                break;
                            case GizmoAxis::Y:
                                newPos.y += projectOnto(endY);
                                break;
                            case GizmoAxis::Z:
                                newPos.z += projectOnto(endZ);
                                break;
                            case GizmoAxis::Center:
                                newPos.x += projectOnto(endX);
                                newPos.y += projectOnto(endY);
                                break;
                            default: break;
                        }
                        float snapInterval = snapEnabled ? m_snapTranslate * handleWorld / std::max(handleLen, 0.001f) : 0.f;
                        applyTranslate(world, entity, newPos, snapEnabled, snapInterval);
                        break;
                    }
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
    return ImGui::IsKeyDown(static_cast<ImGuiKey>(key));
#else
    return false;
#endif
}

void TransformGizmo::renderTranslate(const Vec2& screenPos, float handleLen) {
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

void TransformGizmo::renderRotate(const Vec2& screenPos, float handleLen) {
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

void TransformGizmo::renderScale(const Vec2& screenPos, float handleLen) {
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

void TransformGizmo::renderTranslate3D(const Vec2& screenPos, ImVec2 endX, ImVec2 endY, ImVec2 endZ, bool zDimmed) {
#ifdef CF_HAS_IMGUI
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 sp(screenPos.x, screenPos.y);

    auto drawAxisArrow = [&](ImVec2 from, ImVec2 to, u32 color) {
        dl->AddLine(from, to, color, AXIS_LINE_WIDTH);
        float dx = to.x - from.x, dy = to.y - from.y;
        float len = std::sqrt(dx*dx + dy*dy);
        if (len < 0.001f) return;
        float ux = dx / len, uy = dy / len;
        float nx = -uy * ARROW_SIZE * 0.6f, ny = ux * ARROW_SIZE * 0.6f;
        ImVec2 tip(to.x + ux * ARROW_SIZE, to.y + uy * ARROW_SIZE);
        dl->AddTriangleFilled(tip, ImVec2(to.x + nx, to.y + ny), ImVec2(to.x - nx, to.y - ny), color);
    };

    u32 xColor = (m_hoveredAxis == GizmoAxis::X || m_dragAxis == GizmoAxis::X) ? COLOR_HOVERED : COLOR_X_AXIS;
    drawAxisArrow(sp, endX, xColor);

    u32 yColor = (m_hoveredAxis == GizmoAxis::Y || m_dragAxis == GizmoAxis::Y) ? COLOR_HOVERED : COLOR_Y_AXIS;
    drawAxisArrow(sp, endY, yColor);

    if (!zDimmed) {
        u32 zColor = (m_hoveredAxis == GizmoAxis::Z || m_dragAxis == GizmoAxis::Z) ? COLOR_HOVERED : COLOR_Z_AXIS;
        drawAxisArrow(sp, endZ, zColor);
    }
#endif
}

void TransformGizmo::renderRotate3D(const Vec2& screenPos, float handleLen, bool zDimmed) {
#ifdef CF_HAS_IMGUI
    ImDrawList* dl = ImGui::GetWindowDrawList();
    
    // X circle
    u32 xColor = (m_hoveredAxis == GizmoAxis::X || m_dragAxis == GizmoAxis::X) ? COLOR_HOVERED : COLOR_X_AXIS;
    dl->AddCircle(ImVec2(screenPos.x, screenPos.y), handleLen, xColor, 32, AXIS_LINE_WIDTH * 0.8f);
    
    // Y circle (offset to suggest depth)
    u32 yColor = (m_hoveredAxis == GizmoAxis::Y || m_dragAxis == GizmoAxis::Y) ? COLOR_HOVERED : COLOR_Y_AXIS;
    dl->AddCircle(ImVec2(screenPos.x + 4, screenPos.y + 4), handleLen * 1.1f, yColor, 32, AXIS_LINE_WIDTH * 0.6f);
    
    if (!zDimmed) {
        u32 zColor = (m_hoveredAxis == GizmoAxis::Z || m_dragAxis == GizmoAxis::Z) ? COLOR_HOVERED : COLOR_Z_AXIS;
        dl->AddCircle(ImVec2(screenPos.x - 3, screenPos.y - 3), handleLen * 0.9f, zColor, 32, AXIS_LINE_WIDTH * 0.6f);
    }
#endif
}

void TransformGizmo::renderScale3D(const Vec2& screenPos, ImVec2 endX, ImVec2 endY, ImVec2 endZ, bool zDimmed) {
#ifdef CF_HAS_IMGUI
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 sp(screenPos.x, screenPos.y);

    u32 xColor = (m_hoveredAxis == GizmoAxis::X || m_dragAxis == GizmoAxis::X) ? COLOR_HOVERED : COLOR_X_AXIS;
    dl->AddLine(sp, endX, xColor, AXIS_LINE_WIDTH);
    dl->AddRectFilled(ImVec2(endX.x - BOX_SIZE, endX.y - BOX_SIZE),
                      ImVec2(endX.x + BOX_SIZE, endX.y + BOX_SIZE), xColor);

    u32 yColor = (m_hoveredAxis == GizmoAxis::Y || m_dragAxis == GizmoAxis::Y) ? COLOR_HOVERED : COLOR_Y_AXIS;
    dl->AddLine(sp, endY, yColor, AXIS_LINE_WIDTH);
    dl->AddRectFilled(ImVec2(endY.x - BOX_SIZE, endY.y - BOX_SIZE),
                      ImVec2(endY.x + BOX_SIZE, endY.y + BOX_SIZE), yColor);

    if (!zDimmed) {
        u32 zColor = (m_hoveredAxis == GizmoAxis::Z || m_dragAxis == GizmoAxis::Z) ? COLOR_HOVERED : COLOR_Z_AXIS;
        dl->AddLine(sp, endZ, zColor, AXIS_LINE_WIDTH);
        dl->AddRectFilled(ImVec2(endZ.x - BOX_SIZE, endZ.y - BOX_SIZE),
                          ImVec2(endZ.x + BOX_SIZE, endZ.y + BOX_SIZE), zColor);
    }
#endif
}

GizmoAxis TransformGizmo::intersectTest(const Vec2& mousePos, const Vec2& screenPos,
                                        ImVec2 endX, ImVec2 endY, ImVec2 endZ,
                                        float handleLen, EditorContext::GizmoMode mode, bool zDimmed) {
    float centerDist = std::sqrt((mousePos.x - screenPos.x) * (mousePos.x - screenPos.x) +
                                 (mousePos.y - screenPos.y) * (mousePos.y - screenPos.y));
    if (centerDist < HOVER_THRESHOLD) {
        return GizmoAxis::Center;
    }

    Vec2 sp(screenPos.x, screenPos.y);

    if (pointToLineDistance(mousePos, sp, Vec2(endX.x, endX.y)) < HOVER_THRESHOLD) {
        return GizmoAxis::X;
    }

    if (pointToLineDistance(mousePos, sp, Vec2(endY.x, endY.y)) < HOVER_THRESHOLD) {
        return GizmoAxis::Y;
    }

    if (!zDimmed && mode != EditorContext::GizmoMode::None) {
        if (pointToLineDistance(mousePos, sp, Vec2(endZ.x, endZ.y)) < HOVER_THRESHOLD) {
            return GizmoAxis::Z;
        }
    }

    return GizmoAxis::None;
}

void TransformGizmo::applyTranslate(ECS::World& world, ECS::Entity entity,
                                     Vec3 newWorldPos, bool snapEnabled, float snapInterval) {
    if (snapEnabled && snapInterval > 0.f) {
        newWorldPos.x = applySnap(newWorldPos.x, snapInterval);
        newWorldPos.y = applySnap(newWorldPos.y, snapInterval);
        newWorldPos.z = applySnap(newWorldPos.z, snapInterval);
    }
    auto* transform = world.get<ECS::Transform>(entity);
    if (transform) { transform->position = newWorldPos; return; }
    auto* pos3D = world.get<ECS::Position3D>(entity);
    if (pos3D) { pos3D->position = newWorldPos; }
}

void TransformGizmo::applyRotate(ECS::World& world, ECS::Entity entity, float totalDeltaX, bool snapEnabled) {
    float angle = m_entityStartRotZ + totalDeltaX * 0.01f;
    if (snapEnabled) {
        float snapRad = m_snapRotate * 3.14159265f / 180.0f;
        angle = applySnap(angle, snapRad);
    }
    auto* transform = world.get<ECS::Transform>(entity);
    if (transform) { transform->rotation.z = angle; }
}

void TransformGizmo::applyScale(ECS::World& world, ECS::Entity entity, const Vec2& screenDelta,
                                GizmoAxis axis, bool snapEnabled, float zoom) {
    float pixelsPerUnit = zoom * 50.0f;
    float deltaX = screenDelta.x / pixelsPerUnit;
    float deltaY = -screenDelta.y / pixelsPerUnit;

    auto* transform = world.get<ECS::Transform>(entity);
    if (transform) {
        float factor = 1.0f;
        if (axis == GizmoAxis::X || axis == GizmoAxis::Center || axis == GizmoAxis::None) {
            factor = 1.0f + deltaX * 0.5f;
        }
        if (axis == GizmoAxis::Y || axis == GizmoAxis::Center || axis == GizmoAxis::None) {
            float yFactor = 1.0f + deltaY * 0.5f;
            factor = axis == GizmoAxis::Center ? (factor + yFactor) * 0.5f : yFactor;
        }

        if (axis == GizmoAxis::X || axis == GizmoAxis::Center || axis == GizmoAxis::None) {
            float newX = transform->scale.x * factor;
            if (snapEnabled) newX = applySnap(newX, m_snapScale);
            transform->scale.x = std::max(0.01f, newX);
        }
        if (axis == GizmoAxis::Y || axis == GizmoAxis::Center || axis == GizmoAxis::None) {
            float newY = transform->scale.y * factor;
            if (snapEnabled) newY = applySnap(newY, m_snapScale);
            transform->scale.y = std::max(0.01f, newY);
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
static float pointToLineDistance(const Vec2& point, const Vec2& lineStart, const Vec2& lineEnd) {
    Vec2 lineDir = lineEnd - lineStart;
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