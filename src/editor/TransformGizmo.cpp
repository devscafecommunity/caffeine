#include "editor/TransformGizmo.hpp"
#include "editor/SceneViewport.hpp"
#include "ecs/Components.hpp"
#include "ecs/Components3D.hpp"
#include <algorithm>
#include <cmath>

namespace Caffeine::Editor {

TransformGizmo::Ray3D TransformGizmo::screenToWorldRay(
    const Vec2& screenPos, const Mat4& vpInverse, 
    const ImVec2& viewportSize, const Vec3& camPos) {
    
    // Convert screen coords to NDC [-1, 1]
    f32 ndcX = (2.0f * screenPos.x) / viewportSize.x - 1.0f;
    f32 ndcY = 1.0f - (2.0f * screenPos.y) / viewportSize.y;
    
    // Near plane point in NDC (z = -1 in OpenGL convention)
    Vec4 ndcNear(ndcX, ndcY, -1.0f, 1.0f);
    
    // Unproject to world via vpInverse
    Vec4 worldNear = vpInverse.transformVec4(ndcNear);
    
    // Perspective divide (CRITICAL: must normalize by w)
    if (std::abs(worldNear.w) > 0.0001f) {
        worldNear.x /= worldNear.w;
        worldNear.y /= worldNear.w;
        worldNear.z /= worldNear.w;
    }
    
    Vec3 rayOrigin = camPos;
    Vec3 rayTarget(worldNear.x, worldNear.y, worldNear.z);
    Vec3 rayDir = (rayTarget - rayOrigin).normalized();
    
    return Ray3D{rayOrigin, rayDir};
}

TransformGizmo::RayAxisTest TransformGizmo::rayToAxisSegmentDistance(
    const Ray3D& ray, 
    const Vec3& axisOrigin, const Vec3& axisDir, f32 axisLength) {
    
    // Vector from ray origin to axis origin
    Vec3 w = axisOrigin - ray.origin;
    
    // Solve for closest points:
    // ray(t) = rayOrigin + t * rayDir
    // axis(s) = axisOrigin + s * axisDir, s ∈ [0, axisLength]
    // Minimize: |ray(t) - axis(s)|²
    
    f32 a = ray.direction.dot(ray.direction);     // |rayDir|² = 1 (normalized)
    f32 b = ray.direction.dot(axisDir);
    f32 c = axisDir.dot(axisDir);
    f32 d = ray.direction.dot(w);
    f32 e = axisDir.dot(w);
    
    f32 denom = a * c - b * b;
    f32 t_ray = 0.0f;
    f32 s_axis = 0.0f;
    
    if (std::abs(denom) > 1e-6f) {
        t_ray = (b * e - c * d) / denom;
        s_axis = (a * e - b * d) / denom;
    } else {
        // Rays are parallel; use perpendicular distance
        s_axis = (std::abs(c) > 1e-6f) ? (e / c) : 0.0f;
    }
    
    // Clamp t_ray to positive direction (ray goes forward)
    t_ray = std::max(0.0f, t_ray);
    
    // CRITICAL: Clamp s_axis to axis segment bounds [0, axisLength]
    s_axis = std::max(0.0f, std::min(axisLength, s_axis));
    
    // Compute closest points
    Vec3 closestOnRay = ray.origin + ray.direction * t_ray;
    Vec3 closestOnAxis = axisOrigin + axisDir * s_axis;
    
    // Distance between them
    f32 distance = (closestOnRay - closestOnAxis).length();
    
    return RayAxisTest{distance, t_ray, s_axis};
}

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
    {
        Vec3 wp0 = transform->position;
        Vec3 diff = Vec3(wp0.x - ctx.camFocus.x, wp0.y - ctx.camFocus.y, wp0.z - ctx.camFocus.z);
        float distToObj = std::sqrt(diff.dot(diff)) + ctx.camDistance;
        float fov = 1.0472f;
        float pixelsPerUnit = (vpSize.y * 0.5f) / (distToObj * std::tan(fov * 0.5f));
        handleWorld = handleLen / std::max(pixelsPerUnit, 0.01f);
    }

    // vx   = cosY*ax + sinY*az          (screen-right component)
    // vy2  = cosP*ay + sinP*(-sinY*ax + cosY*az)  (screen-up component)
    // vz2  = -sinP*ay + cosP*(-sinY*ax + cosY*az) (depth: positive = behind camera)
    // sdx  = vx,  sdy = -vy2
    // smag = length of (sdx,sdy) = foreshortening factor (1=perpendicular, 0=parallel to view)
    struct AxisInfo { ImVec2 end; float depth; float alpha; bool collapsed; };

