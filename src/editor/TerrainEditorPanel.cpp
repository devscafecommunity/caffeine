#include "editor/TerrainEditorPanel.hpp"

#ifdef CF_HAS_IMGUI

#include "assets/HdrAssetPresets.hpp"
#include "assets/MeshCache.hpp"
#include "ecs/Components.hpp"
#include "ecs/TerrainComponents.hpp"
#include "terrain/TerrainCache.hpp"
#include "terrain/TerrainGpuTextures.hpp"
#include "terrain/generation/GeologicalSimulator.hpp"
#include "terrain/generation/TerrainGenerator.hpp"

#include <imnodes.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

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

void applyHdrGroundPreset(ECS::TerrainComponent& terrain, int presetIndex) {
    presetIndex = std::clamp(presetIndex, 0, Assets::kHdrGroundPresetCount - 1);
    const char* grass = Assets::kHdrGroundPresetPaths[presetIndex];
    constexpr const char* kRock = Assets::kHdrGroundPresetPaths[5];
    constexpr const char* kSand = Assets::kHdrGroundPresetPaths[1];
    constexpr const char* kDirt = Assets::kHdrGroundPresetPaths[4];

    copyCString(terrain.texturePath, sizeof(terrain.texturePath), grass);
    copyCString(terrain.splatLayerPaths[0], sizeof(terrain.splatLayerPaths[0]), grass);
    copyCString(terrain.splatLayerPaths[1], sizeof(terrain.splatLayerPaths[1]), kRock);
    copyCString(terrain.splatLayerPaths[2], sizeof(terrain.splatLayerPaths[2]), kSand);
    copyCString(terrain.splatLayerPaths[3], sizeof(terrain.splatLayerPaths[3]), kDirt);
    terrain.textureTileSize = 4.0f;
    terrain.splatTileSize = 4.0f;
}

constexpr int kAttrStride = 1000;

int nodeInputAttr(int nodeId) { return nodeId * kAttrStride + 1; }
int nodeOutputAttr(int nodeId) { return nodeId * kAttrStride + 2; }
int nodeIdFromAttr(int attrId) { return attrId / kAttrStride; }

bool isGeneratorNode(TerrainGraphNodeType type) {
    return type == TerrainGraphNodeType::Noise || type == TerrainGraphNodeType::Ridged ||
           type == TerrainGraphNodeType::Hybrid || type == TerrainGraphNodeType::DiamondSquare ||
           type == TerrainGraphNodeType::SpectralFFT;
}

const char* nodeTitle(TerrainGraphNodeType type) {
    switch (type) {
        case TerrainGraphNodeType::Noise: return "Noise FBM";
        case TerrainGraphNodeType::Ridged: return "Ridged";
        case TerrainGraphNodeType::Hybrid: return "Hybrid";
        case TerrainGraphNodeType::DiamondSquare: return "Diamond-Square";
        case TerrainGraphNodeType::SpectralFFT: return "FFT Spectral";
        case TerrainGraphNodeType::Thermal: return "Thermal Erosion";
        case TerrainGraphNodeType::MicroSculpt: return "Micro Sculpt";
        case TerrainGraphNodeType::Rivers: return "Rivers";
        case TerrainGraphNodeType::GeoSim: return "Geo Simulation";
        case TerrainGraphNodeType::Smooth: return "Smooth";
        case TerrainGraphNodeType::Output: return "Output";
    }
    return "Node";
}

}  // namespace

TerrainEditorPanel::TerrainEditorPanel() {
    if (!ImNodes::GetCurrentContext()) {
        ImNodes::CreateContext();
    }
    m_editorContext = ImNodes::EditorContextCreate();
    ensureDefaultGraph();
}

TerrainEditorPanel::~TerrainEditorPanel() {
    if (m_editorContext) {
        ImNodes::EditorContextFree(m_editorContext);
        m_editorContext = nullptr;
    }
}

