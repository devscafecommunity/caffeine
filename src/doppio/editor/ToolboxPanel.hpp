#pragma once

#include "caffeine/ui/PanelRegistry.hpp"
#include "caffeine/ui/UI.hpp"
#include "scene/EditorContext.hpp"
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

        using namespace Caffeine::UIKit;
        using namespace Caffeine::UIKit::Elements;

        Panel root("Toolbox", "Editing tools", "align-left", &m_open);
        UIContext ui;

        auto addSegment = [](Row& row, const char* label, bool selected,
                             std::function<void()> onClick) {
            auto& button = row.emplace<Button>(label, ButtonVariant::Segmented);
            button.setGrow(true);
            button.setSelected(selected);
            button.setOnClick(std::move(onClick));
        };

        auto& geometry = root.emplace<Elements::Section>("Geometry");
        auto& geometryRow = geometry.emplace<Row>("toolbox_geometry_modes");
        auto setMode = [&world, &ctx](EditorContext::MeshElementMode mode) {
            ctx.meshElementMode = mode;
            ctx.meshElementSelection.clear();
            if (mode != EditorContext::MeshElementMode::Object &&
                ctx.selectedEntity.isValid()) {
                ECS::bakeMeshGeometry(world, ctx.selectedEntity,
                                      ctx.projectRootPath.string());
            }
        };
        addSegment(geometryRow, "Object",
                   ctx.meshElementMode == EditorContext::MeshElementMode::Object,
                   [setMode] { setMode(EditorContext::MeshElementMode::Object); });
        addSegment(geometryRow, "Vertex",
                   ctx.meshElementMode == EditorContext::MeshElementMode::Vertex,
                   [setMode] { setMode(EditorContext::MeshElementMode::Vertex); });
        addSegment(geometryRow, "Edge",
                   ctx.meshElementMode == EditorContext::MeshElementMode::Edge,
                   [setMode] { setMode(EditorContext::MeshElementMode::Edge); });
        addSegment(geometryRow, "Face",
                   ctx.meshElementMode == EditorContext::MeshElementMode::Face,
                   [setMode] { setMode(EditorContext::MeshElementMode::Face); });
        geometry.emplace<Label>("Select an element, then drag the gizmo.",
                                TextTone::Muted)
            .setWrap(true);

        auto& view = root.emplace<Elements::Section>("Viewport");
        auto& viewGrid = view.emplace<PropertyGrid>("toolbox_viewport");
        viewGrid.typedRow<FloatProperty>("Grid size", ctx.snapGridSize, 0.1f, 10.0f, 0.01f,
                                         "%.2f", true);
        viewGrid.typedRow<BoolProperty>("Physics shapes", ctx.physicsDebugVisible);
        viewGrid.typedRow<BoolProperty>("Bones", ctx.showBones);

        const bool textured =
            viewport.meshPreviewMode() == SceneViewport::MeshPreviewMode::Textured;
        auto& previewRow = view.emplace<Row>("toolbox_preview_row");
        auto& preview = previewRow.emplace<Button>(textured ? "Textured" : "Wireframe");
        preview.setGrow(true);
        preview.setTooltip("Toggle mesh preview mode");
        preview.setOnClick([&viewport, textured] {
            viewport.setMeshPreviewMode(
                textured ? SceneViewport::MeshPreviewMode::Wireframe
                         : SceneViewport::MeshPreviewMode::Textured);
        });
        if (!textured) {
            m_wireframeDensity = static_cast<int>(viewport.wireframeDensity());
            auto& densityGrid = view.emplace<PropertyGrid>("toolbox_wireframe");
            densityGrid
                .typedRow<ChoiceProperty>("Density", m_wireframeDensity,
                                           std::vector<std::string>{"Low", "Medium", "High"})
                .setOnChange([&viewport](int value) {
                    viewport.setWireframeDensity(
                        static_cast<SceneViewport::WireframeDensity>(value));
                });
        }

        if (ctx.selectedEntity.isValid() &&
            world.has<ECS::TerrainComponent>(ctx.selectedEntity)) {
            auto& terrain = root.emplace<Elements::Section>("Terrain");
            auto& modeRow = terrain.emplace<Row>("toolbox_terrain_modes");
            addSegment(modeRow, "Sculpt",
                       ctx.terrainEditMode == EditorContext::TerrainEditMode::Sculpt,
                       [&ctx] {
                           ctx.terrainEditMode =
                               (ctx.terrainEditMode == EditorContext::TerrainEditMode::Sculpt)
                                   ? EditorContext::TerrainEditMode::None
                                   : EditorContext::TerrainEditMode::Sculpt;
                       });
            addSegment(modeRow, "Paint",
                       ctx.terrainEditMode == EditorContext::TerrainEditMode::Splat,
                       [&ctx] {
                           ctx.terrainEditMode =
                               (ctx.terrainEditMode == EditorContext::TerrainEditMode::Splat)
                                   ? EditorContext::TerrainEditMode::None
                                   : EditorContext::TerrainEditMode::Splat;
                       });

            if (ctx.terrainEditMode == EditorContext::TerrainEditMode::Sculpt) {
                auto& brushRow = terrain.emplace<Row>("toolbox_terrain_brushes");
                addSegment(brushRow, "Raise",
                           ctx.terrainBrushMode == EditorContext::TerrainBrushMode::Raise,
                           [&ctx] {
                               ctx.terrainBrushMode = EditorContext::TerrainBrushMode::Raise;
                           });
                addSegment(brushRow, "Lower",
                           ctx.terrainBrushMode == EditorContext::TerrainBrushMode::Lower,
                           [&ctx] {
                               ctx.terrainBrushMode = EditorContext::TerrainBrushMode::Lower;
                           });
                addSegment(brushRow, "Smooth",
                           ctx.terrainBrushMode == EditorContext::TerrainBrushMode::Smooth,
                           [&ctx] {
                               ctx.terrainBrushMode = EditorContext::TerrainBrushMode::Smooth;
                           });
                auto& brushRow2 = terrain.emplace<Row>("toolbox_terrain_brushes2");
                addSegment(brushRow2, "Flatten",
                           ctx.terrainBrushMode == EditorContext::TerrainBrushMode::Flatten,
                           [&ctx] {
                               ctx.terrainBrushMode = EditorContext::TerrainBrushMode::Flatten;
                           });
                addSegment(brushRow2, "Noise",
                           ctx.terrainBrushMode == EditorContext::TerrainBrushMode::Noise,
                           [&ctx] {
                               ctx.terrainBrushMode = EditorContext::TerrainBrushMode::Noise;
                           });
            } else if (ctx.terrainEditMode == EditorContext::TerrainEditMode::Splat) {
                static const char* kLayers[] = {"Grass", "Rock", "Sand", "Dirt"};
                auto& layerRow = terrain.emplace<Row>("toolbox_terrain_layers");
                for (int i = 0; i < 4; ++i) {
                    addSegment(layerRow, kLayers[i],
                               ctx.terrainSplatLayer == static_cast<u32>(i),
                               [&ctx, i] {
                                   ctx.terrainSplatLayer = static_cast<u32>(i);
                               });
                }
            }

            auto& brushGrid = terrain.emplace<PropertyGrid>("toolbox_terrain");
            brushGrid.typedRow<FloatProperty>("Radius", ctx.terrainBrushRadius, 0.5f, 64.0f,
                                              0.1f, "%.1f", true);
            brushGrid.typedRow<FloatProperty>("Strength", ctx.terrainBrushStrength, 0.01f,
                                              1.0f, 0.01f, "%.2f", true);
            auto& openTerrainRow = terrain.emplace<Row>("toolbox_open_terrain_row");
            auto& openTerrain = openTerrainRow.emplace<Button>("Open Terrain Editor");
            openTerrain.setGrow(true);
            openTerrain.setTooltip("Sculpt, paint, import and export terrain data");
            openTerrain.setOnClick([] {
                UIKit::PanelRegistry::instance().open("terrain.editor");
            });
        }

        ui.draw(root);
    }
#else
    void render(ECS::World&, EditorContext&, SceneViewport&) {}
#endif

private:
    bool m_open = true;
    int m_wireframeDensity = 0;
};

}  // namespace Caffeine::Editor