    auto worldAxisToScreen = [&](float ax, float ay, float az) -> AxisInfo {
        if (ctx.viewMode != EditorContext::ViewMode::Mode3D) {
            Vec3 wp2 = transform->position;
            ImVec2 raw = SceneViewport::projectToScreen({wp2.x + ax*handleWorld, wp2.y + ay*handleWorld, wp2.z + az*handleWorld}, vpMin, vpSize, ctx);
            float dx = raw.x - sp2.x, dy = raw.y - sp2.y;
            float d  = std::sqrt(dx*dx + dy*dy);
            if (d < 3.0f) return {sp2, 0.f, 1.f, true};
            return {ImVec2(sp2.x + dx/d * handleLen, sp2.y + dy/d * handleLen), 0.f, 1.f, false};
        }
        float sinY = std::sin(ctx.camYaw),  cosY = std::cos(ctx.camYaw);
        float sinP = std::sin(ctx.camPitch), cosP = std::cos(ctx.camPitch);
        float vx   = cosY * ax + sinY * az;
        float vy   = ay;
        float vzc  = -sinY * ax + cosY * az;
        float vy2  = cosP * vy + sinP * vzc;
        float vz2  = -sinP * vy + cosP * vzc;
        float sdx  = vx, sdy = -vy2;
        float smag = std::sqrt(sdx*sdx + sdy*sdy);
        float len   = handleLen * std::max(smag, 0.4f);
        float alpha = 0.4f + 0.6f * smag;
        if (smag < 0.001f)
            return {sp2, vz2, alpha, true};
        return {ImVec2(sp2.x + sdx/smag * len, sp2.y + sdy/smag * len), vz2, alpha, false};
    };

    AxisInfo axX = worldAxisToScreen(1.f, 0.f, 0.f);
    AxisInfo axY = worldAxisToScreen(0.f, 1.f, 0.f);
    AxisInfo axZ = worldAxisToScreen(0.f, 0.f, 1.f);

    ImVec2 endX = axX.end, endY = axY.end, endZ = axZ.end;
    bool xCollapsed = axX.collapsed, yCollapsed = axY.collapsed, zCollapsed = axZ.collapsed;
    float alphaX = axX.alpha, alphaY = axY.alpha, alphaZ = axZ.alpha;

    ImVec2 mousePos = ImGui::GetMousePos();
    bool mouseInViewport = (mousePos.x >= vpMin.x && mousePos.x <= vpMax.x &&
                            mousePos.y >= vpMin.y && mousePos.y <= vpMax.y);

