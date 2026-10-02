#pragma once

#include "editor/EditorContext.hpp"
#include "editor/SceneViewport.hpp"

#ifdef CF_HAS_IMGUI
#include "ecs/MeshGeometry.hpp"
#include "ecs/TerrainComponents.hpp"
#include <imgui.h>
#endif

namespace Caffeine::Editor {

class ToolboxPanel {
public:
    void open() { m_open = true; }
    void close() { m_open = false; }
    bool isOpen() const { return m_open; }

#ifdef CF_HAS_IMGUI
    void render(ECS::World& world, EditorContext& ctx, SceneViewport& viewport) {
        if (!m_open) return;
        if (!ImGui::Begin("Toolbox", &m_open)) {
            ImGui::End();
            return;
        }

        ImGui::SeparatorText("Transform");
        auto modeButton = [&](const char* label, EditorContext::GizmoMode mode) {
            const bool active = ctx.gizmoMode == mode;
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.85f, 1.0f));
            if (ImGui::Button(label, ImVec2(72.0f, 0.0f))) {
                ctx.gizmoMode = mode;
                ctx.meshElementMode = EditorContext::MeshElementMode::Object;
                ctx.meshElementSelection.clear();
            }
            if (active) ImGui::PopStyleColor();
        };
        modeButton("Move", EditorContext::GizmoMode::Translate);
        ImGui::SameLine();
        modeButton("Rotate", EditorContext::GizmoMode::Rotate);
        ImGui::SameLine();
        modeButton("Scale", EditorContext::GizmoMode::Scale);
        const bool local = ctx.gizmoSpace == EditorContext::GizmoSpace::Local;
        if (ImGui::Button(local ? "Local axes" : "World axes", ImVec2(-1.0f, 0.0f))) {
            ctx.gizmoSpace = local ? EditorContext::GizmoSpace::World
                                   : EditorContext::GizmoSpace::Local;
        }
        ImGui::TextDisabled("T move   E rotate   R scale");

        ImGui::SeparatorText("Geometry");
        auto elementButton = [&](const char* label, EditorContext::MeshElementMode mode) {
            const bool active = ctx.meshElementMode == mode;
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.55f, 0.15f, 1.0f));
            if (ImGui::Button(label, ImVec2(72.0f, 0.0f))) {
                ctx.meshElementMode = mode;
                ctx.meshElementSelection.clear();
                if (mode != EditorContext::MeshElementMode::Object && ctx.selectedEntity.isValid()) {
                    ECS::bakeMeshGeometry(world, ctx.selectedEntity, ctx.projectRootPath.string());
                }
            }
            if (active) ImGui::PopStyleColor();
        };
        elementButton("Object", EditorContext::MeshElementMode::Object);
        ImGui::SameLine();
        elementButton("Vertex", EditorContext::MeshElementMode::Vertex);
        ImGui::SameLine();
        elementButton("Edge", EditorContext::MeshElementMode::Edge);
        ImGui::SameLine();
        elementButton("Face", EditorContext::MeshElementMode::Face);
        ImGui::TextDisabled("Select an element, then drag the gizmo.");

        ImGui::SeparatorText("View");
        auto viewButton = [&](const char* label, EditorContext::ViewMode mode) {
            const bool active = ctx.viewMode == mode;
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.85f, 1.0f));
            if (ImGui::Button(label, ImVec2(72.0f, 0.0f))) ctx.viewMode = mode;
            if (active) ImGui::PopStyleColor();
        };
        viewButton("2D", EditorContext::ViewMode::Mode2D);
        ImGui::SameLine();
        viewButton("3D", EditorContext::ViewMode::Mode3D);
        ImGui::SameLine();
        viewButton("Iso", EditorContext::ViewMode::Isometric);
        const bool perspective = viewport.projectionMode() == SceneViewport::ProjectionMode::Perspective;
        if (ImGui::Button(perspective ? "Perspective" : "Orthographic", ImVec2(-1.0f, 0.0f))) {
            viewport.toggleProjectionMode();
        }

        ImGui::SeparatorText("Viewport");
        if (ImGui::Checkbox("Snap to grid", &ctx.snapToGrid)) {}
        ImGui::SliderFloat("Grid size", &ctx.snapGridSize, 0.1f, 10.0f, "%.2f");
        if (ImGui::Checkbox("Physics shapes", &ctx.physicsDebugVisible)) {}
        const bool textured = viewport.meshPreviewMode() == SceneViewport::MeshPreviewMode::Textured;
        if (ImGui::Button(textured ? "Textured" : "Wireframe", ImVec2(-1.0f, 0.0f))) {
            viewport.setMeshPreviewMode(textured ? SceneViewport::MeshPreviewMode::Wireframe
                                                 : SceneViewport::MeshPreviewMode::Textured);
        }
        if (!textured) {
            int density = static_cast<int>(viewport.wireframeDensity());
            const char* labels[] = {"Low", "Medium", "High"};
            if (ImGui::Combo("Density", &density, labels, 3)) {
                viewport.setWireframeDensity(static_cast<SceneViewport::WireframeDensity>(density));
            }
        }

        if (ctx.selectedEntity.isValid() && world.has<ECS::TerrainComponent>(ctx.selectedEntity)) {
            ImGui::SeparatorText("Terrain");
            auto terrainButton = [&](const char* label, bool active) {
                if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.65f, 0.35f, 1.0f));
                const bool pressed = ImGui::Button(label);
                if (active) ImGui::PopStyleColor();
                return pressed;
            };
            if (terrainButton("Sculpt", ctx.terrainEditMode == EditorContext::TerrainEditMode::Sculpt)) {
                ctx.terrainEditMode = (ctx.terrainEditMode == EditorContext::TerrainEditMode::Sculpt)
                    ? EditorContext::TerrainEditMode::None
                    : EditorContext::TerrainEditMode::Sculpt;
            }
            ImGui::SameLine();
            if (terrainButton("Paint", ctx.terrainEditMode == EditorContext::TerrainEditMode::Splat)) {
                ctx.terrainEditMode = (ctx.terrainEditMode == EditorContext::TerrainEditMode::Splat)
                    ? EditorContext::TerrainEditMode::None
                    : EditorContext::TerrainEditMode::Splat;
            }
            if (ctx.terrainEditMode == EditorContext::TerrainEditMode::Sculpt) {
                if (terrainButton("Raise", ctx.terrainBrushMode == EditorContext::TerrainBrushMode::Raise))
                    ctx.terrainBrushMode = EditorContext::TerrainBrushMode::Raise;
                ImGui::SameLine();
                if (terrainButton("Lower", ctx.terrainBrushMode == EditorContext::TerrainBrushMode::Lower))
                    ctx.terrainBrushMode = EditorContext::TerrainBrushMode::Lower;
                ImGui::SameLine();
                if (terrainButton("Smooth", ctx.terrainBrushMode == EditorContext::TerrainBrushMode::Smooth))
                    ctx.terrainBrushMode = EditorContext::TerrainBrushMode::Smooth;
            }
            ImGui::SliderFloat("Radius", &ctx.terrainBrushRadius, 0.5f, 64.0f, "%.1f");
            ImGui::SliderFloat("Strength", &ctx.terrainBrushStrength, 0.01f, 1.0f, "%.2f");
        }

        ImGui::End();
    }
#else
    void render(ECS::World&, EditorContext&, SceneViewport&) {}
#endif

private:
    bool m_open = true;
};

}  // namespace Caffeine::Editor
