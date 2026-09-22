#include "editor/SettingsPanel.hpp"
#include "editor/EditorIcons.hpp"
#include "editor/EditorTheme.hpp"

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#ifdef CF_HAS_SDL3
#include <imgui_impl_sdlgpu3.h>
#endif
#endif

namespace Caffeine::Editor {

SettingsPanel::SettingsPanel() {
    m_layoutManager.loadProfiles();
    loadPreferences();

    if (m_layoutManager.getProfile(m_preferences.activeLayoutProfile)) {
        m_layoutManager.applyProfile(m_preferences.activeLayoutProfile);
        m_selectedProfileName = m_preferences.activeLayoutProfile;
    } else {
        m_layoutManager.applyProfile("Default");
        m_selectedProfileName = "Default";
        m_preferences.activeLayoutProfile = "Default";
    }

    for (size_t i = 0; i < m_layoutManager.profiles().size(); ++i) {
        if (m_layoutManager.profiles()[i].name == m_selectedProfileName) {
            m_selectedProfileIndex = static_cast<int>(i);
            break;
        }
    }
}

void SettingsPanel::loadPreferences() {
    m_preferences = EditorPreferences::load();
    syncUIFromPreferences();
}

void SettingsPanel::syncUIFromPreferences() {
    m_vsyncEnabled = m_preferences.vsyncEnabled;
    m_fontSize = m_preferences.fontSize;
    m_darkMode = m_preferences.darkMode;
    m_autoSaveEnabled = m_preferences.autoSaveEnabled;
    m_autoSaveInterval = m_preferences.autoSaveInterval;
}

void SettingsPanel::syncPreferencesFromUI() {
    m_preferences.vsyncEnabled = m_vsyncEnabled;
    m_preferences.fontSize = m_fontSize;
    m_preferences.darkMode = m_darkMode;
    m_preferences.autoSaveEnabled = m_autoSaveEnabled;
    m_preferences.autoSaveInterval = m_autoSaveInterval;
    m_preferences.activeLayoutProfile = m_layoutManager.currentProfile().name;
}

void SettingsPanel::savePreferences() {
    syncPreferencesFromUI();
    m_preferences.save();
}

void SettingsPanel::applyPreferencesToContext(EditorContext& ctx) {
    ctx.uniformScale = m_preferences.uniformScaleDefault;
    ctx.snapToGrid = m_preferences.snapEnabled;
    ctx.snapGridSize = m_preferences.snapIncrement;
    ctx.textureQualityEnabled = m_preferences.textureQualityEnabled;
    ctx.textureQualityRadius = m_preferences.textureQualityRadius;
    ctx.textureQualityFalloff = m_preferences.textureQualityFalloff;
    ctx.textureQualityMinScale = m_preferences.textureQualityMinScale;
}

void SettingsPanel::render() {
#ifdef CF_HAS_IMGUI
    if (!m_open) return;

    if (ImGui::Begin("Settings", &m_open)) {
        struct SettingsNavItem {
            const char* label;
            const char* icon;
        };
        static const SettingsNavItem navItems[] = {
            {"General", "account"},
            {"Editor", "arrows-diagonal"},
            {"Viewport", "arrows-vertical"},
            {"Panels", "arrow-down-square"},
            {"Layout Profiles", "backup-restore"},
        };

        if (ImGui::BeginChild("settings_nav", ImVec2(190.0f, -40.0f), true)) {
            ImGui::TextDisabled("Settings");
            ImGui::Separator();
            for (int i = 0; i < IM_ARRAYSIZE(navItems); ++i) {
                ImGui::PushID(i);
                const bool selected = (m_settingsSection == i);
                if (selected) {
                    ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
                }
                if (EditorIcons::hasIcon(navItems[i].icon)) {
                    EditorIcons::image(navItems[i].icon, ImGui::GetFontSize());
                    ImGui::SameLine();
                }
                if (ImGui::Selectable(navItems[i].label, selected, 0, ImVec2(-1, 0))) {
                    m_settingsSection = i;
                }
                if (selected) {
                    ImGui::PopStyleColor();
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
        }

        ImGui::SameLine();

        if (ImGui::BeginChild("settings_content", ImVec2(0.0f, -40.0f), true)) {
            switch (m_settingsSection) {
                case 0: renderGeneralSettings(); break;
                case 1: renderEditorSettings(); break;
                case 2: renderViewportSettings(); break;
                case 3: renderPanelSettings(); break;
                case 4: renderLayoutProfiles(); break;
                default: break;
            }
            ImGui::EndChild();
        }

        ImGui::Separator();
        if (ImGui::Button("Save Preferences", ImVec2(160, 0))) {
            savePreferences();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Stored in %s", EditorPreferences::preferencesPath().string().c_str());
    }
    ImGui::End();
#endif
}

void SettingsPanel::renderLayoutProfiles() {
#ifdef CF_HAS_IMGUI
    const auto& profiles = m_layoutManager.profiles();

    ImGui::TextColored(ImVec4(0.7f, 0.7f, 1.0f, 1.0f), "Available Profiles:");
    ImGui::Separator();

    if (ImGui::BeginListBox("##profiles", ImVec2(-1, 200))) {
        for (int i = 0; i < static_cast<int>(profiles.size()); ++i) {
            const bool isSelected = (m_selectedProfileIndex == i);
            if (ImGui::Selectable(profiles[i].name.c_str(), isSelected)) {
                m_selectedProfileIndex = i;
                m_selectedProfileName = profiles[i].name;
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndListBox();
    }

    ImGui::Spacing();
    if (ImGui::Button("Apply Profile", ImVec2(150, 0))) {
        applyLayoutProfile(m_selectedProfileName);
        savePreferences();
    }
    ImGui::SameLine();
    ImGui::Text("Current: %s", m_layoutManager.currentProfile().name.c_str());

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 1.0f, 1.0f), "Save Current Layout:");

    ImGui::InputText("Profile Name##new", m_newProfileName.data(), m_newProfileName.capacity(),
                     ImGuiInputTextFlags_CallbackResize,
                     [](ImGuiInputTextCallbackData* data) {
                         auto* str = static_cast<std::string*>(data->UserData);
                         if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
                             str->resize(data->BufSize);
                             data->Buf = str->data();
                         }
                         return 0;
                     },
                     &m_newProfileName);

    if (ImGui::Button("Save As New Profile", ImVec2(200, 0))) {
        if (!m_newProfileName.empty()) {
            LayoutProfile newProfile = m_layoutManager.currentProfile();
            newProfile.name = m_newProfileName;
            if (m_layoutManager.saveProfile(newProfile)) {
                m_selectedProfileName = m_newProfileName;
                savePreferences();
                m_newProfileName.clear();
            }
        }
    }

    if (!profiles.empty() && m_selectedProfileIndex < static_cast<int>(profiles.size())) {
        const auto& selectedProfile = profiles[m_selectedProfileIndex];
        const bool isBuiltIn = selectedProfile.name == "Default" || selectedProfile.name == "Vertical" ||
                               selectedProfile.name == "Horizontal" || selectedProfile.name == "Compact" ||
                               selectedProfile.name == "Fullscreen";
        ImGui::Spacing();
        if (isBuiltIn) {
            ImGui::TextDisabled("(Built-in profiles cannot be deleted)");
        } else if (ImGui::Button("Delete Selected Profile", ImVec2(200, 0))) {
            m_layoutManager.deleteProfile(selectedProfile.name);
            m_selectedProfileIndex = 0;
            if (!profiles.empty()) {
                m_selectedProfileName = profiles[0].name;
            }
            savePreferences();
        }
    }
#endif
}

void SettingsPanel::renderGeneralSettings() {
#ifdef CF_HAS_IMGUI
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 1.0f, 1.0f), "Appearance");
    ImGui::Separator();

    if (ImGui::Checkbox("Enable VSync", &m_vsyncEnabled)) {
        savePreferences();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Requires restart to take effect.");

    if (ImGui::SliderInt("UI Font Size", &m_fontSize, 10, 20)) {
        EditorTheme::setFontSize(static_cast<f32>(m_fontSize));
#ifdef CF_HAS_SDL3
        ImGui_ImplSDLGPU3_CreateDeviceObjects();
#endif
        savePreferences();
    }

    if (ImGui::Checkbox("Dark Mode", &m_darkMode)) {
        EditorThemeSettings theme;
        theme.darkMode = m_darkMode;
        theme.fontSize = static_cast<f32>(m_fontSize);
        EditorTheme::apply(theme);
#ifdef CF_HAS_SDL3
        ImGui_ImplSDLGPU3_CreateDeviceObjects();
#endif
        savePreferences();
    }

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 1.0f, 1.0f), "Project Workflow");
    ImGui::Separator();

