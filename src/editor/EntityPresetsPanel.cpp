#include "editor/EntityPresetsPanel.hpp"
#include "editor/EntityPresetRegistry.hpp"
#include "editor/EditorPanelUtils.hpp"

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#include <cstring>

namespace Caffeine::Editor {

void EntityPresetsPanel::selectPreset(const std::string& presetId) {
    m_selectedPresetId = presetId;
    const auto* preset = EntityPresetRegistry::instance().findPreset(presetId);
    if (!preset) return;
    m_wizard = EntityPresetRegistry::instance().makeDefaultWizardState(*preset);
}

void EntityPresetsPanel::createSelectedPreset(ECS::World& world, EditorContext& ctx) {
    const auto* preset = EntityPresetRegistry::instance().findPreset(m_selectedPresetId);
    if (!preset || !preset->spawn) {
        m_statusMessage = "No preset selected.";
        m_statusIsError = true;
        return;
    }
    if (m_projectRoot.empty()) {
        m_statusMessage = "Open a project before creating presets.";
        m_statusIsError = true;
        return;
    }

    EntityPresetSpawnResult result = preset->spawn(world, ctx, m_projectRoot, m_wizard);
    if (!result.ok()) {
        m_statusMessage = result.error.empty() ? "Failed to create preset." : result.error;
        m_statusIsError = true;
        return;
    }

    m_statusIsError = false;
    if (!result.infoMessages.empty()) {
        m_statusMessage = result.infoMessages.front();
    } else {
        m_statusMessage = "Preset created successfully.";
    }
    if (result.needsRebuild) {
        m_statusMessage += " Rebuild the project (Build & Run) to register C++ scripts.";
    }

    if (!result.createdScriptPaths.empty() && m_onScriptOpen) {
        m_onScriptOpen(result.createdScriptPaths.front());
    }
}

void EntityPresetsPanel::renderCategoryList() {
    ImGui::TextUnformatted("Categories");
    ImGui::Separator();
    for (const auto& category : EntityPresetRegistry::instance().categories()) {
        const bool selected = (m_selectedCategory == category.id);
        if (ImGui::Selectable(category.displayName.c_str(), selected)) {
            m_selectedCategory = category.id;
            m_selectedPresetId.clear();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", category.description.c_str());
        }
    }
}

void EntityPresetsPanel::renderPresetList() {
    const auto presets = EntityPresetRegistry::instance().presetsInCategory(m_selectedCategory);
    ImGui::TextUnformatted("Presets");
    ImGui::Separator();

    if (presets.empty()) {
        ImGui::TextDisabled("No presets in this category.");
        return;
    }

    for (const auto* preset : presets) {
        const bool selected = (m_selectedPresetId == preset->id);
        if (ImGui::Selectable(preset->displayName.c_str(), selected)) {
            selectPreset(preset->id);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s\nSource: %s", preset->description.c_str(), preset->source.c_str());
        }
    }
}

void EntityPresetsPanel::renderWizard() {
    const auto* preset = EntityPresetRegistry::instance().findPreset(m_selectedPresetId);
    if (!preset) {
        ImGui::TextDisabled("Select a preset to configure it.");
        return;
    }

    ImGui::Text("%s", preset->displayName.c_str());
    ImGui::TextWrapped("%s", preset->description.c_str());
    ImGui::TextDisabled("Source: %s", preset->source.c_str());
    ImGui::Separator();

    char nameBuf[96];
    std::strncpy(nameBuf, m_wizard.entityName.c_str(), sizeof(nameBuf) - 1);
    nameBuf[sizeof(nameBuf) - 1] = '\0';
    if (ImGui::InputText("Entity Name", nameBuf, sizeof(nameBuf))) {
        m_wizard.entityName = nameBuf;
    }

    for (auto& field : m_wizard.fields) {
        switch (field.type) {
            case EntityPresetWizardField::Type::Bool:
                ImGui::Checkbox(field.label.c_str(), &field.boolValue);
                break;
            case EntityPresetWizardField::Type::Float:
                ImGui::SliderFloat(field.label.c_str(), &field.floatValue, field.floatMin, field.floatMax);
                break;
            case EntityPresetWizardField::Type::Int:
                ImGui::SliderInt(field.label.c_str(), &field.intValue, field.intMin, field.intMax);
                break;
            case EntityPresetWizardField::Type::String:
                char buf[128];
                std::strncpy(buf, field.stringValue.c_str(), sizeof(buf) - 1);
                buf[sizeof(buf) - 1] = '\0';
                if (ImGui::InputText(field.label.c_str(), buf, sizeof(buf))) {
                    field.stringValue = buf;
                }
                break;
            case EntityPresetWizardField::Type::Enum:
                if (!field.enumOptions.empty()) {
                    if (ImGui::BeginCombo(field.label.c_str(), field.enumOptions[field.enumIndex].c_str())) {
                        for (int i = 0; i < static_cast<int>(field.enumOptions.size()); ++i) {
                            const bool selected = (field.enumIndex == i);
                            if (ImGui::Selectable(field.enumOptions[i].c_str(), selected)) {
                                field.enumIndex = i;
                            }
                        }
                        ImGui::EndCombo();
                    }
                }
                break;
        }
        if (!field.hint.empty() && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", field.hint.c_str());
        }
    }

}

void EntityPresetsPanel::render(ECS::World& world, EditorContext& ctx) {
    if (!m_open) return;

    editorPanelApplyDetach(m_detached, ImVec2(1100, 680));
    ImGui::SetNextWindowSize(ImVec2(920, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Entity Presets", &m_open)) {
        ImGui::End();
        return;
    }
    editorPanelDetachTabButton(m_detached);

    if (ImGui::BeginTable("entity_presets_layout", 3,
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Categories", ImGuiTableColumnFlags_WidthFixed, 170.0f);
        ImGui::TableSetupColumn("Presets", ImGuiTableColumnFlags_WidthFixed, 220.0f);
        ImGui::TableSetupColumn("Wizard", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        if (ImGui::BeginChild("preset_categories", ImVec2(0, 0), false)) {
            renderCategoryList();
        }
        ImGui::EndChild();

        ImGui::TableSetColumnIndex(1);
        if (ImGui::BeginChild("preset_list", ImVec2(0, 0), false)) {
            renderPresetList();
        }
        ImGui::EndChild();

        ImGui::TableSetColumnIndex(2);
        if (ImGui::BeginChild("preset_wizard", ImVec2(0, 0), false)) {
            renderWizard();
            ImGui::Separator();
            if (ImGui::Button("Create in Scene", ImVec2(-1, 0))) {
                createSelectedPreset(world, ctx);
            }
            if (!m_statusMessage.empty()) {
                const ImVec4 color = m_statusIsError ? ImVec4(1, 0.45f, 0.45f, 1) : ImVec4(0.5f, 1, 0.5f, 1);
                ImGui::TextColored(color, "%s", m_statusMessage.c_str());
            }
            ImGui::Separator();
            ImGui::TextWrapped(
                "Packages and plugins can add presets via entity_presets.json. "
                "Place manifests under packages/, plugins/, or assets/presets/packages/.");
        }
        ImGui::EndChild();

        ImGui::EndTable();
    }

    ImGui::End();
}

}  // namespace Caffeine::Editor

#endif
