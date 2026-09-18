#include "editor/TerrainEditorPanel.hpp"

#ifdef CF_HAS_IMGUI

#include "assets/MeshCache.hpp"
#include "ecs/Components.hpp"
#include "ecs/TerrainComponents.hpp"
#include "terrain/TerrainCache.hpp"
#include "terrain/TerrainGpuTextures.hpp"
#include "terrain/generation/TerrainGenerator.hpp"

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
        ImGui::TextDisabled("Select a Terrain entity to generate, sculpt, or paint.");
        ImGui::End();
        return;
    }

    auto* terrain = world.get<ECS::TerrainComponent>(entity);
    if (!terrain) {
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted("Tools for the selected Terrain — not component data.");
    ImGui::Separator();

    if (ImGui::BeginTabBar("##terrain_editor_tabs")) {
        if (ImGui::BeginTabItem("Generate")) {
            drawGeneration(world, entity, ctx);
            ImGui::EndTabItem();
        }
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

void TerrainEditorPanel::drawGeneration(ECS::World& world, ECS::Entity entity, EditorContext& ctx) {
    auto* terrain = world.get<ECS::TerrainComponent>(entity);
    if (!terrain) return;

    static const char* styleNames[] = {"Realistic", "Low Poly", "Stylized", "Custom"};
    int style = static_cast<int>(terrain->generation.style);
    if (ImGui::Combo("Style", &style, styleNames, 4)) {
        terrain->generation.style = static_cast<ECS::TerrainGenStyle>(style);
        if (terrain->generation.style != ECS::TerrainGenStyle::Custom) {
            Terrain::applyGenerationStylePreset(terrain->generation, terrain->generation.style);
        }
        ctx.isDirty = true;
    }

    int seed = static_cast<int>(terrain->generation.seed);
    if (ImGui::InputInt("Seed", &seed)) {
        terrain->generation.seed = static_cast<u32>(std::max(0, seed));
        ctx.isDirty = true;
    }
    if (ImGui::DragFloat("Noise Scale", &terrain->generation.noiseScale, 0.001f, 0.001f, 0.2f,
                          "%.3f")) {
        terrain->generation.style = ECS::TerrainGenStyle::Custom;
        ctx.isDirty = true;
    }
    int octaves = static_cast<int>(terrain->generation.octaves);
    if (ImGui::SliderInt("Octaves", &octaves, 1, 8)) {
        terrain->generation.octaves = static_cast<u32>(octaves);
        terrain->generation.style = ECS::TerrainGenStyle::Custom;
        ctx.isDirty = true;
    }
    if (ImGui::SliderFloat("Persistence", &terrain->generation.persistence, 0.1f, 1.0f)) {
        terrain->generation.style = ECS::TerrainGenStyle::Custom;
        ctx.isDirty = true;
    }
    if (ImGui::SliderFloat("Lacunarity", &terrain->generation.lacunarity, 1.1f, 4.0f)) {
        terrain->generation.style = ECS::TerrainGenStyle::Custom;
        ctx.isDirty = true;
    }
    if (ImGui::SliderFloat("Amplitude", &terrain->generation.amplitude, 0.05f, 1.0f)) {
        terrain->generation.style = ECS::TerrainGenStyle::Custom;
        ctx.isDirty = true;
    }
    if (ImGui::SliderFloat("Base Height", &terrain->generation.baseHeight, 0.0f, 0.8f)) {
        terrain->generation.style = ECS::TerrainGenStyle::Custom;
        ctx.isDirty = true;
    }

    static const char* noiseNames[] = {"Perlin", "Simplex", "Value", "Worley"};
    int noiseType = static_cast<int>(terrain->generation.noiseAlgorithm);
    if (ImGui::Combo("Noise", &noiseType, noiseNames, 4)) {
        terrain->generation.noiseAlgorithm =
            static_cast<Terrain::TerrainNoiseAlgorithm>(noiseType);
        terrain->generation.style = ECS::TerrainGenStyle::Custom;
        ctx.isDirty = true;
    }

    if (ImGui::Checkbox("Domain Warp", &terrain->generation.domainWarp)) {
        terrain->generation.style = ECS::TerrainGenStyle::Custom;
        ctx.isDirty = true;
    }
    if (terrain->generation.domainWarp) {
        if (ImGui::SliderFloat("Warp Strength", &terrain->generation.domainWarpStrength, 0.0f, 1.5f)) {
            terrain->generation.style = ECS::TerrainGenStyle::Custom;
            ctx.isDirty = true;
        }
    }

    if (ImGui::TreeNode("Filters")) {
        if (ImGui::Checkbox("Thermal Erosion", &terrain->generation.thermalErosion)) {
            terrain->generation.style = ECS::TerrainGenStyle::Custom;
            ctx.isDirty = true;
        }
        if (terrain->generation.thermalErosion) {
            int thermalIters = static_cast<int>(terrain->generation.thermalIterations);
            if (ImGui::SliderInt("Thermal Iterations", &thermalIters, 0, 100)) {
                terrain->generation.thermalIterations = static_cast<u32>(thermalIters);
                ctx.isDirty = true;
            }
        }
        if (ImGui::Checkbox("Hydraulic Erosion", &terrain->generation.hydraulicErosion)) {
            terrain->generation.style = ECS::TerrainGenStyle::Custom;
            ctx.isDirty = true;
        }
        if (terrain->generation.hydraulicErosion) {
            int hydroIters = static_cast<int>(terrain->generation.hydraulicIterations);
            if (ImGui::SliderInt("Hydraulic Iterations", &hydroIters, 0, 200)) {
                terrain->generation.hydraulicIterations = static_cast<u32>(hydroIters);
                ctx.isDirty = true;
            }
        }
        if (ImGui::Checkbox("Smooth", &terrain->generation.smoothPass)) {
            terrain->generation.style = ECS::TerrainGenStyle::Custom;
            ctx.isDirty = true;
        }
        if (ImGui::SliderFloat("Height Quantize", &terrain->generation.heightQuantize, 0.0f, 24.0f,
                               "%.0f")) {
            terrain->generation.style = ECS::TerrainGenStyle::Custom;
            ctx.isDirty = true;
        }
        if (ImGui::Checkbox("Auto Splat Biomes", &terrain->generation.autoSplat)) {
            ctx.isDirty = true;
        }
        ImGui::TreePop();
    }

    if (ImGui::Button("Generate Terrain")) {
        Terrain::TerrainCache::instance().generateTerrain(world, entity, *terrain);
        ctx.isDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Flatten")) {
        if (auto* heightmap = Terrain::TerrainCache::instance().heightmapFor(entity)) {
            heightmap->fill(0.0f);
            terrain->dataRevision++;
            ctx.isDirty = true;
        }
    }
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
