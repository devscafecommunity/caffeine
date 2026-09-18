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

void applyHdrGroundPreset(ECS::TerrainComponent& terrain, int presetIndex) {
    presetIndex = std::clamp(presetIndex, 0, Assets::kHdrGroundPresetCount - 1);
    const char* grass = Assets::kHdrGroundPresetPaths[presetIndex];
    constexpr const char* kRock =
        "hdr-assets-texture/Ground108_1K-JPG/Ground108_1K-JPG_Color.jpg";
    constexpr const char* kSand =
        "hdr-assets-texture/Ground079L_1K-JPG/Ground079L_1K-JPG_Color.jpg";
    constexpr const char* kDirt =
        "hdr-assets-texture/Ground051_1K-JPG/Ground051_1K-JPG_Color.jpg";

    copyCString(terrain.texturePath, sizeof(terrain.texturePath), grass);
    copyCString(terrain.splatLayerPaths[0], sizeof(terrain.splatLayerPaths[0]), grass);
    copyCString(terrain.splatLayerPaths[1], sizeof(terrain.splatLayerPaths[1]), kRock);
    copyCString(terrain.splatLayerPaths[2], sizeof(terrain.splatLayerPaths[2]), kSand);
    copyCString(terrain.splatLayerPaths[3], sizeof(terrain.splatLayerPaths[3]), kDirt);
    terrain.textureTileSize = 4.0f;
    terrain.splatTileSize = 4.0f;
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

    auto& gen = terrain->generation;
    auto markCustom = [&]() {
        gen.style = ECS::TerrainGenStyle::Custom;
        ctx.isDirty = true;
    };

    constexpr ImGuiTreeNodeFlags sectionOpen = ImGuiTreeNodeFlags_DefaultOpen;

    // ── Preset & ações ──────────────────────────────────────────
    if (ImGui::CollapsingHeader("Preset e Acoes", sectionOpen)) {
        static const char* styleNames[] = {"Realistic", "Low Poly", "Stylized", "Custom"};
        int style = static_cast<int>(gen.style);
        if (ImGui::Combo("Estilo", &style, styleNames, 4)) {
            gen.style = static_cast<ECS::TerrainGenStyle>(style);
            if (gen.style != ECS::TerrainGenStyle::Custom) {
                Terrain::applyGenerationStylePreset(gen, gen.style);
            }
            ctx.isDirty = true;
        }

        int seed = static_cast<int>(gen.seed);
        if (ImGui::InputInt("Seed", &seed)) {
            gen.seed = static_cast<u32>(std::max(0, seed));
            ctx.isDirty = true;
        }

        if (ImGui::Button("Defaults de Qualidade")) {
            terrain->resolutionX = 257;
            terrain->resolutionZ = 257;
            terrain->splatResolutionScale = 2;
            Terrain::applyGenerationStylePreset(gen, ECS::TerrainGenStyle::Realistic);
            gen.simulation.enabled = false;
            ctx.isDirty = true;
        }
        if (ImGui::Button("Preset Fractal")) {
            Terrain::applyGenerationStylePreset(gen, ECS::TerrainGenStyle::Realistic);
            gen.heightModel = Terrain::TerrainHeightModel::DiamondSquare;
            gen.domainWarp = false;
            gen.fractalRoughness = 0.55f;
            gen.style = ECS::TerrainGenStyle::Custom;
            ctx.isDirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Preset FFT")) {
            Terrain::applyGenerationStylePreset(gen, ECS::TerrainGenStyle::Realistic);
            gen.heightModel = Terrain::TerrainHeightModel::SpectralFFT;
            gen.domainWarp = false;
            gen.spectralExponent = 2.0f;
            gen.slopeWeightAlpha = 0.04f;
            gen.style = ECS::TerrainGenStyle::Custom;
            ctx.isDirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Gerar Terreno")) {
            Terrain::TerrainCache::instance().generateTerrain(world, entity, *terrain);
            ctx.isDirty = true;
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

        ImGui::TextDisabled("Resolucao do mesh: Inspector > Terrain > Resolution");
    }

    // ── 1. Algoritmo de altura ───────────────────────────────────
    if (ImGui::CollapsingHeader("1. Algoritmo de Altura", sectionOpen)) {
        static const char* algoFamilyNames[] = {"Noise (Perlin/Simplex/FBM)",
                                                "Fractal (Diamond-Square)",
                                                "Espectral (FFT 1/f)",
                                                "Combinador (2 campos)"};
        int algoFamily = 0;
        switch (gen.heightModel) {
            case Terrain::TerrainHeightModel::DiamondSquare:
                algoFamily = 1;
                break;
            case Terrain::TerrainHeightModel::SpectralFFT:
                algoFamily = 2;
                break;
            case Terrain::TerrainHeightModel::HeightfieldMultiply:
                algoFamily = 3;
                break;
            default:
                algoFamily = 0;
                break;
        }
        if (ImGui::Combo("Familia", &algoFamily, algoFamilyNames, 4)) {
            switch (algoFamily) {
                case 1:
                    gen.heightModel = Terrain::TerrainHeightModel::DiamondSquare;
                    gen.domainWarp = false;
                    break;
                case 2:
                    gen.heightModel = Terrain::TerrainHeightModel::SpectralFFT;
                    gen.domainWarp = false;
                    break;
                case 3:
                    gen.heightModel = Terrain::TerrainHeightModel::HeightfieldMultiply;
                    break;
                default:
                    gen.heightModel = Terrain::TerrainHeightModel::RollingHills;
                    break;
            }
            markCustom();
        }

        if (algoFamily == 0) {
            static const char* heightModelNames[] = {"Colinas (FBM)", "FBM Fractal",
                                                     "Montanhas (Ridged)", "Multiplicativo (octaves)",
                                                     "Hibrido (FBM+Ridged)", "Combinado (FBM+Mult)"};
            int heightModel = static_cast<int>(gen.heightModel);
            heightModel = std::clamp(heightModel, 0, 5);
            if (ImGui::Combo("Modelo de Noise", &heightModel, heightModelNames, 6)) {
                gen.heightModel = static_cast<Terrain::TerrainHeightModel>(heightModel);
                markCustom();
            }

            static const char* noiseNames[] = {"Perlin", "Simplex", "Value", "Worley"};
            int noiseType = static_cast<int>(gen.noiseAlgorithm);
            if (ImGui::Combo("Sampler", &noiseType, noiseNames, 4)) {
                gen.noiseAlgorithm = static_cast<Terrain::TerrainNoiseAlgorithm>(noiseType);
                markCustom();
            }
            ImGui::TextDisabled("Simplex: isotropico e rapido. Perlin: classico CGI.");
        } else if (algoFamily == 1) {
            ImGui::TextDisabled("Midpoint displacement / Diamond-Square (Bird et al.).");
            if (ImGui::SliderFloat("Rugosidade (H)", &gen.fractalRoughness, 0.1f, 0.9f)) {
                markCustom();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Maior H = terreno mais suave. Menor H = picos mais acentuados.");
            }
        } else if (algoFamily == 2) {
            ImGui::TextDisabled("FFT + filtro 1/f^beta — colinas suaves e tileable.");
            if (ImGui::SliderFloat("Expoente Espectral", &gen.spectralExponent, 1.0f, 3.5f)) {
                markCustom();
            }
        } else {
            ImGui::TextDisabled("Multiplica dois heightmaps: vales suaves, picos acentuados.");
            static const char* layerNames[] = {"Colinas (FBM)", "FBM Fractal", "Montanhas (Ridged)",
                                               "Multiplicativo", "Hibrido", "Combinado"};
            int layerA = static_cast<int>(gen.multiplyLayerA);
            int layerB = static_cast<int>(gen.multiplyLayerB);
            layerA = std::clamp(layerA, 0, 5);
            layerB = std::clamp(layerB, 0, 5);
            if (ImGui::Combo("Camada A", &layerA, layerNames, 6)) {
                gen.multiplyLayerA = static_cast<Terrain::TerrainHeightModel>(layerA);
                markCustom();
            }
            if (ImGui::Combo("Camada B", &layerB, layerNames, 6)) {
                gen.multiplyLayerB = static_cast<Terrain::TerrainHeightModel>(layerB);
                markCustom();
            }
            if (ImGui::SliderFloat("Contraste do Produto", &gen.multiplicativeContrast, 0.5f, 3.0f)) {
                markCustom();
            }
        }

        ImGui::SeparatorText("Parametros globais");
        if (ImGui::DragFloat("Escala", &gen.noiseScale, 0.001f, 0.001f, 0.2f, "%.3f")) {
            markCustom();
        }
        if (algoFamily == 0 || algoFamily == 3) {
            int octaves = static_cast<int>(gen.octaves);
            if (ImGui::SliderInt("Octaves", &octaves, 1, 8)) {
                gen.octaves = static_cast<u32>(octaves);
                markCustom();
            }
            if (ImGui::SliderFloat("Persistence", &gen.persistence, 0.1f, 1.0f)) {
                markCustom();
            }
            if (ImGui::SliderFloat("Lacunarity", &gen.lacunarity, 1.1f, 4.0f)) {
                markCustom();
            }
        }
        if (ImGui::SliderFloat("Amplitude", &gen.amplitude, 0.05f, 1.0f)) {
            markCustom();
        }
        if (ImGui::SliderFloat("Altura Base", &gen.baseHeight, 0.0f, 0.8f)) {
            markCustom();
        }

        if (algoFamily == 0) {
            const bool showRidge =
                gen.heightModel == Terrain::TerrainHeightModel::RidgedMountains ||
                gen.heightModel == Terrain::TerrainHeightModel::Hybrid;
            const bool showBlend = gen.heightModel == Terrain::TerrainHeightModel::Hybrid ||
                                   gen.heightModel == Terrain::TerrainHeightModel::Combined;
            const bool showMult = gen.heightModel == Terrain::TerrainHeightModel::Multiplicative ||
                                  gen.heightModel == Terrain::TerrainHeightModel::Combined;

            if (showRidge || showBlend || showMult) {
                ImGui::SeparatorText("Parametros do modelo");
            }
            if (showRidge) {
                if (ImGui::SliderFloat("Ridge Offset", &gen.ridgeOffset, 0.5f, 1.5f)) {
                    markCustom();
                }
                if (ImGui::SliderFloat("Ridge Gain", &gen.ridgeGain, 0.5f, 2.5f)) {
                    markCustom();
                }
            }
            if (showBlend) {
                if (ImGui::SliderFloat("Blend de Camadas", &gen.ridgedBlend, 0.0f, 1.0f)) {
                    markCustom();
                }
            }
            if (showMult) {
                if (ImGui::SliderFloat("Contraste da Mascara", &gen.multiplicativeContrast, 0.5f, 3.0f)) {
                    markCustom();
                }
            }
        }

        if (algoFamily == 3) {
            static const char* noiseNames[] = {"Perlin", "Simplex", "Value", "Worley"};
            int noiseType = static_cast<int>(gen.noiseAlgorithm);
            if (ImGui::Combo("Sampler das Camadas", &noiseType, noiseNames, 4)) {
                gen.noiseAlgorithm = static_cast<Terrain::TerrainNoiseAlgorithm>(noiseType);
                markCustom();
            }
        }
    }

    // ── 2. Domain Warp ──────────────────────────────────────────
    if (ImGui::CollapsingHeader("2. Domain Warp", 0)) {
        if (ImGui::Checkbox("Ativar", &gen.domainWarp)) {
            markCustom();
        }
        if (gen.domainWarp) {
            if (ImGui::Checkbox("Warp Fractal", &gen.fractalDomainWarp)) {
                markCustom();
            }
            if (ImGui::SliderFloat("Forca", &gen.domainWarpStrength, 0.0f, 1.5f)) {
                markCustom();
            }
            if (gen.fractalDomainWarp) {
                if (ImGui::SliderFloat("Escala do Warp", &gen.domainWarpScale, 8.0f, 128.0f)) {
                    markCustom();
                }
            } else {
                int warpPasses = static_cast<int>(gen.domainWarpPasses);
                if (ImGui::SliderInt("Passagens", &warpPasses, 1, 3)) {
                    gen.domainWarpPasses = static_cast<u32>(warpPasses);
                    markCustom();
                }
            }
        }
    }

    // ── 3. Erosao rapida ────────────────────────────────────────
    if (ImGui::CollapsingHeader("3. Erosao Rapida", sectionOpen)) {
        const bool geoSim = gen.simulation.enabled;
        if (geoSim) {
            ImGui::TextDisabled("Desativada: Simulacao Geologica esta ativa.");
        }

        if (ImGui::Checkbox("Erosao Termica", &gen.thermalErosion)) {
            markCustom();
        }
        if (gen.thermalErosion && !geoSim) {
            int thermalIters = static_cast<int>(gen.thermalIterations);
            if (ImGui::SliderInt("Iteracoes Termicas", &thermalIters, 0, 100)) {
                gen.thermalIterations = static_cast<u32>(thermalIters);
                ctx.isDirty = true;
            }
            if (ImGui::SliderFloat("Talus", &gen.thermalTalus, 0.001f, 0.05f, "%.3f")) {
                markCustom();
            }
        }

        ImGui::Separator();

        if (ImGui::Checkbox("Rastrear Rios", &gen.hydrology.traceRivers)) {
            markCustom();
        }
        if (gen.hydrology.traceRivers && !geoSim) {
            ImGui::TextDisabled("Substitui erosao hidraulica por gotas.");
            int riverSources = static_cast<int>(gen.hydrology.maxRiverSources);
            if (ImGui::SliderInt("Nascentes Max", &riverSources, 8, 128)) {
                gen.hydrology.maxRiverSources = static_cast<u32>(riverSources);
                ctx.isDirty = true;
            }
            if (ImGui::SliderFloat("Forca de Escavação", &gen.hydrology.riverCarveStrength, 0.00001f,
                                   0.001f, "%.5f")) {
                markCustom();
            }
        }

        const bool riversActive = gen.hydrology.traceRivers && !geoSim;
        ImGui::BeginDisabled(riversActive || geoSim);
        if (ImGui::Checkbox("Erosao Hidraulica (gotas)", &gen.hydraulicErosion)) {
            markCustom();
        }
        if (gen.hydraulicErosion) {
            int hydroIters = static_cast<int>(gen.hydraulicIterations);
            if (ImGui::SliderInt("Gotas", &hydroIters, 0, 20000)) {
                gen.hydraulicIterations = static_cast<u32>(hydroIters);
                ctx.isDirty = true;
            }
            int hydroSteps = static_cast<int>(gen.hydraulicMaxSteps);
            if (ImGui::SliderInt("Passos por Gota", &hydroSteps, 16, 200)) {
                gen.hydraulicMaxSteps = static_cast<u32>(hydroSteps);
                ctx.isDirty = true;
            }
            if (ImGui::SliderFloat("Inercia", &gen.hydraulicInertia, 0.0f, 1.0f)) {
                markCustom();
            }
            if (ImGui::SliderFloat("Evaporacao", &gen.hydraulicEvaporation, 0.01f, 0.2f)) {
                markCustom();
            }
        }
        ImGui::EndDisabled();
    }

    // ── 4. Simulacao geologica (offline) ────────────────────────
    if (ImGui::CollapsingHeader("4. Simulacao Geologica (offline)", 0)) {
        auto& sim = gen.simulation;
        if (ImGui::Checkbox("Ativar", &sim.enabled)) {
            markCustom();
        }
        if (sim.enabled) {
            ImGui::TextDisabled("Substitui erosao rapida + rios por iteracoes longas.");

            static const char* envNames[] = {"Custom", "Tropical", "Deserto", "Alpino",
                                             "Costeiro", "Vulcanico"};
            int env = static_cast<int>(sim.environment);
            if (ImGui::Combo("Ambiente", &env, envNames, 6)) {
                sim.environment = static_cast<ECS::TerrainEnvironment>(env);
                if (sim.environment != ECS::TerrainEnvironment::Custom) {
                    Terrain::applyEnvironmentPreset(gen, sim.environment);
                }
                markCustom();
            }

            int totalIters = static_cast<int>(sim.totalIterations);
            if (ImGui::SliderInt("Iteracoes", &totalIters, 50, 800)) {
                sim.totalIterations = static_cast<u32>(totalIters);
                ctx.isDirty = true;
            }
            int droplets = static_cast<int>(sim.dropletsPerIteration);
            if (ImGui::SliderInt("Gotas / Iteracao", &droplets, 4, 64)) {
                sim.dropletsPerIteration = static_cast<u32>(droplets);
                ctx.isDirty = true;
            }

            ImGui::SeparatorText("Forcas");
            if (ImGui::SliderFloat("Agua", &sim.erosionWater, 0.0f, 1.0f)) ctx.isDirty = true;
            if (ImGui::SliderFloat("Termica", &sim.erosionThermal, 0.0f, 1.0f)) ctx.isDirty = true;
            if (ImGui::SliderFloat("Glacial", &sim.erosionGlacial, 0.0f, 1.0f)) ctx.isDirty = true;
            if (ImGui::SliderFloat("Vento", &sim.erosionWind, 0.0f, 1.0f)) ctx.isDirty = true;
            if (ImGui::SliderFloat("Tectonica", &sim.tectonicActivity, 0.0f, 1.0f)) {
                ctx.isDirty = true;
            }
            if (ImGui::Checkbox("Convergencia Auto", &sim.autoConvergence)) {
                ctx.isDirty = true;
            }
        }
    }

    // ── 5. Pos-processamento ────────────────────────────────────
    if (ImGui::CollapsingHeader("5. Pos-processamento", sectionOpen)) {
        if (ImGui::Checkbox("Slope Weighting (exp)", &gen.slopeWeighting)) {
            markCustom();
        }
        if (gen.slopeWeighting) {
            if (ImGui::SliderFloat("Slope Alpha", &gen.slopeWeightAlpha, 0.0f, 0.6f)) {
                markCustom();
            }
            ImGui::TextDisabled("Maior alpha = colinas mais suaves e caminhaveis.");
        }

        ImGui::Separator();

        if (ImGui::Checkbox("Suavizar", &gen.smoothPass)) {
            markCustom();
        }
        if (gen.smoothPass) {
            int smoothIters = static_cast<int>(gen.smoothIterations);
            if (ImGui::SliderInt("Passagens de Suavizacao", &smoothIters, 1, 8)) {
                gen.smoothIterations = static_cast<u32>(smoothIters);
                ctx.isDirty = true;
            }
        }

        if (ImGui::SliderFloat("Quantizar Altura", &gen.heightQuantize, 0.0f, 24.0f, "%.0f")) {
            markCustom();
        }
    }

    // ── 6. Clima & biomas ────────────────────────────────────────
    if (ImGui::CollapsingHeader("6. Clima e Biomas", sectionOpen)) {
        if (ImGui::Checkbox("Biomas por Clima", &gen.useClimateBiomes)) {
            ctx.isDirty = true;
        }
        if (gen.useClimateBiomes) {
            ImGui::TextDisabled("Splat usa elevacao + umidade + temperatura.");
        }

        ImGui::SeparatorText("Clima");
        if (ImGui::SliderFloat("Vento (graus)", &gen.climate.prevailingWindAngle, 0.0f, 360.0f)) {
            ctx.isDirty = true;
        }
        if (ImGui::SliderFloat("Umidade Base", &gen.climate.baseHumidity, 0.0f, 1.0f)) {
            ctx.isDirty = true;
        }
        if (ImGui::SliderFloat("Temperatura", &gen.climate.temperature, 0.0f, 1.0f)) {
            ctx.isDirty = true;
        }
        if (ImGui::SliderFloat("Nivel do Mar", &gen.climate.seaLevel, 0.2f, 0.5f)) {
            ctx.isDirty = true;
        }
        int rainShadow = static_cast<int>(gen.hydrology.rainShadowSteps);
        if (ImGui::SliderInt("Passos Rain Shadow", &rainShadow, 4, 40)) {
            gen.hydrology.rainShadowSteps = static_cast<u32>(rainShadow);
            ctx.isDirty = true;
        }
    }

    // ── 7. Texturas (splat) ─────────────────────────────────────
    if (ImGui::CollapsingHeader("7. Texturas (Splat)", sectionOpen)) {
        if (ImGui::Checkbox("Auto Splat", &gen.autoSplat)) {
            ctx.isDirty = true;
        }
        if (gen.autoSplat) {
            if (ImGui::SliderFloat("Blend entre Biomas", &gen.splatBlendRange, 0.1f, 0.5f)) {
                ctx.isDirty = true;
            }
            int blurPasses = static_cast<int>(gen.splatBlurPasses);
            if (ImGui::SliderInt("Blur do Splat", &blurPasses, 0, 4)) {
                gen.splatBlurPasses = static_cast<u32>(blurPasses);
                ctx.isDirty = true;
            }
        }
        ImGui::TextDisabled("Camadas: R=Grama G=Rocha B=Areia A=Neve");

        ImGui::SeparatorText("HDR Ground (PBR)");
        static int hdrGroundPreset = 0;
        hdrGroundPreset =
            std::clamp(hdrGroundPreset, 0, Assets::kHdrGroundPresetCount - 1);
        if (ImGui::Combo("Preset de Solo", &hdrGroundPreset, Assets::kHdrGroundPresetLabels,
                         Assets::kHdrGroundPresetCount)) {
            applyHdrGroundPreset(*terrain, hdrGroundPreset);
            Terrain::TerrainCache::instance().syncTextureToFilter(world, entity, *terrain);
#ifdef CF_HAS_SDL3
            Terrain::TerrainGpuTextureCache::instance().invalidateEntity(entity, nullptr);
#endif
            terrain->splatRevision++;
            ctx.isDirty = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Aplica texturas HDR ao terreno base e camadas de splat (grama, rocha, areia, "
                "terra).");
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
