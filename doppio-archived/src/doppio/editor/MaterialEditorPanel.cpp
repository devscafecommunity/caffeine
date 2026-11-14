#include "caffeine/ui/UI.hpp"
#include "editor/MaterialEditorPanel.hpp"

#include "assets/MaterialCache.hpp"
#include "assets/MaterialFile.hpp"
#include "assets/MaterialPresets.hpp"
#include "ecs/MeshComponents.hpp"
#include "editor/FilePicker.hpp"
#include "editor/InspectorWidgets.hpp"
#include "scene/EnvironmentSystem.hpp"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string_view>

namespace Caffeine::Editor {

MaterialEditorPanel::MaterialEditorPanel() {
    m_surface.valid = true;
    m_surface.name = "Material";
}

MaterialEditorPanel::~MaterialEditorPanel() = default;

#ifdef CF_HAS_SDL3
void MaterialEditorPanel::initGpu(RHI::RenderDevice* device) {
    m_previewRenderer.init(device);
}

void MaterialEditorPanel::shutdownGpu() {
    m_previewRenderer.shutdown();
}
#endif

std::filesystem::path MaterialEditorPanel::resolvedPath(const std::filesystem::path& path) const {
    if (path.empty() || path.is_absolute() || m_projectRoot.empty()) return path;
    return std::filesystem::path(m_projectRoot) / path;
}

void MaterialEditorPanel::publishLive() {
    if (m_materialPath.empty()) return;
    m_surface.valid = true;
    Assets::MaterialCache::instance().publish(m_materialPath.string(), m_projectRoot, m_surface);
}

bool MaterialEditorPanel::saveToPath(const std::filesystem::path& path) {
    std::filesystem::path target = path;
    if (target.extension() != ".mat") {
        target += ".mat";
    }
    if (!Assets::saveMaterialFile(target, m_surface)) {
        m_status = "Save failed.";
        return false;
    }
    if (!m_projectRoot.empty()) {
        std::error_code ec;
        const auto relative = std::filesystem::relative(target, m_projectRoot, ec);
        if (!ec && !relative.empty() && relative.generic_string().rfind("..") != 0) {
            m_materialPath = relative;
        } else {
            m_materialPath = target;
        }
    } else {
        m_materialPath = target;
    }
    publishLive();
    m_dirty = false;
    m_status.clear();
    m_refreshAssets = true;
    return true;
}

bool MaterialEditorPanel::saveCurrent() {
    if (m_materialPath.empty()) {
        m_pendingQuickSave = true;
        return false;
    }
    return saveToPath(resolvedPath(m_materialPath));
}

void MaterialEditorPanel::handleSaveShortcut() {
    m_pendingQuickSave = true;
}

std::filesystem::path MaterialEditorPanel::newMaterialPath(const EditorContext& ctx) const {
    const std::filesystem::path dir = !ctx.assetRootPath.empty()
                                          ? ctx.assetRootPath
                                          : (m_projectRoot.empty() ? std::filesystem::path("assets")
                                                                   : std::filesystem::path(m_projectRoot) / "assets" / "raw");
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    std::string stem = m_surface.name.empty() ? "Material" : m_surface.name;
    for (char& c : stem) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (!std::isalnum(uc) && c != '_' && c != '-') c = '_';
    }
    if (stem.empty()) stem = "Material";
    std::filesystem::path target = dir / (stem + ".mat");
    int suffix = 1;
    while (std::filesystem::exists(target, ec)) {
        target = dir / (stem + "_" + std::to_string(suffix++) + ".mat");
    }
    return target;
}

bool MaterialEditorPanel::ensureFile(EditorContext& ctx) {
    if (!m_materialPath.empty()) return true;
    const std::string adopted = materialOnSelection(ctx);
    if (!adopted.empty()) {
        m_materialPath = adopted;
        return true;
    }
    return saveToPath(newMaterialPath(ctx));
}

std::string MaterialEditorPanel::materialOnSelection(const EditorContext& ctx) const {
    if (!ctx.activeWorld) return {};
    ECS::World& world = *ctx.activeWorld;
    std::vector<ECS::Entity> targets = ctx.selectedEntities;
    if (targets.empty() && ctx.selectedEntity.isValid()) targets.push_back(ctx.selectedEntity);
    std::string found;
    for (ECS::Entity entity : targets) {
        if (!entity.isValid() || !world.has<ECS::MeshFilterComponent>(entity)) continue;
        const ECS::MeshFilterComponent* filter = world.get<ECS::MeshFilterComponent>(entity);
        if (!filter || filter->customMaterialPath.empty()) continue;
        if (found.empty()) found = filter->customMaterialPath;
        else if (found != filter->customMaterialPath) return {};
    }
    return found;
}