void TerrainEditorPanel::ensureDefaultGraph() {
    if (!m_graphNodes.empty()) return;
    syncGraphFromStyle(ECS::TerrainGenStyle::Realistic);
}

void TerrainEditorPanel::syncGraphFromStyle(ECS::TerrainGenStyle style) {
    m_graphNodes.clear();
    m_graphLinks.clear();
    m_selectedNodeId = 0;
    m_graphLayoutDirty = true;

    auto add = [&](TerrainGraphNodeType type) {
        TerrainGraphNode node;
        node.id = m_nextNodeId++;
        node.type = type;
        m_graphNodes.push_back(node);
    };

    m_nextNodeId = 1;
    m_nextLinkId = 1;
    switch (style) {
        case ECS::TerrainGenStyle::Realistic:
            add(TerrainGraphNodeType::Hybrid);
            add(TerrainGraphNodeType::Thermal);
            add(TerrainGraphNodeType::MicroSculpt);
            add(TerrainGraphNodeType::Output);
            break;
        case ECS::TerrainGenStyle::UltraRealistic:
            add(TerrainGraphNodeType::Hybrid);
            add(TerrainGraphNodeType::GeoSim);
            add(TerrainGraphNodeType::MicroSculpt);
            add(TerrainGraphNodeType::Output);
            break;
        case ECS::TerrainGenStyle::LowPoly:
            add(TerrainGraphNodeType::Noise);
            add(TerrainGraphNodeType::Output);
            break;
        case ECS::TerrainGenStyle::Stylized:
            add(TerrainGraphNodeType::Ridged);
            add(TerrainGraphNodeType::Smooth);
            add(TerrainGraphNodeType::Output);
            break;
        case ECS::TerrainGenStyle::Custom:
            add(TerrainGraphNodeType::Noise);
            add(TerrainGraphNodeType::Thermal);
            add(TerrainGraphNodeType::Smooth);
            add(TerrainGraphNodeType::Output);
            break;
    }

    for (size_t i = 0; i + 1 < m_graphNodes.size(); ++i) {
        TerrainGraphLink link;
        link.id = m_nextLinkId++;
        link.fromAttr = nodeOutputAttr(m_graphNodes[i].id);
        link.toAttr = nodeInputAttr(m_graphNodes[i + 1].id);
        m_graphLinks.push_back(link);
    }

    layoutGraphNodes();
}

void TerrainEditorPanel::layoutGraphNodes() {
    constexpr f32 kStartX = 48.0f;
    constexpr f32 kStartY = 96.0f;
    constexpr f32 kXSpacing = 260.0f;
    constexpr f32 kYSpacing = 150.0f;
    constexpr int kColumns = 2;

    for (size_t i = 0; i < m_graphNodes.size(); ++i) {
        const int col = static_cast<int>(i % kColumns);
        const int row = static_cast<int>(i / kColumns);
        m_graphNodes[i].posX = kStartX + static_cast<f32>(col) * kXSpacing;
        m_graphNodes[i].posY = kStartY + static_cast<f32>(row) * kYSpacing;
    }
}