    if (ImGui::Checkbox("Reopen last scene on startup", &m_preferences.reopenLastSceneOnStartup)) {
        savePreferences();
    }
    if (ImGui::Checkbox("Confirm before closing unsaved scenes", &m_preferences.confirmOnSceneClose)) {
        savePreferences();
    }
    if (ImGui::Checkbox("Show FPS in status bar", &m_preferences.showFPSInStatusBar)) {
        savePreferences();
    }

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 1.0f, 1.0f), "Auto-save");
    ImGui::Separator();

    if (ImGui::Checkbox("Enable Auto-save", &m_autoSaveEnabled)) {
        savePreferences();
    }
    if (ImGui::DragInt("Auto-save interval (seconds)", &m_autoSaveInterval, 1, 10, 3600)) {
        savePreferences();
    }
#endif
}

void SettingsPanel::renderEditorSettings() {
#ifdef CF_HAS_IMGUI
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 1.0f, 1.0f), "Transform Defaults");
    ImGui::Separator();
    if (ImGui::Checkbox("Uniform scale by default", &m_preferences.uniformScaleDefault)) {
        if (m_editorContext) {
            m_editorContext->uniformScale = m_preferences.uniformScaleDefault;
        }
        savePreferences();
    }

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 1.0f, 1.0f), "Snapping");
    ImGui::Separator();
    if (ImGui::Checkbox("Enable snapping", &m_preferences.snapEnabled)) {
        if (m_editorContext) m_editorContext->snapToGrid = m_preferences.snapEnabled;
        savePreferences();
    }
    if (ImGui::DragFloat("Snap increment", &m_preferences.snapIncrement, 0.01f, 0.001f, 10.0f, "%.3f")) {
        if (m_editorContext) m_editorContext->snapGridSize = m_preferences.snapIncrement;
        savePreferences();
    }
    if (ImGui::DragFloat("Default grid size", &m_preferences.defaultGridSize, 0.01f, 0.01f, 10.0f, "%.2f")) {
        savePreferences();
    }
