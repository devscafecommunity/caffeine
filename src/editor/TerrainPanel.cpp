#include "editor/TerrainPanel.hpp"

#include "assets/MeshCache.hpp"
#include "caffeine/ui/Widgets.hpp"
#include "ecs/TerrainComponents.hpp"
#include "ecs/World.hpp"
#include "editor/EditorContext.hpp"
#include "terrain/TerrainCache.hpp"
#include "terrain/TerrainGpuTextures.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

namespace Caffeine::Editor {
namespace {

using namespace Caffeine::UIKit;

const char* const kBrushLabels[] = {"Raise", "Lower", "Smooth", "Flatten", "Noise"};
const char* const kLayerLabels[] = {"Grass", "Rock", "Sand", "Dirt"};
constexpr int kBrushCount = static_cast<int>(sizeof(kBrushLabels) / sizeof(kBrushLabels[0]));
constexpr int kLayerCount = static_cast<int>(sizeof(kLayerLabels) / sizeof(kLayerLabels[0]));

std::filesystem::path projectRootFor(const EditorContext& ctx) {
    if (!ctx.projectRootPath.empty()) return ctx.projectRootPath;
    if (!ctx.currentScenePath.empty()) {
        const std::filesystem::path sceneDir =
            std::filesystem::path(ctx.currentScenePath).parent_path();
        const std::filesystem::path root = sceneDir.parent_path();
        return root.empty() ? sceneDir : root;
    }
    return std::filesystem::current_path();
}

void copyCString(char* dest, usize destSize, const char* src) {
    if (!dest || destSize == 0) return;
    std::strncpy(dest, src ? src : "", destSize - 1);
    dest[destSize - 1] = '\0';
}

void setEditMode(EditorContext& ctx, EditorContext::TerrainEditMode mode) {
    ctx.terrainEditMode = mode;
    if (mode != EditorContext::TerrainEditMode::None) {
        ctx.gizmoMode = EditorContext::GizmoMode::None;
    }
}

}  // namespace

void TerrainPanel::drawEditTools(ECS::World& world, EditorContext& ctx) {
#ifdef CF_HAS_IMGUI
    auto* terrain = world.get<ECS::TerrainComponent>(ctx.selectedEntity);
    if (!terrain) return;

    bool sculpt = ctx.terrainEditMode == EditorContext::TerrainEditMode::Sculpt;
    bool splat = ctx.terrainEditMode == EditorContext::TerrainEditMode::Splat;

    if (BeginPropertyGrid("terrain_panel_modes")) {
        if (CheckboxProperty("Sculpt mode", &sculpt)) {
            setEditMode(ctx, sculpt ? EditorContext::TerrainEditMode::Sculpt
                                    : (splat ? EditorContext::TerrainEditMode::Splat
                                             : EditorContext::TerrainEditMode::None));
        }
        if (CheckboxProperty("Splat paint", &splat)) {
            setEditMode(ctx, splat ? EditorContext::TerrainEditMode::Splat
                                   : (sculpt ? EditorContext::TerrainEditMode::Sculpt
                                             : EditorContext::TerrainEditMode::None));
        }
        EndPropertyGrid();
    }

    if (ctx.terrainEditMode == EditorContext::TerrainEditMode::Sculpt) {
        if (Section("Sculpt brush", true)) {
            const f32 spacing = ImGui::GetStyle().ItemSpacing.x;
            const f32 width =
                std::max(48.0f, (ImGui::GetContentRegionAvail().x - spacing * (kBrushCount - 1)) /
                                    static_cast<f32>(kBrushCount));
            for (int i = 0; i < kBrushCount; ++i) {
                const bool selected =
                    static_cast<int>(ctx.terrainBrushMode) == i;
                if (SegmentedButton(kBrushLabels[i], selected, ImVec2(width, 0.0f))) {
                    ctx.terrainBrushMode = static_cast<EditorContext::TerrainBrushMode>(i);
                    setEditMode(ctx, EditorContext::TerrainEditMode::Sculpt);
                }
                if (i + 1 < kBrushCount) ImGui::SameLine();
            }
            TextMuted("Flatten keeps the height under the first click of the stroke.");
        }
    } else if (ctx.terrainEditMode == EditorContext::TerrainEditMode::Splat) {
        if (Section("Paint layer", true)) {
            const f32 spacing = ImGui::GetStyle().ItemSpacing.x;
            const f32 width =
                std::max(52.0f, (ImGui::GetContentRegionAvail().x - spacing * (kLayerCount - 1)) /
                                    static_cast<f32>(kLayerCount));
            for (int i = 0; i < kLayerCount; ++i) {
                const bool selected = static_cast<int>(ctx.terrainSplatLayer) == i;
                if (SegmentedButton(kLayerLabels[i], selected, ImVec2(width, 0.0f))) {
                    ctx.terrainSplatLayer = static_cast<u32>(i);
                    setEditMode(ctx, EditorContext::TerrainEditMode::Splat);
                }
                if (i + 1 < kLayerCount) ImGui::SameLine();
            }
        }
    }

    if (ctx.terrainEditMode != EditorContext::TerrainEditMode::None) {
        if (BeginPropertyGrid("terrain_panel_brush")) {
            SliderFloatProperty("Brush radius", &ctx.terrainBrushRadius, 0.5f, 64.0f, "%.1f m");
            SliderFloatProperty("Brush strength", &ctx.terrainBrushStrength, 0.01f, 1.0f, "%.2f");
            EndPropertyGrid();
        }
        TextMuted("Left-drag in the viewport to edit. Right-drag orbits the camera.");
    }

    const auto* heightmap = Terrain::TerrainCache::instance().heightmapFor(ctx.selectedEntity);
    const auto* splatmap = Terrain::TerrainCache::instance().splatmapFor(ctx.selectedEntity);
    if (heightmap && splatmap) {
        TextMutedF("Heightmap %ux%u · splat %ux%u · %.0f x %.0f m",
                   heightmap->resolutionX(), heightmap->resolutionZ(),
                   splatmap->resolutionX(), splatmap->resolutionZ(),
                   terrain->worldSizeX, terrain->worldSizeZ);
    }

    SeparatorSpaced();
    if (ToolbarButton("Reset default textures",
                      "Restore the default terrain and splat textures")) {
        const ECS::TerrainComponent defaults;
        copyCString(terrain->texturePath, sizeof(terrain->texturePath), defaults.texturePath);
        terrain->tint = defaults.tint;
        for (u32 i = 0; i < ECS::kTerrainSplatLayerCount; ++i) {
            copyCString(terrain->splatLayerPaths[i], sizeof(terrain->splatLayerPaths[i]),
                        defaults.splatLayerPaths[i]);
        }
        if (auto* splatmap =
                Terrain::TerrainCache::instance().splatmapFor(ctx.selectedEntity)) {
            splatmap->fillLayer(0, 1.0f);
        }
        Terrain::TerrainCache::instance().syncTextureToFilter(world, ctx.selectedEntity, *terrain);
#ifdef CF_HAS_SDL3
        Terrain::TerrainGpuTextureCache::instance().invalidateEntity(ctx.selectedEntity, nullptr);
#endif
        terrain->splatRevision++;
        m_status = "Default terrain textures restored.";
        ctx.markDirty();
        ctx.assetBrowserDirty = true;
    }

    if (ctx.terrainEditMode == EditorContext::TerrainEditMode::Splat) {
        SeparatorSpaced();
        SectionLabel("Layer textures");
        const std::filesystem::path root = projectRootFor(ctx);
        for (u32 i = 0; i < ECS::kTerrainSplatLayerCount; ++i) {
            ImGui::PushID(static_cast<int>(i));
            const std::filesystem::path shown(terrain->splatLayerPaths[i]);
            TextMutedF("%s: %s", kLayerLabels[i], shown.filename().string().c_str());
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", terrain->splatLayerPaths[i]);
            }
            if (terrain->splatLayerPaths[i][0] != '\0' &&
                Assets::MeshCache::resolveTexturePath(terrain->splatLayerPaths[i], root.string())
                    .empty()) {
                TextDanger("missing");
            }
            ImGui::PopID();
        }
    }
#endif
}