void MaterialEditorPanel::assignToSelection(EditorContext& ctx, bool force) {
    if (!ctx.activeWorld || m_materialPath.empty()) return;
    ECS::World& world = *ctx.activeWorld;
    const std::string path = m_materialPath.generic_string();
    std::vector<ECS::Entity> targets = ctx.selectedEntities;
    if (targets.empty() && ctx.selectedEntity.isValid()) targets.push_back(ctx.selectedEntity);
    int applied = 0;
    for (ECS::Entity entity : targets) {
        if (!entity.isValid() || !world.has<ECS::MeshFilterComponent>(entity)) continue;
        ECS::MeshFilterComponent* filter = world.get<ECS::MeshFilterComponent>(entity);
        if (!filter) continue;
        if (!force && !filter->customMaterialPath.empty() && filter->customMaterialPath != path) continue;
        filter->customMaterialPath = path;
        if (!world.has<ECS::MeshRendererComponent>(entity)) world.add<ECS::MeshRendererComponent>(entity);
        if (ECS::MeshRendererComponent* renderer = world.get<ECS::MeshRendererComponent>(entity)) {
            renderer->materialPath = path;
        }
        ++applied;
    }
    if (applied > 0) ctx.markDirty();
}

bool MaterialEditorPanel::createInAssetBrowser(EditorContext& ctx) {
    m_materialPath.clear();
    if (!saveToPath(newMaterialPath(ctx))) return false;
    assignToSelection(ctx, true);
    ctx.assetBrowserNavigateTo = resolvedPath(m_materialPath).parent_path();
    m_status = "Created " + m_materialPath.filename().string();
    return true;
}

void MaterialEditorPanel::applyToSelection(EditorContext& ctx) {
    if (!ensureFile(ctx)) {
        m_status = "Could not save material.";
        return;
    }
    if (!saveToPath(resolvedPath(m_materialPath))) {
        m_status = "Could not save material.";
        return;
    }
    assignToSelection(ctx, true);
    publishLive();
    int meshes = 0;
    if (ctx.activeWorld) {
        for (ECS::Entity entity : ctx.selectedEntities) {
            if (entity.isValid() && ctx.activeWorld->has<ECS::MeshFilterComponent>(entity)) ++meshes;
        }
    }
    if (meshes == 0) m_status = "Saved " + m_materialPath.filename().string() + ". Select a mesh to apply it.";
    else m_status = "Applied " + m_materialPath.filename().string();
}

bool MaterialEditorPanel::openFromPath(const std::filesystem::path& path) {
    const std::filesystem::path resolved = resolvedPath(path);
    Assets::MaterialSurface loaded;
    if (!Assets::loadMaterialFile(resolved, loaded)) {
        m_status = "Could not read material.";
        return false;
    }
    m_surface = std::move(loaded);
    m_materialPath = resolved;
    m_status.clear();
    m_dirty = false;
    publishLive();
    return true;
}

void MaterialEditorPanel::requestOpenMaterial(EditorContext& ctx) {
    m_pickedMaterialPath.clear();
    m_awaitingMaterialPick = true;
    ctx.browse.requestProjectAsset(&m_pickedMaterialPath, ".mat;.material", "Open Material");
}

void MaterialEditorPanel::pollOpenedMaterial(EditorContext& ctx) {
    if (!m_awaitingMaterialPick) return;
    if (ctx.browse.kind != EditorContext::BrowseSession::Kind::None) return;
    m_awaitingMaterialPick = false;
    if (m_pickedMaterialPath.empty()) return;
    const std::string picked = std::move(m_pickedMaterialPath);
    m_pickedMaterialPath.clear();
    if (!openFromPath(picked)) m_status = "Could not read material.";
}

