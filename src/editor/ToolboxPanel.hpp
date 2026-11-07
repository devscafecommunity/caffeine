#pragma once

#include "caffeine/ui/Widgets.hpp"
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
        if (!UIKit::BeginPanel("Toolbox", &m_open)) {
            ImGui::End();
            return;
        }

        auto segmentWidth = [](int count) {
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float avail = ImGui::GetContentRegionAvail().x;
            return (avail - spacing * static_cast<float>(count - 1)) /
                   static_cast<float>(count);
        };

        // ── Geometry ────────────────────────────────────────────────
        UIKit::SectionLabel("Geometry");
        {
            const float w = segmentWidth(4);
            auto elementButton = [&](const char* label, EditorContext::MeshElementMode mode) {
                if (UIKit::SegmentedButton(label, ctx.meshElementMode == mode,
                                           ImVec2(w, 0))) {
                    ctx.meshElementMode = mode;
                    ctx.meshElementSelection.clear();
                    if (mode != EditorContext::MeshElementMode::Object &&
                        ctx.selectedEntity.isValid()) {
                        ECS::bakeMeshGeometry(world, ctx.selectedEntity,
                                              ctx.projectRootPath.string());
                    }
                }
            };
            elementButton("Object", EditorContext::MeshElementMode::Object);
            ImGui::SameLine();
            elementButton("Vertex", EditorContext::MeshElementMode::Vertex);
            ImGui::SameLine();
            elementButton("Edge", EditorContext::MeshElementMode::Edge);
            ImGui::SameLine();
            elementButton("Face", EditorContext::MeshElementMode::Face);
        }
        UIKit::TextMuted("Select an element, then drag the gizmo.");

        // ── Viewport ────────────────────────────────────────────────
        UIKit::SectionLabel("Viewport");
        if (UIKit::BeginPropertyGrid("toolbox_viewport", 120.0f)) {
            UIKit::SliderFloatProperty("Grid size", &ctx.snapGridSize, 0.1f, 10.0f, "%.2f");
            UIKit::CheckboxProperty("Physics shapes", &ctx.physicsDebugVisible);
            UIKit::CheckboxProperty("Bones", &ctx.showBones);
            UIKit::EndPropertyGrid();
        }

        const bool textured =
            viewport.meshPreviewMode() == SceneViewport::MeshPreviewMode::Textured;
        if (UIKit::ToolbarButton(textured ? "Textured" : "Wireframe",
                                 "Toggle mesh preview mode", false, ImVec2(-1.0f, 0.0f))) {
            viewport.setMeshPreviewMode(textured ? SceneViewport::MeshPreviewMode::Wireframe
                                                 : SceneViewport::MeshPreviewMode::Textured);
        }
        if (!textured) {
            int density = static_cast<int>(viewport.wireframeDensity());
            const char* labels[] = {"Low", "Medium", "High"};
            if (UIKit::BeginPropertyGrid("toolbox_wireframe", 120.0f)) {
                if (UIKit::ComboProperty("Density", &density, labels, 3)) {
                    viewport.setWireframeDensity(
                        static_cast<SceneViewport::WireframeDensity>(density));
                }
                UIKit::EndPropertyGrid();
            }
        }

        // ── Terrain ─────────────────────────────────────────────────
        if (ctx.selectedEntity.isValid() &&
            world.has<ECS::TerrainComponent>(ctx.selectedEntity)) {
            UIKit::SectionLabel("Terrain");
            const float w = segmentWidth(2);
            if (UIKit::SegmentedButton(
                    "Sculpt",
                    ctx.terrainEditMode == EditorContext::TerrainEditMode::Sculpt,
                    ImVec2(w, 0))) {
                ctx.terrainEditMode =
                    (ctx.terrainEditMode == EditorContext::TerrainEditMode::Sculpt)
                        ? EditorContext::TerrainEditMode::None
                        : EditorContext::TerrainEditMode::Sculpt;
            }
            ImGui::SameLine();
            if (UIKit::SegmentedButton(
                    "Paint", ctx.terrainEditMode == EditorContext::TerrainEditMode::Splat,
                    ImVec2(w, 0))) {
                ctx.terrainEditMode =
                    (ctx.terrainEditMode == EditorContext::TerrainEditMode::Splat)
                        ? EditorContext::TerrainEditMode::None
                        : EditorContext::TerrainEditMode::Splat;
            }

            if (ctx.terrainEditMode == EditorContext::TerrainEditMode::Sculpt) {
                const float bw = segmentWidth(3);
                if (UIKit::SegmentedButton(
                        "Raise", ctx.terrainBrushMode == EditorContext::TerrainBrushMode::Raise,
                        ImVec2(bw, 0))) {
                    ctx.terrainBrushMode = EditorContext::TerrainBrushMode::Raise;
                }
                ImGui::SameLine();
                if (UIKit::SegmentedButton(
                        "Lower", ctx.terrainBrushMode == EditorContext::TerrainBrushMode::Lower,
                        ImVec2(bw, 0))) {
                    ctx.terrainBrushMode = EditorContext::TerrainBrushMode::Lower;
                }
                ImGui::SameLine();
                if (UIKit::SegmentedButton(
                        "Smooth",
                        ctx.terrainBrushMode == EditorContext::TerrainBrushMode::Smooth,
                        ImVec2(bw, 0))) {
                    ctx.terrainBrushMode = EditorContext::TerrainBrushMode::Smooth;
                }
            }

            if (UIKit::BeginPropertyGrid("toolbox_terrain", 120.0f)) {
                UIKit::SliderFloatProperty("Radius", &ctx.terrainBrushRadius, 0.5f, 64.0f,
                                           "%.1f");
                UIKit::SliderFloatProperty("Strength", &ctx.terrainBrushStrength, 0.01f, 1.0f,
                                           "%.2f");
                UIKit::EndPropertyGrid();
            }
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
