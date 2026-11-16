#include "editor/TerrainPanel.hpp"

#include "assets/MeshCache.hpp"
#include "caffeine/ui/UI.hpp"
#include "ecs/TerrainComponents.hpp"
#include "ecs/World.hpp"
#include "scene/EditorContext.hpp"
#include "terrain/TerrainCache.hpp"
#include "terrain/TerrainGpuTextures.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

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

void TerrainPanel::drawEditTools(ECS::World& world, EditorContext& ctx,
                                 Caffeine::UIKit::Elements::Container& parent) {
#ifdef CF_HAS_IMGUI
    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;

    auto* terrain = world.get<ECS::TerrainComponent>(ctx.selectedEntity);
    if (!terrain) return;

    m_sculptMode = ctx.terrainEditMode == EditorContext::TerrainEditMode::Sculpt;
    m_splatMode = ctx.terrainEditMode == EditorContext::TerrainEditMode::Splat;
    m_brushIndex = static_cast<int>(ctx.terrainBrushMode);
    m_layerIndex = static_cast<int>(ctx.terrainSplatLayer);

    auto& modes = parent.emplace<Card>("terrain_modes");
    modes.emplace<Label>("Edit mode", TextTone::Accent).setHeading(true);
    auto& modeGrid = modes.emplace<PropertyGrid>("terrain_modes_grid");
    modeGrid.typedRow<BoolProperty>("Sculpt mode", m_sculptMode)
        .setOnChange([&ctx](bool value) {
            const bool splat = ctx.terrainEditMode == EditorContext::TerrainEditMode::Splat;
            setEditMode(ctx, value ? EditorContext::TerrainEditMode::Sculpt
                                   : (splat ? EditorContext::TerrainEditMode::Splat
                                            : EditorContext::TerrainEditMode::None));
        });
    modeGrid.typedRow<BoolProperty>("Splat paint", m_splatMode)
        .setOnChange([&ctx](bool value) {
            const bool sculpt = ctx.terrainEditMode == EditorContext::TerrainEditMode::Sculpt;
            setEditMode(ctx, value ? EditorContext::TerrainEditMode::Splat
                                   : (sculpt ? EditorContext::TerrainEditMode::Sculpt
                                             : EditorContext::TerrainEditMode::None));
        });

    if (ctx.terrainEditMode == EditorContext::TerrainEditMode::Sculpt) {
        auto& section = parent.emplace<Elements::Section>("Sculpt brush", true);
        auto& brushes = section.emplace<SegmentedControl>(
            "terrain_brush", &m_brushIndex,
            std::vector<std::string>{kBrushLabels[0], kBrushLabels[1], kBrushLabels[2],
                                     kBrushLabels[3], kBrushLabels[4]});
        brushes.setOnChange([&ctx](int value) {
            ctx.terrainBrushMode = static_cast<EditorContext::TerrainBrushMode>(value);
            setEditMode(ctx, EditorContext::TerrainEditMode::Sculpt);
        });
        section.emplace<Label>("Flatten keeps the height under the first click of the stroke.",
                               TextTone::Muted);
    } else if (ctx.terrainEditMode == EditorContext::TerrainEditMode::Splat) {
        auto& section = parent.emplace<Elements::Section>("Paint layer", true);
        auto& layers = section.emplace<SegmentedControl>(
            "terrain_layer", &m_layerIndex,
            std::vector<std::string>{kLayerLabels[0], kLayerLabels[1], kLayerLabels[2],
                                     kLayerLabels[3]});
        layers.setOnChange([&ctx](int value) {
            ctx.terrainSplatLayer = static_cast<u32>(value);
            setEditMode(ctx, EditorContext::TerrainEditMode::Splat);
        });
    }

    if (ctx.terrainEditMode != EditorContext::TerrainEditMode::None) {
        auto& settings = parent.emplace<Card>("terrain_brush_settings");
        settings.emplace<Label>("Brush", TextTone::Accent).setHeading(true);
        auto& grid = settings.emplace<PropertyGrid>("terrain_brush_grid");
        grid.typedRow<FloatProperty>("Brush radius", ctx.terrainBrushRadius, 0.5f, 64.0f,
                                     0.1f, "%.1f m", true);
        grid.typedRow<FloatProperty>("Brush strength", ctx.terrainBrushStrength, 0.01f, 1.0f,
                                     0.01f, "%.2f", true);
        settings.emplace<Label>(
            "Left-drag in the viewport to edit. Right-drag orbits the camera.",
            TextTone::Muted);
    }

    const auto* heightmap = Terrain::TerrainCache::instance().heightmapFor(ctx.selectedEntity);
    const auto* splatmap = Terrain::TerrainCache::instance().splatmapFor(ctx.selectedEntity);
    if (heightmap && splatmap) {
        char stats[256];
        std::snprintf(stats, sizeof(stats),
                      "Heightmap %ux%u · splat %ux%u · %.0f x %.0f m",
                      heightmap->resolutionX(), heightmap->resolutionZ(),
                      splatmap->resolutionX(), splatmap->resolutionZ(),
                      terrain->worldSizeX, terrain->worldSizeZ);
        parent.emplace<Label>(stats, TextTone::Muted);
    }

    parent.emplace<Separator>(true);
    auto& actions = parent.emplace<Row>("terrain_actions");
    actions.emplace<Button>("Reset default textures", ButtonVariant::Toolbar)
        .setOnClick([&] {
            const ECS::TerrainComponent defaults;
            copyCString(terrain->texturePath, sizeof(terrain->texturePath),
                        defaults.texturePath);
            terrain->tint = defaults.tint;
            for (u32 i = 0; i < ECS::kTerrainSplatLayerCount; ++i) {
                copyCString(terrain->splatLayerPaths[i],
                            sizeof(terrain->splatLayerPaths[i]),
                            defaults.splatLayerPaths[i]);
            }
            if (auto* map =
                    Terrain::TerrainCache::instance().splatmapFor(ctx.selectedEntity)) {
                map->fillLayer(0, 1.0f);
            }
            Terrain::TerrainCache::instance().syncTextureToFilter(world, ctx.selectedEntity,
                                                                  *terrain);
#ifdef CF_HAS_SDL3
            Terrain::TerrainGpuTextureCache::instance().invalidateEntity(ctx.selectedEntity,
                                                                         nullptr);
#endif
            terrain->splatRevision++;
            m_status = "Default terrain textures restored.";
            ctx.markDirty();
            ctx.assetBrowserDirty = true;
        });

    if (ctx.terrainEditMode == EditorContext::TerrainEditMode::Splat) {
        parent.emplace<Separator>(true);
        auto& layerCard = parent.emplace<Card>("terrain_layers");
        layerCard.emplace<Label>("Layer textures", TextTone::Accent).setHeading(true);
        const std::filesystem::path rootPath = projectRootFor(ctx);
        for (u32 i = 0; i < ECS::kTerrainSplatLayerCount; ++i) {
            auto& row = layerCard.emplace<Row>("terrain_layer_" + std::to_string(i));
            const std::filesystem::path shown(terrain->splatLayerPaths[i]);
            row.emplace<Label>(std::string(kLayerLabels[i]) + ": " +
                                   shown.filename().string(),
                               TextTone::Muted)
                .setTooltip(terrain->splatLayerPaths[i]);
            if (terrain->splatLayerPaths[i][0] != '\0' &&
                Assets::MeshCache::resolveTexturePath(terrain->splatLayerPaths[i],
                                                      rootPath.string())
                    .empty()) {
                row.emplace<Label>("missing", TextTone::Danger);
            }
        }
    }
#endif
}

