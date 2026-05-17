#include "editor/SettingsPanel.hpp"

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

namespace Caffeine::Editor {

SettingsPanel::SettingsPanel() {
    m_layoutManager.loadProfiles();
    if (auto firstProfile = m_layoutManager.getProfile("Default")) {
        m_selectedProfileName = firstProfile->name;
    }
}

void SettingsPanel::render() {
#ifdef CF_HAS_IMGUI
    if (!m_open) return;

    if (ImGui::Begin("Settings", &m_open)) {
        if (ImGui::BeginTabBar("SettingsTabs")) {
            if (ImGui::BeginTabItem("Layout Profiles")) {
                renderLayoutProfiles();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("General")) {
                renderGeneralSettings();
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
    ImGui::End();
#endif
}

void SettingsPanel::renderLayoutProfiles() {
#ifdef CF_HAS_IMGUI
    const auto& profiles = m_layoutManager.profiles();

    ImGui::TextColored(ImVec4(0.7f, 0.7f, 1.0f, 1.0f), "Available Profiles:");
    ImGui::Separator();

    // Profile list
    if (ImGui::BeginListBox("##profiles", ImVec2(-1, 200))) {
        for (int i = 0; i < static_cast<int>(profiles.size()); ++i) {
            bool isSelected = (m_selectedProfileIndex == i);
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
    ImGui::Separator();

    // Apply selected profile
    if (ImGui::Button("Apply Profile", ImVec2(150, 0))) {
        applyLayoutProfile(m_selectedProfileName);
    }
    ImGui::SameLine();
    ImGui::Text("Current: %s", m_layoutManager.currentProfile().name.c_str());

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 1.0f, 1.0f), "Save Current Layout:");

    // Save new profile
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
                m_newProfileName.clear();
            }
        }
    }

    ImGui::Spacing();

    // Delete profile
    if (!profiles.empty() && m_selectedProfileIndex < static_cast<int>(profiles.size())) {
        const auto& selectedProfile = profiles[m_selectedProfileIndex];
        
        // Disable delete for built-in profiles
        bool isBuiltIn = selectedProfile.name == "Default"
                      || selectedProfile.name == "Vertical"
                      || selectedProfile.name == "Horizontal"
                      || selectedProfile.name == "Compact"
                      || selectedProfile.name == "Fullscreen";

        if (isBuiltIn) {
            ImGui::TextDisabled("(Built-in profiles cannot be deleted)");
        } else {
            if (ImGui::Button("Delete Selected Profile", ImVec2(200, 0))) {
                m_layoutManager.deleteProfile(selectedProfile.name);
                m_selectedProfileIndex = 0;
                if (!profiles.empty()) {
                    m_selectedProfileName = profiles[0].name;
                }
            }
        }
    }
#endif
}

void SettingsPanel::renderGeneralSettings() {
#ifdef CF_HAS_IMGUI
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 1.0f, 1.0f), "Editor Preferences:");
    ImGui::Separator();

    static bool vsyncEnabled = true;
    if (ImGui::Checkbox("Enable VSync", &vsyncEnabled)) {
        // TODO: Apply VSync setting
    }

    static int fontSize = 13;
    if (ImGui::SliderInt("UI Font Size", &fontSize, 10, 20)) {
        // TODO: Apply font size
    }

    static bool darkMode = true;
    if (ImGui::Checkbox("Dark Mode", &darkMode)) {
        // TODO: Apply theme
    }

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 1.0f, 1.0f), "Auto-save Settings:");
    ImGui::Separator();

    static bool autoSaveEnabled = true;
    if (ImGui::Checkbox("Enable Auto-save", &autoSaveEnabled)) {
        // TODO: Configure auto-save
    }

    static int autoSaveInterval = 300;
    if (ImGui::DragInt("Auto-save Interval (seconds)", &autoSaveInterval, 1, 10, 3600)) {
        // TODO: Update auto-save interval
    }
#endif
}

void SettingsPanel::applyLayoutProfile(const std::string& profileName) {
    auto profile = m_layoutManager.getProfile(profileName);
     if (profile) {
         m_layoutManager.updateCurrentProfile(*profile);
         if (m_onLayoutChange) {
             m_onLayoutChange();
         }
     }
}

} // namespace Caffeine::Editor