void TerrainEditorPanel::applyGraphToSettings(ECS::TerrainGenerationSettings& gen) const {
    gen.microSculpt = false;
    gen.microRiverCarve = 0.0f;
    gen.thermalErosion = false;
    gen.hydraulicErosion = false;
    gen.hydrology.traceRivers = false;
    gen.smoothPass = false;
    gen.slopeWeighting = false;
    gen.simulation.enabled = false;

    int outputId = 0;
    for (const auto& node : m_graphNodes) {
        if (node.type == TerrainGraphNodeType::Output) outputId = node.id;
    }
    if (outputId == 0) return;

    auto findNode = [&](int id) -> const TerrainGraphNode* {
        for (const auto& node : m_graphNodes) {
            if (node.id == id) return &node;
        }
        return nullptr;
    };
    auto sourceOf = [&](int nodeId) -> int {
        const int inAttr = nodeInputAttr(nodeId);
        for (const auto& link : m_graphLinks) {
            if (link.toAttr == inAttr) {
                return nodeIdFromAttr(link.fromAttr);
            }
        }
        return 0;
    };

    int current = outputId;
    for (int hop = 0; hop < 32 && current != 0; ++hop) {
        const TerrainGraphNode* node = findNode(current);
        if (!node) break;
        switch (node->type) {
            case TerrainGraphNodeType::Noise:
                gen.heightModel = Terrain::TerrainHeightModel::FractalFBM;
                gen.useRidgedNoise = false;
                break;
            case TerrainGraphNodeType::Ridged:
                gen.heightModel = Terrain::TerrainHeightModel::RidgedMountains;
                gen.useRidgedNoise = true;
                break;
            case TerrainGraphNodeType::Hybrid:
                gen.heightModel = Terrain::TerrainHeightModel::Hybrid;
                gen.useRidgedNoise = true;
                gen.ridgedBlend = std::max(gen.ridgedBlend, 0.4f);
                break;
            case TerrainGraphNodeType::DiamondSquare:
                gen.heightModel = Terrain::TerrainHeightModel::DiamondSquare;
                gen.domainWarp = false;
                break;
            case TerrainGraphNodeType::SpectralFFT:
                gen.heightModel = Terrain::TerrainHeightModel::SpectralFFT;
                gen.domainWarp = false;
                break;
            case TerrainGraphNodeType::Thermal:
                gen.thermalErosion = true;
                gen.thermalIterations = std::max(12u, gen.thermalIterations);
                break;
            case TerrainGraphNodeType::MicroSculpt:
                gen.microSculpt = true;
                break;
            case TerrainGraphNodeType::GeoSim:
                gen.simulation.enabled = true;
                gen.thermalErosion = false;
                gen.hydraulicErosion = false;
                gen.hydrology.traceRivers = false;
                break;
            case TerrainGraphNodeType::Rivers:
                gen.microRiverCarve = std::max(gen.microRiverCarve, 0.35f);
                gen.hydrology.traceRivers = true;
                gen.hydraulicErosion = true;
                gen.hydraulicIterations = std::max(gen.hydraulicIterations, 4000u);
                break;
            case TerrainGraphNodeType::Smooth:
                gen.smoothPass = true;
                gen.smoothIterations = std::max(1u, gen.smoothIterations);
                break;
            case TerrainGraphNodeType::Output:
                break;
        }
        current = sourceOf(current);
    }
}