void MaterialEditorPanel::renderProperties(
    Caffeine::UIKit::Elements::Container& parent, EditorContext& ctx, bool& changed) {
    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;

    auto& header = parent.emplace<Card>("mat_file");
    auto& fileRow = header.emplace<Row>("mat_file_row");
    fileRow.emplace<Button>("Browse Assets").setOnClick([this, &ctx] {
        requestOpenMaterial(ctx);
    });
    auto& pathLabel = fileRow.emplace<Label>(
        m_materialPath.empty() ? "No material file" : m_materialPath.generic_string(),
        TextTone::Muted);
    pathLabel.setGrow(true);
    pathLabel.setTooltip(m_materialPath.generic_string());

    auto& meta = header.emplace<PropertyGrid>("mat_meta");
    meta.typedRow<TextFieldProperty>("Name", m_surface.name)
        .setOnChange([&changed](const std::string&) { changed = true; });

    std::vector<std::string> presets;
    presets.emplace_back("Apply a preset...");
    for (const Assets::MaterialPreset& preset : Assets::materialPresets()) {
        presets.emplace_back(preset.name);
    }
    if (m_presetIndex < 0 || m_presetIndex >= static_cast<int>(presets.size())) {
        m_presetIndex = 0;
    }
    meta.typedRow<ChoiceProperty>("Preset", m_presetIndex, presets)
        .setOnChange([this, &changed](int value) {
            const auto& list = Assets::materialPresets();
            if (value > 0 && value <= static_cast<int>(list.size())) {
                Assets::applyMaterialPreset(list[static_cast<size_t>(value - 1)].surface,
                                            m_surface);
                changed = true;
            }
            m_presetIndex = 0;
        });

    auto floatRow = [&changed](PropertyGrid& grid, const char* label, float& value, float lo,
                               float hi, const char* format) {
        grid.typedRow<FloatProperty>(label, value, lo, hi, 0.01f, format, true)
            .setOnChange([&changed](float) { changed = true; });
    };

    auto& surface = parent.emplace<Elements::Section>("Surface");
    auto& surfaceGrid = surface.emplace<PropertyGrid>("mat_surface_grid");
    surfaceGrid.typedRow<ColorFieldProperty>("Albedo", &m_surface.albedo.x)
        .setOnChange([&changed] { changed = true; });
    floatRow(surfaceGrid, "Metallic", m_surface.metallic, 0.0f, 1.0f, "%.2f");
    floatRow(surfaceGrid, "Roughness", m_surface.roughness, 0.0f, 1.0f, "%.3f");
    floatRow(surfaceGrid, "Reflectance", m_surface.reflectance, 0.0f, 1.0f, "%.2f");
    surface.emplace<Label>("Metallic 1 + roughness 0 is a perfect mirror.",
                           TextTone::Muted)
        .setWrap(true);

    auto& maps = parent.emplace<Elements::Section>("Maps & UV");
    auto& mapsGrid = maps.emplace<PropertyGrid>("mat_maps_grid");
    auto addPath = [this, &ctx, &changed](PropertyGrid& grid, const char* label,
                                          std::string& target, const char* filter) {
        auto& row = grid.typedRow<PathProperty>(label, target);
        row.setOnBrowse([&ctx, &target, label, filter] {
            ctx.browse.requestProjectAsset(&target, filter, label);
        });
        row.setOnDrop([this, &ctx, &target, filter, &changed](const char* path) {
            const std::filesystem::path dropped(path);
            if (!Widgets::extensionMatchesFilter(dropped, filter)) return;
            target = Widgets::pathRelativeToProject(ctx, dropped);
            changed = true;
        });
        row.setOnChange([&changed] { changed = true; });
    };
    addPath(mapsGrid, "Albedo Map", m_surface.albedoMap, ".png;.jpg;.jpeg");
    addPath(mapsGrid, "Normal Map", m_surface.normalMap, ".png;.jpg;.jpeg");
    floatRow(mapsGrid, "Normal Strength", m_surface.normalStrength, 0.0f, 4.0f, "%.2f");
    addPath(mapsGrid, "ORM Map", m_surface.ormMap, ".png;.jpg;.jpeg");
    floatRow(mapsGrid, "AO Strength", m_surface.aoStrength, 0.0f, 1.0f, "%.2f");
    mapsGrid.typedRow<Vec2Property>("UV Tiling", m_surface.uvTiling)
        .setOnChange([&changed] { changed = true; });
    mapsGrid.typedRow<Vec2Property>("UV Offset", m_surface.uvOffset)
        .setOnChange([&changed] { changed = true; });
    maps.emplace<Label>("ORM: occlusion (R), roughness (G), metallic (B).",
                        TextTone::Muted)
        .setWrap(true);

    auto& alpha = parent.emplace<Elements::Section>("Transparency", false);
    auto& alphaGrid = alpha.emplace<PropertyGrid>("mat_alpha_grid");
    m_alphaModeIndex = static_cast<int>(m_surface.alphaMode);
    alphaGrid.typedRow<ChoiceProperty>(
        "Alpha Mode", m_alphaModeIndex,
        std::vector<std::string>{"Opaque", "Cutout", "Blend"})
        .setOnChange([this, &changed](int value) {
            m_surface.alphaMode = static_cast<Assets::MaterialAlphaMode>(value);
            changed = true;
        });
    if (m_surface.alphaMode == Assets::MaterialAlphaMode::Cutout) {
        floatRow(alphaGrid, "Alpha Cutoff", m_surface.alphaCutoff, 0.0f, 1.0f, "%.2f");
    }
    floatRow(alphaGrid, "Transmission", m_surface.transmission, 0.0f, 1.0f, "%.2f");
    floatRow(alphaGrid, "IOR", m_surface.ior, 1.0f, 2.5f, "%.3f");
    alpha.emplace<Label>("Water 1.33, ice 1.31, glass 1.5, diamond 2.42.",
                         TextTone::Muted)
        .setWrap(true);

    auto& coat = parent.emplace<Elements::Section>("Clear Coat", false);
    auto& coatGrid = coat.emplace<PropertyGrid>("mat_coat_grid");
    floatRow(coatGrid, "Clear Coat", m_surface.clearcoat, 0.0f, 1.0f, "%.2f");
    floatRow(coatGrid, "Coat Roughness", m_surface.clearcoatRoughness, 0.0f, 1.0f, "%.3f");

    auto& sheen = parent.emplace<Elements::Section>("Sheen", false);
    auto& sheenGrid = sheen.emplace<PropertyGrid>("mat_sheen_grid");
    sheenGrid.typedRow<Color3FieldProperty>("Sheen Color", &m_surface.sheenColor.x)
        .setOnChange([&changed] { changed = true; });
    floatRow(sheenGrid, "Sheen Roughness", m_surface.sheenRoughness, 0.0f, 1.0f, "%.2f");

    auto& reflect = parent.emplace<Elements::Section>("Reflection", false);
    auto& reflectGrid = reflect.emplace<PropertyGrid>("mat_reflect_grid");
    floatRow(reflectGrid, "Reflection", m_surface.reflection, 0.0f, 1.0f, "%.2f");
    reflect.emplace<Label>(
        "Both modules are optional. Together, quality raises the march and "
        "performance still caps it.",
        TextTone::Muted)
        .setWrap(true);

    m_reflectionPerformanceEdit = m_surface.reflectionPerformance != 0;
    reflectGrid.typedRow<BoolProperty>("Performance", m_reflectionPerformanceEdit)
        .setOnChange([this, &changed](bool value) {
            m_surface.reflectionPerformance = value ? 1 : 0;
            changed = true;
        });
    if (m_reflectionPerformanceEdit) {
        floatRow(reflectGrid, "SSR Resolution", m_surface.ssrResolution, 0.25f, 1.0f, "%.2f");
        floatRow(reflectGrid, "Max Ray Steps", m_surface.ssrMaxSteps, 4.0f, 64.0f, "%.0f");
        floatRow(reflectGrid, "Temporal Reuse", m_surface.ssrTemporalFrames, 1.0f, 8.0f,
                 "%.0f frames");
        floatRow(reflectGrid, "Distance Fade", m_surface.ssrDistance, 1.0f, 80.0f, "%.0f m");
        reflect.emplace<Label>(
            "Half resolution, fewer steps on rough surfaces, and a dithered march.",
            TextTone::Muted)
            .setWrap(true);
    }

    m_reflectionQualityEdit = m_surface.reflectionQuality != 0;
    reflectGrid.typedRow<BoolProperty>("Quality", m_reflectionQualityEdit)
        .setOnChange([this, &changed](bool value) {
            m_surface.reflectionQuality = value ? 1 : 0;
            changed = true;
        });
    if (m_reflectionQualityEdit) {
        floatRow(reflectGrid, "SSR Samples", m_surface.ssrSamples, 4.0f, 64.0f, "%.0f");
        floatRow(reflectGrid, "Denoise Strength", m_surface.ssrDenoise, 0.0f, 1.0f, "%.2f");
        m_reflectionPlanarEdit = m_surface.reflectionPlanar != 0;
        reflectGrid.typedRow<BoolProperty>("Planar", m_reflectionPlanarEdit)
            .setOnChange([this, &changed](bool value) {
                m_surface.reflectionPlanar = value ? 1 : 0;
                changed = true;
            });
        floatRow(reflectGrid, "Probe Blend", m_surface.ssrProbeBlend, 1.0f, 8.0f, "%.0f");
        floatRow(reflectGrid, "Bounce Count", m_surface.ssrBounces, 0.0f, 2.0f, "%.0f");
        reflect.emplace<Label>(
            "Denoise uses the previous frame. Planar is only this surface. "
            "Probes stay cached.",
            TextTone::Muted)
            .setWrap(true);
    }

    auto& iridescence = parent.emplace<Elements::Section>("Iridescence", false);
    auto& iridescenceGrid = iridescence.emplace<PropertyGrid>("mat_iri_grid");
    floatRow(iridescenceGrid, "Iridescence", m_surface.iridescence, 0.0f, 1.0f, "%.2f");
    floatRow(iridescenceGrid, "Film Thickness", m_surface.iridescenceThickness, 100.0f,
             1200.0f, "%.0f nm");
    floatRow(iridescenceGrid, "Film IOR", m_surface.iridescenceIor, 1.0f, 2.5f, "%.2f");

    auto& emission = parent.emplace<Elements::Section>("Emission", false);
    auto& emissionGrid = emission.emplace<PropertyGrid>("mat_emit_grid");
    emissionGrid.typedRow<Color3FieldProperty>("Emission", &m_surface.emission.x)
        .setOnChange([&changed] { changed = true; });
    floatRow(emissionGrid, "Emission Strength", m_surface.emissionStrength, 0.0f, 50.0f,
             "%.2f");
    addPath(emissionGrid, "Emission Map", m_surface.emissionMap, ".png;.jpg;.jpeg");

    if (!m_materialPath.empty()) {
        parent.emplace<Label>(m_materialPath.filename().string(), TextTone::Muted);
    }
    if (!m_status.empty()) {
        parent.emplace<Label>(m_status, TextTone::Muted).setWrap(true);
    }
}

