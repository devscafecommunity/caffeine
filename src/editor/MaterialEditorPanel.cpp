#include "editor/MaterialEditorPanel.hpp"

#include "assets/MaterialCache.hpp"
#include "assets/MaterialFile.hpp"
#include "assets/MaterialPresets.hpp"
#include "editor/InspectorWidgets.hpp"
#include "scene/EnvironmentSystem.hpp"

#include <imgui.h>

#include <algorithm>
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

bool MaterialEditorPanel::saveCurrent() {
    if (m_materialPath.empty()) {
        m_status = "Create the .mat from the asset browser, then open it.";
        return false;
    }
    const std::filesystem::path path = resolvedPath(m_materialPath);
    if (!Assets::saveMaterialFile(path, m_surface)) {
        m_status = "Save failed.";
        return false;
    }
    m_materialPath = path;
    publishLive();
    m_status.clear();
    return true;
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
    publishLive();
    return true;
}

void MaterialEditorPanel::renderPresetPicker() {
    Widgets::setWidthForLabel("Preset");
    if (!ImGui::BeginCombo("Preset", "Apply a preset...")) return;
    std::string_view category;
    for (const Assets::MaterialPreset& preset : Assets::materialPresets()) {
        if (preset.category != category) {
            category = preset.category;
            ImGui::SeparatorText(std::string(category).c_str());
        }
        if (ImGui::Selectable(std::string(preset.name).c_str())) {
            Assets::applyMaterialPreset(preset.surface, m_surface);
            publishLive();
        }
    }
    ImGui::EndCombo();
}

void MaterialEditorPanel::renderProperties(EditorContext& ctx) {
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
    Widgets::setWidthForLabel("Name");
    if (ImGui::InputText("Name", name, sizeof(name))) {
        m_surface.name = name;
        changed = true;
    }
    renderPresetPicker();

    if (ImGui::CollapsingHeader("Surface", ImGuiTreeNodeFlags_DefaultOpen)) {
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

    if (ImGui::CollapsingHeader("Maps & UV", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (Widgets::AssetField(ctx, "Albedo Map", m_surface.albedoMap, ".png;.jpg;.jpeg")) changed = true;
        if (Widgets::AssetField(ctx, "Normal Map", m_surface.normalMap, ".png;.jpg;.jpeg")) changed = true;
        slider("Normal Strength", m_surface.normalStrength, 0.0f, 4.0f);
        if (Widgets::AssetField(ctx, "ORM Map", m_surface.ormMap, ".png;.jpg;.jpeg")) changed = true;
        ImGui::TextDisabled("ORM: occlusion (R), roughness (G), metallic (B).");
        slider("AO Strength", m_surface.aoStrength, 0.0f, 1.0f);
        if (Widgets::DragVec2("UV Tiling", m_surface.uvTiling, 0.01f)) changed = true;
        if (Widgets::DragVec2("UV Offset", m_surface.uvOffset, 0.01f)) changed = true;
    }

    if (ImGui::CollapsingHeader("Transparency")) {
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

    if (ImGui::CollapsingHeader("Clear Coat")) {
        slider("Clear Coat", m_surface.clearcoat, 0.0f, 1.0f);
        slider("Coat Roughness", m_surface.clearcoatRoughness, 0.0f, 1.0f, "%.3f");
    }

    if (ImGui::CollapsingHeader("Sheen")) {
        color3("Sheen Color", m_surface.sheenColor);
        slider("Sheen Roughness", m_surface.sheenRoughness, 0.0f, 1.0f);
    }

    if (ImGui::CollapsingHeader("Iridescence")) {
        slider("Iridescence", m_surface.iridescence, 0.0f, 1.0f);
        slider("Film Thickness", m_surface.iridescenceThickness, 100.0f, 1200.0f, "%.0f nm");
        slider("Film IOR", m_surface.iridescenceIor, 1.0f, 2.5f, "%.2f");
    }

    if (ImGui::CollapsingHeader("Emission")) {
        color3("Emission", m_surface.emission);
        slider("Emission Strength", m_surface.emissionStrength, 0.0f, 50.0f);
        if (Widgets::AssetField(ctx, "Emission Map", m_surface.emissionMap, ".png;.jpg;.jpeg")) changed = true;
    }

    if (changed) {
        Assets::sanitizeMaterialSurface(m_surface);
        publishLive();
    }

    if (!m_materialPath.empty()) {
        ImGui::TextDisabled("%s", m_materialPath.filename().string().c_str());
    }
    if (!m_status.empty()) {
        ImGui::TextWrapped("%s", m_status.c_str());
    }
}

void MaterialEditorPanel::renderPreview(const EditorContext& ctx, float width, float height) {
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
    settings.environmentPath = Scene::resolveBuiltinSkyboxPath(ctx.skyboxIndex).string();
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
    ImGui::SliderFloat("Orbit", &m_previewRotation, 0.0f, 360.0f, "%.0f deg");
    Widgets::setWidthForLabel("Height");
    ImGui::SliderFloat("Height", &m_previewPitch, -30.0f, 70.0f, "%.0f deg");
    ImGui::Checkbox("Floor", &m_previewFloor);
}

void MaterialEditorPanel::onImGuiRender(EditorContext& ctx) {
    if (!m_open) return;

    ImGui::Begin("Material Editor", &m_open, ImGuiWindowFlags_MenuBar);
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New")) {
                m_surface = Assets::MaterialSurface{};
                m_surface.valid = true;
                m_surface.name = "Material";
                m_materialPath.clear();
                m_status.clear();
            }
            if (ImGui::MenuItem("Save", "Ctrl+S")) saveCurrent();
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
    ImGui::End();
}

} // namespace Caffeine::Editor