        if (mouseInViewport) {
        struct DrawAxis { int idx; float depth; };
        DrawAxis order[3] = {{0, axX.depth}, {1, axY.depth}, {2, axZ.depth}};
        std::sort(order, order+3, [](const DrawAxis& a, const DrawAxis& b){ return a.depth > b.depth; });
        auto sortedEnd   = [&](int i) -> ImVec2   { return i==0?endX : i==1?endY : endZ; };
        auto sortedAlpha = [&](int i) -> float    { return i==0?alphaX : i==1?alphaY : alphaZ; };
        auto sortedCol   = [&](int i) -> bool     { return i==0?xCollapsed : i==1?yCollapsed : zCollapsed; };

        switch (ctx.gizmoMode) {
            case EditorContext::GizmoMode::Translate:
                renderTranslate3D(screenPos,
                    sortedEnd(order[0].idx), sortedEnd(order[1].idx), sortedEnd(order[2].idx),
                    zDimmed,
                    sortedCol(order[0].idx), sortedCol(order[1].idx), sortedCol(order[2].idx),
                    sortedAlpha(order[0].idx), sortedAlpha(order[1].idx), sortedAlpha(order[2].idx),
                    order[0].idx, order[1].idx, order[2].idx);
                break;
            case EditorContext::GizmoMode::Rotate:
                renderRotate3D(screenPos, handleLen, zDimmed);
                break;
            case EditorContext::GizmoMode::Scale:
                renderScale3D(screenPos,
                    sortedEnd(order[0].idx), sortedEnd(order[1].idx), sortedEnd(order[2].idx),
                    zDimmed,
                    sortedCol(order[0].idx), sortedCol(order[1].idx), sortedCol(order[2].idx),
                    sortedAlpha(order[0].idx), sortedAlpha(order[1].idx), sortedAlpha(order[2].idx),
                    order[0].idx, order[1].idx, order[2].idx);
                break;
            default: break;
        }

        if (ImGui::IsWindowFocused()) {
            Vec2 mousePosGlm(mousePos.x, mousePos.y);
            
            // Build VP matrix for raycasting
            f32 sinY = std::sin(ctx.camYaw), cosY = std::cos(ctx.camYaw);
            f32 sinP = std::sin(ctx.camPitch), cosP = std::cos(ctx.camPitch);
            Vec3 camPos = ctx.camFocus + Vec3(sinY * cosP, -sinP, -cosY * cosP) * ctx.camDistance;
            Mat4 view = Mat4::lookAt(camPos, ctx.camFocus, Vec3(0.0f, 1.0f, 0.0f));
            f32 aspect = vpSize.x / std::max(vpSize.y, 1.0f);
            Mat4 proj = Mat4::perspective(1.0472f, aspect, 0.1f, 10000.0f);
            Mat4 vp = proj * view;
            Mat4 vpInverse = vp.inverted();

            // World-space axis vectors (from entity origin, in world units)
            Vec3 entityPos = transform->position;
            Vec3 worldAxisX = entityPos + Vec3(handleWorld, 0.0f, 0.0f);
            Vec3 worldAxisY = entityPos + Vec3(0.0f, handleWorld, 0.0f);
            Vec3 worldAxisZ = entityPos + Vec3(0.0f, 0.0f, handleWorld);

            // Perform raycasting intersection test
            m_hoveredAxis = intersectTest(
                mousePosGlm, vpInverse, camPos, vpSize,
                worldAxisX, worldAxisY, worldAxisZ, handleWorld,
                ctx.gizmoMode, zDimmed, xCollapsed, yCollapsed, zCollapsed);

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

void TransformGizmo::renderTranslate3D(const Vec2& screenPos,
    ImVec2 end0, ImVec2 end1, ImVec2 end2, bool zDimmed,
    bool col0, bool col1, bool col2,
    float alpha0, float alpha1, float alpha2,
    int axis0, int axis1, int axis2) {
#ifdef CF_HAS_IMGUI
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 sp(screenPos.x, screenPos.y);

    auto withAlpha = [](u32 base, float a) -> u32 {
        return (base & 0x00FFFFFFu) | (static_cast<u32>(std::min(a, 1.f) * 255.f) << 24);
    };
    auto axisBaseColor = [&](int idx) -> u32 {
        if (idx == 0) return (m_hoveredAxis==GizmoAxis::X||m_dragAxis==GizmoAxis::X) ? COLOR_HOVERED : COLOR_X_AXIS;
        if (idx == 1) return (m_hoveredAxis==GizmoAxis::Y||m_dragAxis==GizmoAxis::Y) ? COLOR_HOVERED : COLOR_Y_AXIS;
        return (m_hoveredAxis==GizmoAxis::Z||m_dragAxis==GizmoAxis::Z) ? COLOR_HOVERED : COLOR_Z_AXIS;
    };

    auto drawAxisArrow = [&](ImVec2 to, u32 color, bool collapsed) {
        if (collapsed) {
            dl->AddCircle(sp, ARROW_SIZE * 0.6f, color, 12, AXIS_LINE_WIDTH * 0.8f);
            return;
        }
        dl->AddLine(sp, to, color, AXIS_LINE_WIDTH);
        float dx = to.x - sp.x, dy = to.y - sp.y;
        float len = std::sqrt(dx*dx + dy*dy);
        if (len < 0.001f) return;
        float ux = dx/len, uy = dy/len;
        float nx = -uy*ARROW_SIZE*0.6f, ny = ux*ARROW_SIZE*0.6f;
        ImVec2 tip(to.x + ux*ARROW_SIZE, to.y + uy*ARROW_SIZE);
        dl->AddTriangleFilled(tip, ImVec2(to.x+nx,to.y+ny), ImVec2(to.x-nx,to.y-ny), color);
    };

    int axes[3]   = {axis0, axis1, axis2};
    ImVec2 ends[3]= {end0, end1, end2};
    bool   cols[3]= {col0, col1, col2};
    float  alps[3]= {alpha0, alpha1, alpha2};

    for (int i = 0; i < 3; ++i) {
        int idx = axes[i];
        if (idx == 2 && zDimmed) continue;
        u32 color = withAlpha(axisBaseColor(idx), alps[i]);
        drawAxisArrow(ends[i], color, cols[i]);
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

void TransformGizmo::renderScale3D(const Vec2& screenPos,
    ImVec2 end0, ImVec2 end1, ImVec2 end2, bool zDimmed,
    bool col0, bool col1, bool col2,
    float alpha0, float alpha1, float alpha2,
    int axis0, int axis1, int axis2) {
#ifdef CF_HAS_IMGUI
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 sp(screenPos.x, screenPos.y);

    auto withAlpha = [](u32 base, float a) -> u32 {
        return (base & 0x00FFFFFFu) | (static_cast<u32>(std::min(a, 1.f) * 255.f) << 24);
    };
    auto axisBaseColor = [&](int idx) -> u32 {
        if (idx == 0) return (m_hoveredAxis==GizmoAxis::X||m_dragAxis==GizmoAxis::X) ? COLOR_HOVERED : COLOR_X_AXIS;
        if (idx == 1) return (m_hoveredAxis==GizmoAxis::Y||m_dragAxis==GizmoAxis::Y) ? COLOR_HOVERED : COLOR_Y_AXIS;
        return (m_hoveredAxis==GizmoAxis::Z||m_dragAxis==GizmoAxis::Z) ? COLOR_HOVERED : COLOR_Z_AXIS;
    };

    auto drawAxisScale = [&](ImVec2 to, u32 color, bool collapsed) {
        if (collapsed) {
            dl->AddRect(ImVec2(sp.x-BOX_SIZE, sp.y-BOX_SIZE),
                        ImVec2(sp.x+BOX_SIZE, sp.y+BOX_SIZE), color, 0.f, 0, AXIS_LINE_WIDTH*0.8f);
            return;
        }
        dl->AddLine(sp, to, color, AXIS_LINE_WIDTH);
        dl->AddRectFilled(ImVec2(to.x-BOX_SIZE, to.y-BOX_SIZE),
                          ImVec2(to.x+BOX_SIZE, to.y+BOX_SIZE), color);
    };

    int axes[3]   = {axis0, axis1, axis2};
    ImVec2 ends[3]= {end0, end1, end2};
    bool   cols[3]= {col0, col1, col2};
    float  alps[3]= {alpha0, alpha1, alpha2};

    for (int i = 0; i < 3; ++i) {
        int idx = axes[i];
        if (idx == 2 && zDimmed) continue;
        u32 color = withAlpha(axisBaseColor(idx), alps[i]);
        drawAxisScale(ends[i], color, cols[i]);
    }
#endif
}

GizmoAxis TransformGizmo::intersectTest(
    const Vec2& mousePos, 
    const Mat4& vpInverse, const Vec3& camPos,
    const ImVec2& viewportSize,
    const Vec3& axisX, const Vec3& axisY, const Vec3& axisZ,
    f32 axisLength,
    EditorContext::GizmoMode mode, bool zDimmed,
    bool xCollapsed, bool yCollapsed, bool zCollapsed) {
    
    // Test center first (close proximity in world units ~0.2f)
    Vec3 gizmoOrigin = axisX; // All axes share same origin point
    Ray3D ray = screenToWorldRay(mousePos, vpInverse, viewportSize, camPos);
    
    Vec3 toOrigin = gizmoOrigin - ray.origin;
    f32 distToOrigin = (toOrigin - ray.direction * toOrigin.dot(ray.direction)).length();
    if (distToOrigin < 0.2f) {
        return GizmoAxis::Center;
    }
    
    // World-space threshold for axis picking (0.05f units)
    const f32 AXIS_THRESHOLD = 0.05f;
    
    // Test X axis
    if (!xCollapsed) {
        Vec3 axisDir = axisX.normalized();
        RayAxisTest testX = rayToAxisSegmentDistance(ray, axisX, axisDir, axisLength);
        if (testX.distance < AXIS_THRESHOLD) {
            return GizmoAxis::X;
        }
    }
    
    // Test Y axis
    if (!yCollapsed) {
        Vec3 axisDir = axisY.normalized();
        RayAxisTest testY = rayToAxisSegmentDistance(ray, axisY, axisDir, axisLength);
        if (testY.distance < AXIS_THRESHOLD) {
            return GizmoAxis::Y;
        }
    }
    
    // Test Z axis (respect zDimmed in 2D mode)
    if (!zDimmed && !zCollapsed && mode != EditorContext::GizmoMode::None) {
        Vec3 axisDir = axisZ.normalized();
        RayAxisTest testZ = rayToAxisSegmentDistance(ray, axisZ, axisDir, axisLength);
        if (testZ.distance < AXIS_THRESHOLD) {
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