void MaterialEditorPanel::renderPreview(Caffeine::UIKit::Elements::Container& parent,
                                        EditorContext& ctx) {
    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;

    auto& card = parent.emplace<Card>("mat_preview_card");
    card.emplace<Label>("Preview", TextTone::Accent).setHeading(true);

    auto& canvas = card.emplace<Canvas>("mat_preview_canvas");
    canvas.setHeight(340.0f);
    canvas.setOnDraw([this, &ctx](UIContext&, ImDrawList* drawList, const ImVec2& origin,
                                  const ImVec2& size) {
        const float side = std::max(1.0f, std::min(size.x, size.y));
        const ImVec2 p0(origin.x + (size.x - side) * 0.5f,
                        origin.y + (size.y - side) * 0.5f);
        const ImVec2 p1(p0.x + side, p0.y + side);

        bool drewImage = false;
#ifdef CF_HAS_SDL3
        const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
        const float dpi = std::max(1.0f, std::max(scale.x, scale.y));
        u32 pixels = std::clamp(static_cast<u32>(std::ceil(side * dpi)), 128u, 768u);
        pixels = (pixels + 3u) & ~3u;

        Render::MaterialPreviewSettings settings;
        settings.yawDegrees = m_previewRotation;
        settings.pitchDegrees = m_previewPitch;
        settings.showFloor = m_previewFloor;
        settings.environmentPath.clear();
        if (ctx.activeWorld) {
            const Scene::ActiveSkybox sky = Scene::findActiveSkybox(*ctx.activeWorld);
            if (sky.component) {
                settings.environmentPath =
                    Scene::resolveSkyboxTexturePath(*sky.component, m_projectRoot).string();
                settings.environmentExposure = sky.component->exposure;
            }
        }

        if (m_frameCmd && m_previewRenderer.isReady()) {
            m_previewRenderer.render(m_frameCmd, m_surface, pixels, settings, m_projectRoot);
        }
        if (RHI::Texture* texture = m_previewRenderer.colorTexture();
            texture && texture->handle) {
            drawList->AddImage(
                ImTextureRef(reinterpret_cast<ImTextureID>(texture->handle)), p0, p1);
            drewImage = true;
        }
#else
        (void)ctx;
#endif
        if (!drewImage) {
            drawList->AddText(ImVec2(origin.x + 8.0f, origin.y + 8.0f),
                              ImGui::GetColorU32(ImGuiCol_TextDisabled),
                              "GPU preview unavailable");
        }
    });

    auto& grid = card.emplace<PropertyGrid>("mat_preview_grid");
    grid.typedRow<FloatProperty>("Orbit", m_previewRotation, 0.0f, 360.0f, 1.0f,
                                 "%.0f deg", true);
    grid.typedRow<FloatProperty>("Height", m_previewPitch, -30.0f, 70.0f, 1.0f,
                                 "%.0f deg", true);
    grid.typedRow<BoolProperty>("Floor", m_previewFloor);
}

