#include "caffeine/ui/UI.hpp"
#include "editor/EntityPresetsPanel.hpp"
#include "editor/EntityPresetRegistry.hpp"
#include "editor/PluginSystem.hpp"
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
    if (!PluginManager::instance().isContributionEnabled(preset->plugin)) {
        m_statusMessage = "That preset belongs to a disabled plugin.";
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

void EntityPresetsPanel::renderCategoryList(
    Caffeine::UIKit::Elements::Container& parent) {
    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;
    auto& list = parent.emplace<ListBox>("category_list");
    list.setHeight(400.0f);
    for (const auto& category : EntityPresetRegistry::instance().categories()) {
        const bool selected = (m_selectedCategory == category.id);
        auto& item = list.emplace<ListItem>(category.displayName, selected);
        item.setId("category:" + category.id);
        item.setTooltip(category.description);
        item.setOnClick([this, id = category.id] {
            m_selectedCategory = id;
            m_selectedPresetId.clear();
        });
    }
}

void EntityPresetsPanel::renderPresetList(
    Caffeine::UIKit::Elements::Container& parent) {
    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;
    const auto presets = EntityPresetRegistry::instance().presetsInCategory(m_selectedCategory);
    auto& list = parent.emplace<ListBox>("preset_list");
    list.setHeight(400.0f);
    if (presets.empty()) {
        parent.emplace<Label>("No presets in this category.", TextTone::Muted);
        return;
    }
    for (const auto* preset : presets) {
        if (!PluginManager::instance().isContributionEnabled(preset->plugin)) continue;
        const bool selected = (m_selectedPresetId == preset->id);
        auto& item = list.emplace<ListItem>(preset->displayName, selected);
        item.setId("preset:" + preset->id);
        item.setTooltip(preset->description + "\nSource: " + preset->source);
        item.setOnClick([this, id = preset->id] { selectPreset(id); });
    }
}

void EntityPresetsPanel::renderWizard(Caffeine::UIKit::Elements::Container& parent,
                                      ECS::World& world, EditorContext& ctx) {
    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;
    const auto* preset = EntityPresetRegistry::instance().findPreset(m_selectedPresetId);
    if (!preset) {
        parent.emplace<Label>("Select a preset to configure it.", TextTone::Muted);
        return;
    }

    parent.emplace<Label>(preset->displayName, TextTone::Accent).setHeading(true);
    parent.emplace<Elements::Paragraph>(preset->description);
    parent.emplace<Label>("Source: " + preset->source, TextTone::Disabled);
    parent.emplace<Separator>();

    auto& grid = parent.emplace<PropertyGrid>("preset_wizard");
    grid.typedRow<TextFieldProperty>("Entity Name", m_wizard.entityName, 96);
    for (auto& field : m_wizard.fields) {
        switch (field.type) {
            case EntityPresetWizardField::Type::Bool: {
                auto& row = grid.typedRow<BoolProperty>(field.label, field.boolValue);
                if (!field.hint.empty()) row.setHint(field.hint);
                break;
            }
            case EntityPresetWizardField::Type::Float: {
                auto& row = grid.typedRow<FloatProperty>(
                    field.label, field.floatValue, field.floatMin, field.floatMax, 0.01f,
                    "%.2f", true);
                if (!field.hint.empty()) row.setHint(field.hint);
                break;
            }
            case EntityPresetWizardField::Type::Int: {
                auto& row = grid.typedRow<IntProperty>(
                    field.label, field.intValue, field.intMin, field.intMax, 1.0f, "%d",
                    true);
                if (!field.hint.empty()) row.setHint(field.hint);
                break;
            }
            case EntityPresetWizardField::Type::String: {
                auto& row = grid.typedRow<TextFieldProperty>(
                    field.label, field.stringValue, 128);
                if (!field.hint.empty()) row.setHint(field.hint);
                break;
            }
            case EntityPresetWizardField::Type::Enum: {
                auto& row = grid.typedRow<ChoiceProperty>(
                    field.label, field.enumIndex, field.enumOptions);
                if (!field.hint.empty()) row.setHint(field.hint);
                break;
            }
        }
    }

    auto& create = parent.emplace<Button>("Create in Scene", ButtonVariant::Primary);
    create.setGrow(true);
    create.setOnClick([this, &world, &ctx] { createSelectedPreset(world, ctx); });

    if (!m_statusMessage.empty()) {
        parent.emplace<Label>(m_statusMessage,
                              m_statusIsError ? TextTone::Danger : TextTone::Success);
    }
    parent.emplace<Elements::Paragraph>(
        "Packages and plugins can add presets via entity_presets.json. Place manifests "
        "under packages/, plugins/, or assets/presets/packages/.");
}

void EntityPresetsPanel::render(ECS::World& world, EditorContext& ctx) {
    if (!m_open) return;

    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;

    editorPanelApplyDetach(m_detached, ImVec2(1100, 680));
    Panel root("Entity Presets", "Create entities from presets", "presets", &m_open);
    root.setInitialSize(ImVec2(920, 520));
    root.setOnBegin([this] { editorPanelDetachTabButton(m_detached); });
    UIContext ui;

    auto& columns = root.emplace<Row>("preset_columns");
    auto& categories = columns.emplace<Card>("categories_card");
    categories.setWidth(170.0f);
    categories.emplace<Label>("Categories", TextTone::Accent).setHeading(true);
    renderCategoryList(categories);

    auto& presets = columns.emplace<Card>("presets_card");
    presets.setWidth(220.0f);
    presets.emplace<Label>("Presets", TextTone::Accent).setHeading(true);
    renderPresetList(presets);

    auto& wizard = columns.emplace<Card>("wizard_card");
    wizard.setGrow(true);
    renderWizard(wizard, world, ctx);

    ui.draw(root);
}

}  // namespace Caffeine::Editor

#endif