#endif
}

void SettingsPanel::renderViewportSettings() {
#ifdef CF_HAS_IMGUI
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 1.0f, 1.0f), "Camera");
    ImGui::Separator();
    if (ImGui::DragFloat("Move speed", &m_preferences.cameraMoveSpeed, 0.1f, 0.1f, 50.0f, "%.1f")) {
        savePreferences();
    }
    if (ImGui::DragFloat("Orbit sensitivity", &m_preferences.cameraOrbitSpeed, 0.0005f, 0.0005f, 0.05f,
                         "%.4f")) {
        savePreferences();
    }

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 1.0f, 1.0f), "Grid");
    ImGui::Separator();
    if (ImGui::Checkbox("Show grid in viewport", &m_preferences.showGrid)) {
        savePreferences();
    }

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 1.0f, 1.0f), "Texture quality");
    ImGui::Separator();
    ImGui::TextWrapped(
        "Full-resolution textures near scene viewers (viewport camera + scene cameras). "
        "Quality decays smoothly beyond the radius.");
    if (ImGui::Checkbox("Distance-based texture quality", &m_preferences.textureQualityEnabled)) {
        savePreferences();
        if (m_editorContext) applyPreferencesToContext(*m_editorContext);
    }
    if (m_preferences.textureQualityEnabled) {
        if (ImGui::DragFloat("Full quality radius (m)", &m_preferences.textureQualityRadius, 1.0f,
                             5.0f, 500.0f, "%.0f m")) {
            savePreferences();
            if (m_editorContext) applyPreferencesToContext(*m_editorContext);
        }
        if (ImGui::DragFloat("Falloff distance (m)", &m_preferences.textureQualityFalloff, 2.0f,
                             10.0f, 1000.0f, "%.0f m")) {
            savePreferences();
            if (m_editorContext) applyPreferencesToContext(*m_editorContext);
        }
        if (ImGui::SliderFloat("Minimum quality", &m_preferences.textureQualityMinScale, 0.125f,
                               1.0f, "%.2f")) {
            savePreferences();
            if (m_editorContext) applyPreferencesToContext(*m_editorContext);
        }
    }
#endif
}

void SettingsPanel::renderPanelSettings() {
#ifdef CF_HAS_IMGUI
    LayoutProfile profile = m_layoutManager.currentProfile();
    bool changed = false;

    ImGui::TextColored(ImVec4(0.7f, 0.7f, 1.0f, 1.0f), "Visible Panels");
    ImGui::Separator();
    ImGui::TextWrapped("These options update the active layout profile. Save preferences to keep them between sessions.");

    changed |= ImGui::Checkbox("Hierarchy", &profile.hierarchyOpen);
    changed |= ImGui::Checkbox("Inspector", &profile.inspectorOpen);
    changed |= ImGui::Checkbox("Scene Viewport", &profile.viewportOpen);
    changed |= ImGui::Checkbox("Asset Browser", &profile.assetsOpen);
    changed |= ImGui::Checkbox("Console", &profile.consoleOpen);
    changed |= ImGui::Checkbox("Profiler", &profile.profilerOpen);
    changed |= ImGui::Checkbox("Animation Timeline", &profile.animationTimelineOpen);
    changed |= ImGui::Checkbox("Animator Controller", &profile.animatorControllerOpen);
    changed |= ImGui::Checkbox("Tilemap Editor", &profile.tilemapEditorOpen);
    changed |= ImGui::Checkbox("Script Editor", &profile.scriptEditorOpen);

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 1.0f, 1.0f), "Panel Widths");
    ImGui::Separator();
    changed |= ImGui::SliderFloat("Hierarchy width", &profile.hierarchyWidth, 0.12f, 0.4f, "%.2f");
    changed |= ImGui::SliderFloat("Inspector width", &profile.inspectorWidth, 0.12f, 0.4f, "%.2f");

    if (changed) {
        m_layoutManager.updateCurrentProfile(profile);
        if (m_onLayoutChange) {
            m_onLayoutChange();
        }
        savePreferences();
    }
#endif
}

void SettingsPanel::applyLayoutProfile(const std::string& profileName) {
    if (!m_layoutManager.applyProfile(profileName)) return;
    m_preferences.activeLayoutProfile = profileName;
    if (m_onLayoutChange) {
        m_onLayoutChange();
    }
}

}  // namespace Caffeine::Editor