void TerrainEditorPanel::generateFromGraph(ECS::World& world, ECS::Entity entity,
                                           ECS::TerrainComponent& terrain, EditorContext& ctx) {
    applyGraphToSettings(terrain.generation);
    Terrain::TerrainCache::instance().generateTerrain(world, entity, terrain);
    ctx.isDirty = true;
}

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
    auto& gen = terrain->generation;

    static const char* styleNames[] = {
        "Realistic", "Low Poly", "Stylized", "Custom", "Ultra Realistic"
    };
    int style = static_cast<int>(gen.style);
    if (ImGui::Combo("Preset", &style, styleNames, 5)) {
        gen.style = static_cast<ECS::TerrainGenStyle>(style);
        Terrain::applyGenerationStylePreset(gen, gen.style);
        if (gen.style == ECS::TerrainGenStyle::UltraRealistic) {
            copyCString(terrain->texturePath, sizeof(terrain->texturePath),
                        Assets::kUltraRealisticAlbedo);
            for (u32 i = 0; i < ECS::kTerrainSplatLayerCount; ++i) {
                copyCString(terrain->splatLayerPaths[i], sizeof(terrain->splatLayerPaths[i]),
                            Assets::kUltraRealisticSplatLayers[i]);
            }
            terrain->useSplatmap = true;
            terrain->textureTileSize = 12.0f;
            terrain->splatTileSize = 12.0f;
            terrain->splatRevision++;
        }
        if (gen.style != ECS::TerrainGenStyle::Custom) {
            syncGraphFromStyle(gen.style);
        }
        generateFromGraph(world, entity, *terrain, ctx);
        ctx.isDirty = true;
    }
    ImGui::SameLine();
    int seed = static_cast<int>(gen.seed);
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::InputInt("Seed", &seed)) {
        gen.seed = static_cast<u32>(std::max(0, seed));
        ctx.isDirty = true;
    }
    if (ImGui::Button("Gerar")) {
        generateFromGraph(world, entity, *terrain, ctx);
    }
    ImGui::SameLine();
    if (ImGui::Button("Aplanar")) {
        if (auto* heightmap = Terrain::TerrainCache::instance().heightmapFor(entity)) {
            heightmap->fill(0.0f);
            terrain->dataRevision++;
            Terrain::TerrainCache::instance().syncEntity(world, entity);
            ctx.isDirty = true;
        }
    }

    ImGui::TextDisabled("Liga geradores a filtros e depois a Output. Clique Gerar para aplicar.");
    drawGenerationGraph(world, entity, ctx);

    ImGui::SeparatorText("HDR Ground");
    static int hdrGroundPreset = 0;
    hdrGroundPreset = std::clamp(hdrGroundPreset, 0, Assets::kHdrGroundPresetCount - 1);
    if (ImGui::Combo("Solo", &hdrGroundPreset, Assets::kHdrGroundPresetLabels,
                     Assets::kHdrGroundPresetCount)) {
        applyHdrGroundPreset(*terrain, hdrGroundPreset);
        Terrain::TerrainCache::instance().syncTextureToFilter(world, entity, *terrain);
#ifdef CF_HAS_SDL3
        Terrain::TerrainGpuTextureCache::instance().invalidateEntity(entity, nullptr);
#endif
        terrain->splatRevision++;
        ctx.isDirty = true;
    }
}