void MaterialEditorPanel::onImGuiRender(EditorContext& ctx) {
    if (!m_open) return;
    pollOpenedMaterial(ctx);

    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;

    Panel root("Material Editor", "PBR surface authoring", "align-left", &m_open);
    root.setWindowFlags(ImGuiWindowFlags_MenuBar);
    root.setOnBegin([this, &ctx] {
        m_wasFocusedLastFrame =
            ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteFocused)) {
            handleSaveShortcut();
        }
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File##MaterialEditor")) {
                if (ImGui::MenuItem("Open...", nullptr, false, !m_awaitingMaterialPick)) {
                    requestOpenMaterial(ctx);
                }
                if (ImGui::MenuItem("New")) {
                    m_surface = Assets::MaterialSurface{};
                    m_surface.valid = true;
                    m_surface.name = "Material";
                    m_materialPath.clear();
                    m_status.clear();
                }
                if (ImGui::MenuItem("Create in Asset Browser")) createInAssetBrowser(ctx);
                if (ImGui::MenuItem("Save", "Ctrl+S")) handleSaveShortcut();
                if (ImGui::MenuItem("Save As...")) m_pendingSaveAs = true;
                if (ImGui::MenuItem("Apply to Selected")) applyToSelection(ctx);
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }
    });
    UIContext ui;

    bool changed = false;
    auto& columns = root.emplace<Row>("mat_columns");
    auto& properties = columns.emplace<ScrollArea>("mat_properties_scroll");
    properties.setGrow(true);
    renderProperties(properties, ctx, changed);
    auto& preview = columns.emplace<ScrollArea>("mat_preview_scroll");
    preview.setGrow(true);
    renderPreview(preview, ctx);

    const std::string albedoMapBefore = m_surface.albedoMap;
    const std::string normalMapBefore = m_surface.normalMap;
    const std::string ormMapBefore = m_surface.ormMap;
    const std::string emissionMapBefore = m_surface.emissionMap;

    ui.draw(root);

    if (m_surface.albedoMap != albedoMapBefore ||
        m_surface.normalMap != normalMapBefore || m_surface.ormMap != ormMapBefore ||
        m_surface.emissionMap != emissionMapBefore) {
        changed = true;
    }

    if (changed) {
        Assets::sanitizeMaterialSurface(m_surface);
        m_dirty = true;
        if (m_materialPath.empty()) {
            const std::string adopted = materialOnSelection(ctx);
            if (!adopted.empty()) {
                m_materialPath = adopted;
                m_status = "Editing " +
                           std::filesystem::path(adopted).filename().string();
            } else if (saveToPath(newMaterialPath(ctx))) {
                assignToSelection(ctx, true);
                m_status = "Saved " + m_materialPath.generic_string();
            } else {
                m_status = "Could not save material.";
            }
        }
        publishLive();
    }

    if (m_pendingSaveAs) {
        const std::filesystem::path start =
            m_materialPath.empty()
                ? (m_projectRoot.empty() ? std::filesystem::path(".")
                                         : std::filesystem::path(m_projectRoot) / "materials")
                : m_materialPath.parent_path();
        if (auto selected =
                FilePicker::pickPath(FilePicker::Mode::SaveFile, "Save Material", start)) {
            if (saveToPath(*selected)) {
                ctx.assetBrowserNavigateTo = selected->parent_path();
                applyToSelection(ctx);
            }
            m_pendingSaveAs = false;
        } else if (FilePicker::consumeCloseEvent("Save Material")) {
            m_pendingSaveAs = false;
        }
    }

    if (m_pendingQuickSave) {
        m_pendingQuickSave = false;
        if (!ensureFile(ctx) || !saveToPath(resolvedPath(m_materialPath))) {
            m_status = "Save failed.";
        } else {
            assignToSelection(ctx, true);
            publishLive();
            m_status = "Saved " + m_materialPath.generic_string();
            ctx.assetBrowserNavigateTo = resolvedPath(m_materialPath).parent_path();
        }
    }

    if (m_dirty && !m_materialPath.empty() &&
        !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        saveToPath(resolvedPath(m_materialPath));
    }

    if (m_refreshAssets) {
        ctx.assetBrowserDirty = true;
        m_refreshAssets = false;
    }
}

} // namespace Caffeine::Editor