void TerrainPanel::drawDataFile(ECS::World& world, EditorContext& ctx) {
#ifdef CF_HAS_IMGUI
    auto* terrain = world.get<ECS::TerrainComponent>(ctx.selectedEntity);
    if (!terrain) return;

    if (BeginPropertyGrid("terrain_panel_data")) {
        if (InputTextProperty("Terrain path", terrain->terrainDataPath,
                              sizeof(terrain->terrainDataPath))) {
            ctx.markDirty();
        }
        HelpMarker("Project-relative path to the .cterrain height/splat data file.");
        EndPropertyGrid();
    }

    const std::filesystem::path root = projectRootFor(ctx);
    if (ToolbarButton("Export .cterrain", "Write the height and splat data to disk")) {
        if (terrain->terrainDataPath[0] == '\0') {
            const std::string relPath =
                "terrain/Terrain_" + std::to_string(ctx.selectedEntity.id()) + ".cterrain";
            copyCString(terrain->terrainDataPath, sizeof(terrain->terrainDataPath),
                        relPath.c_str());
        }
        if (Terrain::TerrainCache::instance().saveTerrainFile(
                ctx.selectedEntity, root / terrain->terrainDataPath, terrain->useSplatmap)) {
            m_status = std::string("Exported ") + terrain->terrainDataPath;
            ctx.markDirty();
        } else {
            m_status = "Export failed. Check the terrain path.";
        }
    }
    ImGui::SameLine();
    if (ToolbarButton("Import .cterrain", "Replace the terrain from the data file")) {
        if (terrain->terrainDataPath[0] == '\0') {
            m_status = "Set a terrain path before importing.";
        } else if (Terrain::TerrainCache::instance().loadTerrainFile(
                       world, ctx.selectedEntity, *terrain, root / terrain->terrainDataPath)) {
            m_status = std::string("Imported ") + terrain->terrainDataPath;
            ctx.markDirty();
            ctx.assetBrowserDirty = true;
        } else {
            m_status = "Import failed. Check that the .cterrain file exists.";
        }
    }

    TextMutedF("Resolved path: %s", (root / terrain->terrainDataPath).string().c_str());
    TextMuted("The active scene save also writes this file automatically.");
    if (!m_status.empty()) TextMuted(m_status.c_str());
#endif
}

void TerrainPanel::render(ECS::World& world, EditorContext& ctx) {
#ifdef CF_HAS_IMGUI
    if (!m_open) return;

    if (BeginPanel("Terrain", &m_open)) {
        PanelHeader("Terrain Editor", "Sculpt, paint and export terrain data", "terrain");

        if (!ctx.selectedEntity.isValid() ||
            !world.has<ECS::TerrainComponent>(ctx.selectedEntity)) {
            EmptyState("No terrain entity selected",
                       "Select a Terrain entity in the Hierarchy to sculpt or paint it.",
                       "terrain");
            EndPanel();
            return;
        }

        if (BeginTabs("##terrain_panel_tabs")) {
            if (BeginTab("Sculpt / Paint")) {
                drawEditTools(world, ctx);
                EndTab();
            }
            if (BeginTab("Data")) {
                drawDataFile(world, ctx);
                EndTab();
            }
            EndTabs();
        }
    }
    EndPanel();
#endif
}

}  // namespace Caffeine::Editor