void TerrainEditorPanel::drawGenerationGraph(ECS::World& world, ECS::Entity entity,
                                             EditorContext& ctx) {
    auto* terrain = world.get<ECS::TerrainComponent>(entity);
    if (!terrain) return;
    ensureDefaultGraph();

    if (m_editorContext) {
        ImNodes::EditorContextSet(m_editorContext);
    }
    ImNodes::GetIO().AutoPanningSpeed = 0.0f;

    if (ImGui::Button("Adicionar node")) {
        ImGui::OpenPopup("##add_terrain_node");
    }
    ImGui::SameLine();
    if (ImGui::Button("Reorganizar")) {
        layoutGraphNodes();
    }
    if (ImGui::BeginPopup("##add_terrain_node")) {
        auto add = [&](TerrainGraphNodeType type) {
            TerrainGraphNode node;
            node.id = m_nextNodeId++;
            node.type = type;
            node.posX = 24.0f + static_cast<f32>(m_graphNodes.size()) * 36.0f;
            node.posY = 48.0f + static_cast<f32>(m_graphNodes.size()) * 12.0f;
            m_graphNodes.push_back(node);
            ImNodes::SetNodeEditorSpacePos(node.id, ImVec2(node.posX, node.posY));
            ImGui::CloseCurrentPopup();
        };
        if (ImGui::MenuItem("Noise FBM")) add(TerrainGraphNodeType::Noise);
        if (ImGui::MenuItem("Ridged")) add(TerrainGraphNodeType::Ridged);
        if (ImGui::MenuItem("Hybrid")) add(TerrainGraphNodeType::Hybrid);
        if (ImGui::MenuItem("Diamond-Square")) add(TerrainGraphNodeType::DiamondSquare);
        if (ImGui::MenuItem("FFT Spectral")) add(TerrainGraphNodeType::SpectralFFT);
        if (ImGui::MenuItem("Thermal Erosion")) add(TerrainGraphNodeType::Thermal);
        if (ImGui::MenuItem("Micro Sculpt")) add(TerrainGraphNodeType::MicroSculpt);
        if (ImGui::MenuItem("Rivers")) add(TerrainGraphNodeType::Rivers);
        if (ImGui::MenuItem("Geo Simulation")) add(TerrainGraphNodeType::GeoSim);
        if (ImGui::MenuItem("Smooth")) add(TerrainGraphNodeType::Smooth);
        ImGui::EndPopup();
    }

    const f32 canvasHeight =
        std::clamp(ImGui::GetContentRegionAvail().y * 0.58f, 240.0f, 520.0f);
    ImGui::PushID("##terrain_graph_editor");
    ImGui::BeginChild("##terrain_graph_canvas", ImVec2(0.0f, canvasHeight), true,
                      ImGuiWindowFlags_NoScrollbar);

    if (m_graphLayoutDirty) {
        layoutGraphNodes();
        m_graphLayoutDirty = false;
    }

    ImNodes::BeginNodeEditor();
    for (const auto& node : m_graphNodes) {
        ImNodes::SetNodeEditorSpacePos(node.id, ImVec2(node.posX, node.posY));
        ImNodes::BeginNode(node.id);
        ImNodes::BeginNodeTitleBar();
        ImGui::TextUnformatted(nodeTitle(node.type));
        ImNodes::EndNodeTitleBar();

        if (!isGeneratorNode(node.type)) {
            ImNodes::BeginInputAttribute(nodeInputAttr(node.id));
            ImGui::TextUnformatted("in");
            ImNodes::EndInputAttribute();
        }
        if (node.type != TerrainGraphNodeType::Output) {
            ImNodes::BeginOutputAttribute(nodeOutputAttr(node.id));
            ImGui::TextUnformatted("out");
            ImNodes::EndOutputAttribute();
        }
        ImNodes::EndNode();
    }
    for (const auto& link : m_graphLinks) {
        ImNodes::Link(link.id, link.fromAttr, link.toAttr);
    }
    ImNodes::EndNodeEditor();

    for (auto& node : m_graphNodes) {
        const ImVec2 pos = ImNodes::GetNodeEditorSpacePos(node.id);
        node.posX = pos.x;
        node.posY = pos.y;
    }

    int startAttr = 0;
    int endAttr = 0;
    if (ImNodes::IsLinkCreated(&startAttr, &endAttr)) {
        TerrainGraphLink link;
        link.id = m_nextLinkId++;
        link.fromAttr = startAttr;
        link.toAttr = endAttr;
        m_graphLinks.push_back(link);
        ctx.isDirty = true;
    }
    int destroyed = 0;
    if (ImNodes::IsLinkDestroyed(&destroyed)) {
        m_graphLinks.erase(std::remove_if(m_graphLinks.begin(), m_graphLinks.end(),
                                          [destroyed](const TerrainGraphLink& link) {
                                              return link.id == destroyed;
                                          }),
                           m_graphLinks.end());
        ctx.isDirty = true;
    }

    const int selectedCount = ImNodes::NumSelectedNodes();
    if (selectedCount == 1) {
        std::vector<int> selectedIds(static_cast<size_t>(selectedCount));
        ImNodes::GetSelectedNodes(selectedIds.data());
        m_selectedNodeId = selectedIds[0];
    } else if (selectedCount == 0) {
        m_selectedNodeId = 0;
    }

    ImGui::EndChild();
    ImGui::PopID();

    if (m_selectedNodeId != 0) {
        TerrainGraphNode* selected = nullptr;
        for (auto& node : m_graphNodes) {
            if (node.id == m_selectedNodeId) selected = &node;
        }
        if (selected) {
            auto& gen = terrain->generation;
            ImGui::SeparatorText(nodeTitle(selected->type));
            if (selected->type == TerrainGraphNodeType::Noise ||
                selected->type == TerrainGraphNodeType::Ridged ||
                selected->type == TerrainGraphNodeType::Hybrid) {
                static const char* noiseNames[] = {"Perlin", "Simplex", "Value", "Worley"};
                int noiseType = static_cast<int>(gen.noiseAlgorithm);
                if (ImGui::Combo("Sampler", &noiseType, noiseNames, 4)) {
                    gen.noiseAlgorithm = static_cast<Terrain::TerrainNoiseAlgorithm>(noiseType);
                    ctx.isDirty = true;
                }
                if (ImGui::DragFloat("Escala", &gen.noiseScale, 0.001f, 0.005f, 0.2f, "%.3f")) {
                    ctx.isDirty = true;
                }
                int octaves = static_cast<int>(gen.octaves);
                if (ImGui::SliderInt("Octaves", &octaves, 1, 8)) {
                    gen.octaves = static_cast<u32>(octaves);
                    ctx.isDirty = true;
                }
            }
            if (selected->type == TerrainGraphNodeType::DiamondSquare) {
                if (ImGui::SliderFloat("Rugosidade", &gen.fractalRoughness, 0.1f, 0.9f)) {
                    ctx.isDirty = true;
                }
            }
            if (selected->type == TerrainGraphNodeType::SpectralFFT) {
                if (ImGui::SliderFloat("Expoente", &gen.spectralExponent, 1.0f, 3.5f)) {
                    ctx.isDirty = true;
                }
            }
            if (selected->type == TerrainGraphNodeType::MicroSculpt) {
                if (ImGui::SliderFloat("Strength", &gen.microSculptStrength, 0.0f, 1.0f)) {
                    ctx.isDirty = true;
                }
                if (ImGui::SliderFloat("Softness", &gen.microSculptSoftness, 0.0f, 1.0f)) {
                    ctx.isDirty = true;
                }
                if (ImGui::SliderFloat("Rivers", &gen.microRiverCarve, 0.0f, 1.0f)) {
                    ctx.isDirty = true;
                }
            }
            if (selected->type == TerrainGraphNodeType::GeoSim) {
                ImGui::TextDisabled("Mais lento, mais detalhe geologico.");
                int iterations = static_cast<int>(gen.simulation.totalIterations);
                if (ImGui::SliderInt("Iterations", &iterations, 80, 800)) {
                    gen.simulation.totalIterations = static_cast<u32>(iterations);
                    ctx.isDirty = true;
                }
                int droplets = static_cast<int>(gen.simulation.dropletsPerIteration);
                if (ImGui::SliderInt("Droplets", &droplets, 4, 32)) {
                    gen.simulation.dropletsPerIteration = static_cast<u32>(droplets);
                    ctx.isDirty = true;
                }
                if (ImGui::SliderFloat("Tectonic", &gen.simulation.tectonicActivity, 0.0f, 1.0f)) {
                    ctx.isDirty = true;
                }
                if (ImGui::SliderFloat("Water Erosion", &gen.simulation.erosionWater, 0.0f, 1.0f)) {
                    ctx.isDirty = true;
                }
            }
            if (selected->type == TerrainGraphNodeType::Thermal) {
                int thermalIters = static_cast<int>(gen.thermalIterations);
                if (ImGui::SliderInt("Iterations", &thermalIters, 1, 48)) {
                    gen.thermalIterations = static_cast<u32>(thermalIters);
                    ctx.isDirty = true;
                }
            }
            if (ImGui::Button("Apagar node") && selected->type != TerrainGraphNodeType::Output) {
                const int id = selected->id;
                m_graphLinks.erase(std::remove_if(m_graphLinks.begin(), m_graphLinks.end(),
                                                  [id](const TerrainGraphLink& link) {
                                                      return nodeIdFromAttr(link.fromAttr) == id ||
                                                             nodeIdFromAttr(link.toAttr) == id;
                                                  }),
                                   m_graphLinks.end());
                m_graphNodes.erase(std::remove_if(m_graphNodes.begin(), m_graphNodes.end(),
                                                  [id](const TerrainGraphNode& node) {
                                                      return node.id == id;
                                                  }),
                                   m_graphNodes.end());
                m_selectedNodeId = 0;
            }
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
