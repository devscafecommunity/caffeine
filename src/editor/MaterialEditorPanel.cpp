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

void MaterialEditorPanel::renderPresetPicker() {
    Widgets::setWidthForLabel("Preset");
    if (!ImGui::BeginCombo("##mat_preset", "Apply a preset...")) return;
    std::string_view category;
    int index = 0;
    for (const Assets::MaterialPreset& preset : Assets::materialPresets()) {
        if (preset.category != category) {
            category = preset.category;
            ImGui::SeparatorText(std::string(category).c_str());
        }
        ImGui::PushID(index++);
        const std::string label(preset.name);
        if (ImGui::Selectable(label.c_str())) {
            Assets::applyMaterialPreset(preset.surface, m_surface);
            publishLive();
        }
        ImGui::PopID();
    }
    ImGui::EndCombo();
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

void MaterialEditorPanel::renderProperties(EditorContext& ctx) {
    ImGui::PushID("MatProps");
    if (ImGui::Button("Browse Assets")) requestOpenMaterial(ctx);
    ImGui::SameLine();
    if (m_materialPath.empty()) ImGui::TextDisabled("No material file");
    else ImGui::TextDisabled("%s", m_materialPath.generic_string().c_str());
    bool changed = false;
    auto slider = [&](const char* label, float& value, float lo, float hi, const char* fmt = "%.2f") {
        Widgets::setWidthForLabel(label);
        if (ImGui::SliderFloat(label, &value, lo, hi, fmt)) changed = true;
    };
    auto color3 = [&](const char* label, Vec3& value) {
        float rgb[3] = {value.x, value.y, value.z};
        Widgets::setWidthForLabel(label);
        if (ImGui::ColorEdit3(label, rgb, ImGuiColorEditFlags_Float)) {
            value = Vec3(rgb[0], rgb[1], rgb[2]);
            changed = true;
        }
    };

    char name[128] = {};
    const std::string& currentName = m_surface.name;
    const size_t copy = currentName.size() < sizeof(name) - 1 ? currentName.size() : sizeof(name) - 1;
    currentName.copy(name, copy);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Name");
    ImGui::SameLine();
    Widgets::setWidthForLabel("Name");
    if (ImGui::InputText("##mat_name", name, sizeof(name))) {
        m_surface.name = name;
        changed = true;
    }
    renderPresetPicker();

    if (ImGui::CollapsingHeader("Surface##mat_surface", ImGuiTreeNodeFlags_DefaultOpen)) {
        float albedo[4] = {m_surface.albedo.x, m_surface.albedo.y, m_surface.albedo.z, m_surface.albedo.w};
        Widgets::setWidthForLabel("Albedo");
        if (ImGui::ColorEdit4("Albedo", albedo)) {
            m_surface.albedo = Vec4(albedo[0], albedo[1], albedo[2], albedo[3]);
            changed = true;
        }
        slider("Metallic", m_surface.metallic, 0.0f, 1.0f);
        slider("Roughness", m_surface.roughness, 0.0f, 1.0f, "%.3f");
        slider("Reflectance", m_surface.reflectance, 0.0f, 1.0f);
        ImGui::TextDisabled("Metallic 1 + roughness 0 is a perfect mirror.");
    }

    if (ImGui::CollapsingHeader("Maps & UV##mat_maps", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (Widgets::AssetField(ctx, "Albedo Map", m_surface.albedoMap, ".png;.jpg;.jpeg")) changed = true;
        if (Widgets::AssetField(ctx, "Normal Map", m_surface.normalMap, ".png;.jpg;.jpeg")) changed = true;
        slider("Normal Strength", m_surface.normalStrength, 0.0f, 4.0f);
        if (Widgets::AssetField(ctx, "ORM Map", m_surface.ormMap, ".png;.jpg;.jpeg")) changed = true;
        ImGui::TextDisabled("ORM: occlusion (R), roughness (G), metallic (B).");
        slider("AO Strength", m_surface.aoStrength, 0.0f, 1.0f);
        if (Widgets::DragVec2("UV Tiling", m_surface.uvTiling, 0.01f)) changed = true;
        if (Widgets::DragVec2("UV Offset", m_surface.uvOffset, 0.01f)) changed = true;
    }

    if (ImGui::CollapsingHeader("Transparency##mat_alpha")) {
        int mode = static_cast<int>(m_surface.alphaMode);
        const char* modes[] = {"Opaque", "Cutout", "Blend"};
        Widgets::setWidthForLabel("Alpha Mode");
        if (ImGui::Combo("Alpha Mode", &mode, modes, 3)) {
            m_surface.alphaMode = static_cast<Assets::MaterialAlphaMode>(mode);
            changed = true;
        }
        if (m_surface.alphaMode == Assets::MaterialAlphaMode::Cutout) {
            slider("Alpha Cutoff", m_surface.alphaCutoff, 0.0f, 1.0f);
        }
        ImGui::TextDisabled("Opacity is the albedo alpha (and the albedo map alpha).");
        slider("Transmission", m_surface.transmission, 0.0f, 1.0f);
        slider("IOR", m_surface.ior, 1.0f, 2.5f, "%.3f");
        ImGui::TextDisabled("Water 1.33, ice 1.31, glass 1.5, diamond 2.42.");
    }

    if (ImGui::CollapsingHeader("Clear Coat##mat_coat")) {
        slider("Clear Coat", m_surface.clearcoat, 0.0f, 1.0f);
        slider("Coat Roughness", m_surface.clearcoatRoughness, 0.0f, 1.0f, "%.3f");
    }

    if (ImGui::CollapsingHeader("Sheen##mat_sheen")) {
        color3("Sheen Color", m_surface.sheenColor);
        slider("Sheen Roughness", m_surface.sheenRoughness, 0.0f, 1.0f);
    }

    if (ImGui::CollapsingHeader("Reflection##mat_reflect")) {
        slider("Reflection", m_surface.reflection, 0.0f, 1.0f);
        ImGui::TextDisabled("Both modules are optional. Together, quality raises the march and performance still caps it.");

        bool performance = m_surface.reflectionPerformance != 0;
        if (ImGui::Checkbox("Performance##mat_ssr_perf", &performance)) {
            m_surface.reflectionPerformance = performance ? 1 : 0;
            changed = true;
        }
        if (performance) {
            slider("SSR Resolution", m_surface.ssrResolution, 0.25f, 1.0f, "%.2f");
            slider("Max Ray Steps", m_surface.ssrMaxSteps, 4.0f, 64.0f, "%.0f");
            slider("Temporal Reuse", m_surface.ssrTemporalFrames, 1.0f, 8.0f, "%.0f frames");
            slider("Distance Fade", m_surface.ssrDistance, 1.0f, 80.0f, "%.0f m");
            ImGui::TextDisabled("Half resolution, fewer steps on rough surfaces, and a dithered march.");
        }

        bool quality = m_surface.reflectionQuality != 0;
        if (ImGui::Checkbox("Quality##mat_ssr_quality", &quality)) {
            m_surface.reflectionQuality = quality ? 1 : 0;
            changed = true;
        }
        if (quality) {
            slider("SSR Samples", m_surface.ssrSamples, 4.0f, 64.0f, "%.0f");
            slider("Denoise Strength", m_surface.ssrDenoise, 0.0f, 1.0f);
            bool planar = m_surface.reflectionPlanar != 0;
            if (ImGui::Checkbox("Planar##mat_ssr_planar", &planar)) {
                m_surface.reflectionPlanar = planar ? 1 : 0;
                changed = true;
            }
            slider("Probe Blend", m_surface.ssrProbeBlend, 1.0f, 8.0f, "%.0f");
            slider("Bounce Count", m_surface.ssrBounces, 0.0f, 2.0f, "%.0f");
            ImGui::TextDisabled("Denoise uses the previous frame. Planar is only this surface. Probes stay cached.");
        }
    }

    if (ImGui::CollapsingHeader("Iridescence##mat_iri")) {
        slider("Iridescence", m_surface.iridescence, 0.0f, 1.0f);
        slider("Film Thickness", m_surface.iridescenceThickness, 100.0f, 1200.0f, "%.0f nm");
        slider("Film IOR", m_surface.iridescenceIor, 1.0f, 2.5f, "%.2f");
    }

    if (ImGui::CollapsingHeader("Emission##mat_emit")) {
        color3("Emission", m_surface.emission);
        slider("Emission Strength", m_surface.emissionStrength, 0.0f, 50.0f);
        if (Widgets::AssetField(ctx, "Emission Map", m_surface.emissionMap, ".png;.jpg;.jpeg")) changed = true;
    }

    if (changed) {
        Assets::sanitizeMaterialSurface(m_surface);
        m_dirty = true;
        if (m_materialPath.empty()) {
            const std::string adopted = materialOnSelection(ctx);
            if (!adopted.empty()) {
                m_materialPath = adopted;
                m_status = "Editing " + std::filesystem::path(adopted).filename().string();
            } else if (saveToPath(newMaterialPath(ctx))) {
                assignToSelection(ctx, true);
                m_status = "Saved " + m_materialPath.generic_string();
            } else {
                m_status = "Could not save material.";
            }
        }
        publishLive();
    }

    if (!m_materialPath.empty()) {
        ImGui::TextDisabled("%s", m_materialPath.filename().string().c_str());
    }
    if (!m_status.empty()) {
        ImGui::TextWrapped("%s", m_status.c_str());
    }
    ImGui::PopID();
}

void MaterialEditorPanel::renderPreview(const EditorContext& ctx, float width, float height) {
    ImGui::PushID("MatPreview");
    const float side = std::max(1.0f, std::min(width, height));
    const ImVec2 start = ImGui::GetCursorPos();
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
    if (RHI::Texture* texture = m_previewRenderer.colorTexture(); texture && texture->handle) {
        ImGui::SetCursorPos(ImVec2(start.x + (width - side) * 0.5f, start.y + (height - side) * 0.5f));
        ImGui::Image(reinterpret_cast<ImTextureID>(texture->handle), ImVec2(side, side));
        drewImage = true;
    }
#else
    (void)ctx;
#endif
    ImGui::SetCursorPos(start);
    ImGui::Dummy(ImVec2(width, std::max(height, 1.0f)));
    if (!drewImage) {
        ImGui::SetCursorPos(ImVec2(start.x + 8.0f, start.y + 8.0f));
        ImGui::TextDisabled("GPU preview unavailable");
        ImGui::SetCursorPos(ImVec2(start.x, start.y + height));
    }

    Widgets::setWidthForLabel("Orbit");
    ImGui::SliderFloat("##preview_orbit", &m_previewRotation, 0.0f, 360.0f, "Orbit %.0f deg");
    Widgets::setWidthForLabel("Height");
    ImGui::SliderFloat("##preview_pitch", &m_previewPitch, -30.0f, 70.0f, "Height %.0f deg");
    ImGui::Checkbox("Floor", &m_previewFloor);
    ImGui::PopID();
}

void MaterialEditorPanel::onImGuiRender(EditorContext& ctx) {
    if (!m_open) return;
    pollOpenedMaterial(ctx);

    ImGui::Begin("Material Editor", &m_open, ImGuiWindowFlags_MenuBar);
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

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float leftW = avail.x * 0.58f;
    if (ImGui::BeginChild("MaterialProperties", ImVec2(leftW, avail.y), true)) {
        renderProperties(ctx);
    }
    ImGui::EndChild();
    ImGui::SameLine();
    if (ImGui::BeginChild("MaterialPreview", ImVec2(avail.x - leftW - spacing, avail.y), true)) {
        const ImVec2 previewAvail = ImGui::GetContentRegionAvail();
        const float controlsH = ImGui::GetFrameHeightWithSpacing() * 3.0f;
        renderPreview(ctx, previewAvail.x, previewAvail.y - controlsH);
    }
    ImGui::EndChild();

    m_wasFocusedLastFrame =
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

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

    if (m_dirty && !m_materialPath.empty() && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        saveToPath(resolvedPath(m_materialPath));
    }

    if (m_refreshAssets) {
        ctx.assetBrowserDirty = true;
        m_refreshAssets = false;
    }

    ImGui::End();
}

} // namespace Caffeine::Editor
