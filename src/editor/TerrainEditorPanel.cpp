#include "editor/TerrainEditorPanel.hpp"

#ifdef CF_HAS_IMGUI

#include "assets/MeshCache.hpp"
#include "ecs/Components.hpp"
#include "ecs/TerrainComponents.hpp"
#include "terrain/TerrainCache.hpp"
#include "terrain/TerrainGpuTextures.hpp"

#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <string>

namespace Caffeine::Editor {
namespace {

std::filesystem::path projectRootFromContext(const EditorContext& ctx) {
    if (ctx.currentScenePath.empty()) return {};
    const auto sceneDir = std::filesystem::path(ctx.currentScenePath).parent_path();
    const auto root = sceneDir.parent_path();
    return root.empty() ? sceneDir : root;
}

void copyCString(char* dest, usize destSize, const char* src) {
    if (!dest || destSize == 0) return;
    std::strncpy(dest, src ? src : "", destSize - 1);
    dest[destSize - 1] = '\0';
}

}  // namespace

void TerrainEditorPanel::render(ECS::World& world, EditorContext& ctx) {
    if (!m_open) return;

    if (!ImGui::Begin("Terrain Editor", &m_open)) {
        ImGui::End();
        return;
    }

    ECS::Entity entity = ctx.selectedEntity;
    if (!entity.isValid() || !world.has<ECS::TerrainComponent>(entity)) {
        ImGui::TextWrapped(
            "Select a Terrain entity to sculpt or paint. Use the Terrain Generator plugin to "
            "procedurally create heightmaps.");
        ImGui::End();
        return;
    }

    auto* terrain = world.get<ECS::TerrainComponent>(entity);
    if (!terrain) {
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted("Sculpt, paint and export terrain data for the selected entity.");
    ImGui::Separator();

    if (ImGui::BeginTabBar("##terrain_editor_tabs")) {
        if (ImGui::BeginTabItem("Sculpt / Paint")) {
            drawEditTools(world, entity, ctx);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Data")) {
            drawDataFile(world, entity, ctx);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

void TerrainEditorPanel::drawEditTools(ECS::World& world, ECS::Entity entity, EditorContext& ctx) {
    auto* terrain = world.get<ECS::TerrainComponent>(entity);
    if (!terrain) return;

    const std::string projectRoot = projectRootFromContext(ctx).string();

    if (ImGui::Button("Reset Default Textures")) {
        const ECS::TerrainComponent defaults;
        copyCString(terrain->texturePath, sizeof(terrain->texturePath), defaults.texturePath);
        for (u32 i = 0; i < ECS::kTerrainSplatLayerCount; ++i) {
            copyCString(terrain->splatLayerPaths[i], sizeof(terrain->splatLayerPaths[i]),
                        defaults.splatLayerPaths[i]);
        }
        Terrain::TerrainCache::instance().syncTextureToFilter(world, entity, *terrain);
#ifdef CF_HAS_SDL3
        Terrain::TerrainGpuTextureCache::instance().invalidateEntity(entity, nullptr);
#endif
        terrain->splatRevision++;
        ctx.isDirty = true;
    }

    ImGui::Separator();
    bool sculptActive = (ctx.terrainEditMode == EditorContext::TerrainEditMode::Sculpt);
    if (ImGui::Checkbox("Sculpt Mode", &sculptActive)) {
        if (sculptActive) {
            ctx.terrainEditMode = EditorContext::TerrainEditMode::Sculpt;
            ctx.gizmoMode = EditorContext::GizmoMode::None;
        } else if (ctx.terrainEditMode == EditorContext::TerrainEditMode::Sculpt) {
            ctx.terrainEditMode = EditorContext::TerrainEditMode::None;
        }
    }
    bool splatActive = (ctx.terrainEditMode == EditorContext::TerrainEditMode::Splat);
    if (ImGui::Checkbox("Splat Paint", &splatActive)) {
        if (splatActive) {
            ctx.terrainEditMode = EditorContext::TerrainEditMode::Splat;
            ctx.gizmoMode = EditorContext::GizmoMode::None;
        } else if (ctx.terrainEditMode == EditorContext::TerrainEditMode::Splat) {
            ctx.terrainEditMode = EditorContext::TerrainEditMode::None;
        }
    }

    if (ctx.terrainEditMode == EditorContext::TerrainEditMode::Sculpt) {
        int brushMode = static_cast<int>(ctx.terrainBrushMode);
        if (ImGui::RadioButton("Raise", brushMode == 0)) {
            ctx.terrainBrushMode = EditorContext::TerrainBrushMode::Raise;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Lower", brushMode == 1)) {
            ctx.terrainBrushMode = EditorContext::TerrainBrushMode::Lower;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Smooth", brushMode == 2)) {
            ctx.terrainBrushMode = EditorContext::TerrainBrushMode::Smooth;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Flatten", brushMode == 3)) {
            ctx.terrainBrushMode = EditorContext::TerrainBrushMode::Flatten;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Noise", brushMode == 4)) {
            ctx.terrainBrushMode = EditorContext::TerrainBrushMode::Noise;
        }
        if (ImGui::SliderFloat("Brush Radius", &ctx.terrainBrushRadius, 0.5f, 64.0f, "%.1f")) {
            ctx.terrainBrushRadius = std::max(0.5f, ctx.terrainBrushRadius);
        }
        if (ImGui::SliderFloat("Brush Strength", &ctx.terrainBrushStrength, 0.01f, 1.0f, "%.2f")) {
            ctx.terrainBrushStrength = std::max(0.01f, ctx.terrainBrushStrength);
        }
    } else if (ctx.terrainEditMode == EditorContext::TerrainEditMode::Splat) {
        const char* layerLabels[] = {"Grass", "Rock", "Sand", "Dirt"};
        int layer = static_cast<int>(ctx.terrainSplatLayer);
        if (ImGui::SliderInt("Paint Layer", &layer, 0, ECS::kTerrainSplatLayerCount - 1,
                             layerLabels[layer])) {
            ctx.terrainSplatLayer = static_cast<u32>(layer);
        }
        if (ImGui::SliderFloat("Brush Radius", &ctx.terrainBrushRadius, 0.5f, 64.0f, "%.1f")) {
            ctx.terrainBrushRadius = std::max(0.5f, ctx.terrainBrushRadius);
        }
        if (ImGui::SliderFloat("Brush Strength", &ctx.terrainBrushStrength, 0.01f, 1.0f, "%.2f")) {
            ctx.terrainBrushStrength = std::max(0.01f, ctx.terrainBrushStrength);
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Layer textures");
        for (u32 i = 0; i < ECS::kTerrainSplatLayerCount; ++i) {
            ImGui::PushID(static_cast<int>(i));
            const std::filesystem::path shown(terrain->splatLayerPaths[i]);
            ImGui::Text("%s: %s", layerLabels[i], shown.filename().string().c_str());
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", terrain->splatLayerPaths[i]);
            }
            if (terrain->splatLayerPaths[i][0] != '\0' &&
                Assets::MeshCache::resolveTexturePath(terrain->splatLayerPaths[i], projectRoot)
                    .empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "missing");
            }
            ImGui::PopID();
        }
    }
}

void TerrainEditorPanel::drawDataFile(ECS::World& world, ECS::Entity entity, EditorContext& ctx) {
    auto* terrain = world.get<ECS::TerrainComponent>(entity);
    if (!terrain) return;

    if (ImGui::InputText("Terrain Path", terrain->terrainDataPath, sizeof(terrain->terrainDataPath))) {
        ctx.isDirty = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Project-relative path to the .cterrain height/splat data file");
    }
    if (ImGui::Button("Export .cterrain")) {
        const std::filesystem::path projectRoot = projectRootFromContext(ctx);
        if (!projectRoot.empty()) {
            if (terrain->terrainDataPath[0] == '\0') {
                std::string entityName = "Terrain";
                if (auto* name = world.get<NameComponent>(entity)) {
                    if (name->name[0] != '\0') entityName = name->name;
                }
                for (char& c : entityName) {
                    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') {
                        c = '_';
                    }
                }
                const std::string relPath =
                    "terrain/" + entityName + "_" + std::to_string(entity.id()) + ".cterrain";
                copyCString(terrain->terrainDataPath, sizeof(terrain->terrainDataPath),
                            relPath.c_str());
            }
            Terrain::TerrainCache::instance().saveTerrainFile(
                entity, projectRoot / terrain->terrainDataPath, terrain->useSplatmap);
            ctx.isDirty = true;
        }
    }
}

}  // namespace Caffeine::Editor

#endif