void TerrainPanel::drawDataFile(ECS::World& world, EditorContext& ctx,
                                Caffeine::UIKit::Elements::Container& parent) {
#ifdef CF_HAS_IMGUI
    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;

    auto* terrain = world.get<ECS::TerrainComponent>(ctx.selectedEntity);
    if (!terrain) return;

    auto& card = parent.emplace<Card>("terrain_data");
    card.emplace<Label>("Terrain data", TextTone::Accent).setHeading(true);
    auto& grid = card.emplace<PropertyGrid>("terrain_data_grid");
    grid.typedRow<BufferProperty>("Terrain path", terrain->terrainDataPath,
                                  sizeof(terrain->terrainDataPath));
    card.emplace<Label>(
        "Project-relative path to the .cterrain height/splat data file.",
        TextTone::Muted);

    const std::filesystem::path rootPath = projectRootFor(ctx);
    auto& actions = card.emplace<Row>("terrain_data_actions");
    actions.emplace<Button>("Export .cterrain", ButtonVariant::Toolbar)
        .setOnClick([&] {
            if (terrain->terrainDataPath[0] == '\0') {
                const std::string relPath = "terrain/Terrain_" +
                                            std::to_string(ctx.selectedEntity.id()) +
                                            ".cterrain";
                copyCString(terrain->terrainDataPath, sizeof(terrain->terrainDataPath),
                            relPath.c_str());
            }
            if (Terrain::TerrainCache::instance().saveTerrainFile(
                    ctx.selectedEntity, rootPath / terrain->terrainDataPath,
                    terrain->useSplatmap)) {
                m_status = std::string("Exported ") + terrain->terrainDataPath;
                ctx.markDirty();
            } else {
                m_status = "Export failed. Check the terrain path.";
            }
        });
    actions.emplace<Button>("Import .cterrain", ButtonVariant::Toolbar)
        .setOnClick([&] {
            if (terrain->terrainDataPath[0] == '\0') {
                m_status = "Set a terrain path before importing.";
            } else if (Terrain::TerrainCache::instance().loadTerrainFile(
                           world, ctx.selectedEntity, *terrain,
                           rootPath / terrain->terrainDataPath)) {
                m_status = std::string("Imported ") + terrain->terrainDataPath;
                ctx.markDirty();
                ctx.assetBrowserDirty = true;
            } else {
                m_status = "Import failed. Check that the .cterrain file exists.";
            }
        });

    card.emplace<Label>((rootPath / terrain->terrainDataPath).string(),
                        TextTone::Disabled);
    card.emplace<Label>("The active scene save also writes this file automatically.",
                        TextTone::Muted);
    if (!m_status.empty()) card.emplace<Label>(m_status, TextTone::Muted);
#endif
}

void TerrainPanel::render(ECS::World& world, EditorContext& ctx) {
#ifdef CF_HAS_IMGUI
    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;

    if (!m_open) return;

    Panel root("Terrain", "Sculpt, paint and export terrain data", "terrain", &m_open);
    UIContext ui;
    if (!ctx.selectedEntity.isValid() ||
        !world.has<ECS::TerrainComponent>(ctx.selectedEntity)) {
        root.emplace<Elements::EmptyState>(
            "No terrain entity selected",
            "Select a Terrain entity in the Hierarchy to sculpt or paint it.", "terrain");
        ui.draw(root);
        return;
    }

    auto& tabs = root.emplace<TabBar>("terrain_tabs");
    auto& editPage = tabs.emplace<TabPage>("Sculpt / Paint");
    drawEditTools(world, ctx, editPage);
    auto& dataPage = tabs.emplace<TabPage>("Data");
    drawDataFile(world, ctx, dataPage);
    ui.draw(root);
#endif
}

}  // namespace Caffeine::Editor
