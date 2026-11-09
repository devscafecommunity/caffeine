#include "caffeine/ui/UI.hpp"
#include "editor/SettingsPanel.hpp"
#include "editor/EditorIcons.hpp"
#include "editor/EditorShortcuts.hpp"
#include "editor/EditorTheme.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

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
    ctx.renderScale = std::clamp(m_preferences.renderScale, 0.25f, 2.0f);
    const int aa = std::clamp(m_preferences.viewportAntiAlias, 0, 2);
    m_preferences.viewportAntiAlias = aa;
    ctx.viewportAntiAlias = static_cast<EditorContext::ViewportAntiAlias>(aa);
}

void SettingsPanel::render() {
#ifdef CF_HAS_IMGUI
    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;
    if (!m_open) return;

    Panel root("Settings", "Editor preferences and layouts", "settings", &m_open);
    UIContext ui;

    auto& columns = root.emplace<Row>("settings_columns");
    auto& nav = columns.emplace<Card>("settings_nav");
    nav.setWidth(210.0f);
    nav.emplace<Label>("Settings", TextTone::Accent).setHeading(true);
    auto& navList = nav.emplace<ListBox>("settings_nav_list");
    navList.setHeight(400.0f);
    static const char* navItems[] = {"General",   "Editor",  "Viewport",
                                     "Panels",    "Layout Profiles", "Shortcuts"};
    for (int i = 0; i < 6; ++i) {
        auto& item = navList.emplace<ListItem>(navItems[i], m_settingsSection == i);
        item.setId("settings_nav_" + std::to_string(i));
        item.setOnClick([this, i] { m_settingsSection = i; });
    }

    auto& content = columns.emplace<Card>("settings_content");
    content.setGrow(true);
    switch (m_settingsSection) {
        case 0: renderGeneralSettings(content); break;
        case 1: renderEditorSettings(content); break;
        case 2: renderViewportSettings(content); break;
        case 3: renderPanelSettings(content); break;
        case 4: renderLayoutProfiles(content); break;
        case 5: renderShortcutSettings(content); break;
        default: break;
    }

    root.emplace<Separator>(true);
    auto& footer = root.emplace<Row>("settings_footer");
    footer.emplace<Button>("Save Preferences", ButtonVariant::Primary)
        .setOnClick([this] { savePreferences(); });
    footer.emplace<Label>("Stored in " + EditorPreferences::preferencesPath().string(),
                          TextTone::Muted);

    ui.draw(root);
#endif
}

void SettingsPanel::renderGeneralSettings(
    Caffeine::UIKit::Elements::Container& parent) {
#ifdef CF_HAS_IMGUI
    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;

    auto& appearance = parent.emplace<Card>("general_appearance");
    appearance.emplace<Label>("Appearance", TextTone::Accent).setHeading(true);
    auto& appearanceGrid = appearance.emplace<PropertyGrid>("general_appearance_grid");
    appearanceGrid.typedRow<BoolProperty>("Enable VSync", m_vsyncEnabled)
        .setOnChange([this](bool) { savePreferences(); })
        .setHint("Requires restart to take effect.");
    appearanceGrid.typedRow<IntProperty>("UI Font Size", m_fontSize, 10, 20, 1.0f, "%d", true)
        .setOnChange([this](int value) {
            m_fontSize = value;
            EditorTheme::setFontSize(static_cast<f32>(m_fontSize));
#ifdef CF_HAS_SDL3
            ImGui_ImplSDLGPU3_CreateDeviceObjects();
#endif
            savePreferences();
        });
    appearanceGrid.typedRow<BoolProperty>("Dark Mode", m_darkMode)
        .setOnChange([this](bool value) {
            m_darkMode = value;
            EditorThemeSettings theme;
            theme.darkMode = m_darkMode;
            theme.fontSize = static_cast<f32>(m_fontSize);
            EditorTheme::apply(theme);
#ifdef CF_HAS_SDL3
            ImGui_ImplSDLGPU3_CreateDeviceObjects();
#endif
            savePreferences();
        });

    auto& workflow = parent.emplace<Card>("general_workflow");
    workflow.emplace<Label>("Project Workflow", TextTone::Accent).setHeading(true);
    auto& workflowGrid = workflow.emplace<PropertyGrid>("general_workflow_grid");
    workflowGrid
        .typedRow<BoolProperty>("Reopen last scene on startup",
                                m_preferences.reopenLastSceneOnStartup)
        .setOnChange([this](bool) { savePreferences(); });
    workflowGrid
        .typedRow<BoolProperty>("Confirm before closing unsaved scenes",
                                m_preferences.confirmOnSceneClose)
        .setOnChange([this](bool) { savePreferences(); });
    workflowGrid
        .typedRow<BoolProperty>("Show FPS in status bar",
                                m_preferences.showFPSInStatusBar)
        .setOnChange([this](bool) { savePreferences(); });

    auto& autoSave = parent.emplace<Card>("general_autosave");
    autoSave.emplace<Label>("Auto-save", TextTone::Accent).setHeading(true);
    auto& autoGrid = autoSave.emplace<PropertyGrid>("general_autosave_grid");
    autoGrid.typedRow<BoolProperty>("Enable Auto-save", m_autoSaveEnabled)
        .setOnChange([this](bool) { savePreferences(); });
    autoGrid.typedRow<IntProperty>("Auto-save interval (seconds)", m_autoSaveInterval, 10,
                                   3600, 1.0f, "%d")
        .setOnChange([this](int) { savePreferences(); });
#endif
}

void SettingsPanel::renderEditorSettings(
    Caffeine::UIKit::Elements::Container& parent) {
#ifdef CF_HAS_IMGUI
    using namespace Caffeine::UIKit::Elements;

    auto& transform = parent.emplace<Card>("editor_transform");
    transform.emplace<Label>("Transform Defaults", TextTone::Accent).setHeading(true);
    auto& transformGrid = transform.emplace<PropertyGrid>("editor_transform_grid");
    transformGrid
        .typedRow<BoolProperty>("Uniform scale by default",
                                m_preferences.uniformScaleDefault)
        .setOnChange([this](bool value) {
            m_preferences.uniformScaleDefault = value;
            if (m_editorContext) m_editorContext->uniformScale = value;
            savePreferences();
        });

    auto& snapping = parent.emplace<Card>("editor_snapping");
    snapping.emplace<Label>("Snapping", TextTone::Accent).setHeading(true);
    auto& snapGrid = snapping.emplace<PropertyGrid>("editor_snapping_grid");
    snapGrid.typedRow<BoolProperty>("Enable snapping", m_preferences.snapEnabled)
        .setOnChange([this](bool value) {
            m_preferences.snapEnabled = value;
            if (m_editorContext) m_editorContext->snapToGrid = value;
            savePreferences();
        });
    snapGrid.typedRow<FloatProperty>("Snap increment", m_preferences.snapIncrement, 0.001f,
                                     10.0f, 0.01f, "%.3f")
        .setOnChange([this](float value) {
            m_preferences.snapIncrement = value;
            if (m_editorContext) m_editorContext->snapGridSize = value;
            savePreferences();
        });
    snapGrid.typedRow<FloatProperty>("Default grid size", m_preferences.defaultGridSize,
                                     0.01f, 10.0f, 0.01f, "%.2f")
        .setOnChange([this](float) { savePreferences(); });
#endif
}

void SettingsPanel::renderViewportSettings(
    Caffeine::UIKit::Elements::Container& parent) {
#ifdef CF_HAS_IMGUI
    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;

    auto& camera = parent.emplace<Card>("viewport_camera");
    camera.emplace<Label>("Camera", TextTone::Accent).setHeading(true);
    auto& cameraGrid = camera.emplace<PropertyGrid>("viewport_camera_grid");
    cameraGrid.typedRow<FloatProperty>("Move speed", m_preferences.cameraMoveSpeed, 0.1f,
                                       50.0f, 0.1f, "%.1f")
        .setOnChange([this](float) { savePreferences(); });
    cameraGrid.typedRow<FloatProperty>("Orbit sensitivity", m_preferences.cameraOrbitSpeed,
                                       0.0005f, 0.05f, 0.0005f, "%.4f")
        .setOnChange([this](float) { savePreferences(); });

    auto& grid = parent.emplace<Card>("viewport_grid");
    grid.emplace<Label>("Grid", TextTone::Accent).setHeading(true);
    auto& gridGrid = grid.emplace<PropertyGrid>("viewport_grid_grid");
    gridGrid.typedRow<BoolProperty>("Show grid in viewport", m_preferences.showGrid)
        .setOnChange([this](bool) { savePreferences(); });

    auto& rendering = parent.emplace<Card>("viewport_render");
    rendering.emplace<Label>("Rendering", TextTone::Accent).setHeading(true);
    rendering.emplace<Elements::Paragraph>(
        "Scene viewport and play-mode previews render at viewport size times this scale "
        "(HDR + TAA + post stack). Use 1.0 for native resolution, 1.5-2.0 for sharper edges.");
    auto& renderGrid = rendering.emplace<PropertyGrid>("viewport_render_grid");
    renderGrid.typedRow<FloatProperty>("Render scale", m_preferences.renderScale, 0.25f,
                                       2.0f, 0.01f, "%.2fx", true)
        .setOnChange([this](float value) {
            m_preferences.renderScale = std::clamp(value, 0.25f, 2.0f);
            savePreferences();
            if (m_editorContext) applyPreferencesToContext(*m_editorContext);
        });
    rendering.emplace<Elements::Paragraph>(
        "Anti-aliasing for the scene viewport only. The game keeps the anti-aliasing "
        "set on each camera's post-processing.");
    renderGrid.typedRow<ChoiceProperty>(
        "Viewport anti-aliasing", m_preferences.viewportAntiAlias,
        std::vector<std::string>{"Off", "FXAA", "TAA"})
        .setOnChange([this](int value) {
            m_preferences.viewportAntiAlias = std::clamp(value, 0, 2);
            savePreferences();
            if (m_editorContext) applyPreferencesToContext(*m_editorContext);
        });

    auto& texture = parent.emplace<Card>("viewport_texture");
    texture.emplace<Label>("Texture quality", TextTone::Accent).setHeading(true);
    texture.emplace<Elements::Paragraph>(
        "Full-resolution textures near scene viewers (viewport camera + scene cameras). "
        "Quality decays smoothly beyond the radius.");
    auto& textureGrid = texture.emplace<PropertyGrid>("viewport_texture_grid");
    textureGrid
        .typedRow<BoolProperty>("Distance-based texture quality",
                                m_preferences.textureQualityEnabled)
        .setOnChange([this](bool) {
            savePreferences();
            if (m_editorContext) applyPreferencesToContext(*m_editorContext);
        });
    if (m_preferences.textureQualityEnabled) {
        textureGrid
            .typedRow<FloatProperty>("Full quality radius (m)",
                                     m_preferences.textureQualityRadius, 5.0f, 500.0f,
                                     1.0f, "%.0f m")
            .setOnChange([this](float) {
                savePreferences();
                if (m_editorContext) applyPreferencesToContext(*m_editorContext);
            });
        textureGrid
            .typedRow<FloatProperty>("Falloff distance (m)",
                                     m_preferences.textureQualityFalloff, 10.0f, 1000.0f,
                                     2.0f, "%.0f m")
            .setOnChange([this](float) {
                savePreferences();
                if (m_editorContext) applyPreferencesToContext(*m_editorContext);
            });
        textureGrid
            .typedRow<FloatProperty>("Minimum quality",
                                     m_preferences.textureQualityMinScale, 0.125f, 1.0f,
                                     0.01f, "%.2f", true)
            .setOnChange([this](float) {
                savePreferences();
                if (m_editorContext) applyPreferencesToContext(*m_editorContext);
            });
    }
#endif
}

void SettingsPanel::commitPanelProfile() {
    m_layoutManager.updateCurrentProfile(m_panelProfile);
    if (m_onLayoutChange) m_onLayoutChange();
    savePreferences();
}

void SettingsPanel::renderPanelSettings(
    Caffeine::UIKit::Elements::Container& parent) {
#ifdef CF_HAS_IMGUI
    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;

    m_panelProfile = m_layoutManager.currentProfile();

    auto& panels = parent.emplace<Card>("layout_panels");
    panels.emplace<Label>("Visible Panels", TextTone::Accent).setHeading(true);
    panels.emplace<Elements::Paragraph>(
        "These options update the active layout profile. Save preferences to keep them "
        "between sessions.");
    auto& panelGrid = panels.emplace<PropertyGrid>("layout_panels_grid");
    auto toggle = [this, &panelGrid](const char* label, bool& value) {
        panelGrid.typedRow<BoolProperty>(label, value)
            .setOnChange([this](bool) { commitPanelProfile(); });
    };
    toggle("Hierarchy", m_panelProfile.hierarchyOpen);
    toggle("Inspector", m_panelProfile.inspectorOpen);
    toggle("Scene Viewport", m_panelProfile.viewportOpen);
    toggle("Asset Browser", m_panelProfile.assetsOpen);
    toggle("Console", m_panelProfile.consoleOpen);
    toggle("Profiler", m_panelProfile.profilerOpen);
    toggle("Animation Timeline", m_panelProfile.animationTimelineOpen);
    toggle("Animator Controller", m_panelProfile.animatorControllerOpen);
    toggle("Tilemap Editor", m_panelProfile.tilemapEditorOpen);
    toggle("Script Editor", m_panelProfile.scriptEditorOpen);

    auto& widths = parent.emplace<Card>("layout_widths");
    widths.emplace<Label>("Panel Widths", TextTone::Accent).setHeading(true);
    auto& widthGrid = widths.emplace<PropertyGrid>("layout_widths_grid");
    widthGrid.typedRow<FloatProperty>("Hierarchy width", m_panelProfile.hierarchyWidth,
                                      0.12f, 0.4f, 0.01f, "%.2f", true)
        .setOnChange([this](float) { commitPanelProfile(); });
    widthGrid.typedRow<FloatProperty>("Inspector width", m_panelProfile.inspectorWidth,
                                      0.12f, 0.4f, 0.01f, "%.2f", true)
        .setOnChange([this](float) { commitPanelProfile(); });
#endif
}

void SettingsPanel::renderLayoutProfiles(
    Caffeine::UIKit::Elements::Container& parent) {
#ifdef CF_HAS_IMGUI
    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;

    const auto& profiles = m_layoutManager.profiles();

    auto& listCard = parent.emplace<Card>("profiles_list");
    listCard.emplace<Label>("Available Profiles", TextTone::Accent).setHeading(true);
    auto& list = listCard.emplace<ListBox>("profiles_box");
    list.setHeight(200.0f);
    for (int i = 0; i < static_cast<int>(profiles.size()); ++i) {
        auto& item = list.emplace<ListItem>(profiles[i].name, m_selectedProfileIndex == i);
        item.setId("profile_" + std::to_string(i));
        item.setOnClick([this, i, name = profiles[i].name] {
            m_selectedProfileIndex = i;
            m_selectedProfileName = name;
        });
    }
    auto& actions = listCard.emplace<Row>("profiles_actions");
    actions.emplace<Button>("Apply Profile")
        .setOnClick([this] {
            applyLayoutProfile(m_selectedProfileName);
            savePreferences();
        });
    actions.emplace<Label>("Current: " + m_layoutManager.currentProfile().name,
                           TextTone::Muted);

    auto& saveCard = parent.emplace<Card>("profiles_save");
    saveCard.emplace<Label>("Save Current Layout", TextTone::Accent).setHeading(true);
    auto& saveGrid = saveCard.emplace<PropertyGrid>("profiles_save_grid");
    saveGrid.typedRow<TextFieldProperty>("Profile Name", m_newProfileName, 128);
    auto& saveActions = saveCard.emplace<Row>("profiles_save_actions");
    saveActions.emplace<Button>("Save As New Profile", ButtonVariant::Primary)
        .setOnClick([this] {
            if (m_newProfileName.empty()) return;
            LayoutProfile newProfile = m_layoutManager.currentProfile();
            newProfile.name = m_newProfileName;
            if (m_layoutManager.saveProfile(newProfile)) {
                m_selectedProfileName = m_newProfileName;
                savePreferences();
                m_newProfileName.clear();
            }
        });

    if (!profiles.empty() &&
        m_selectedProfileIndex < static_cast<int>(profiles.size())) {
        const auto& selectedProfile = profiles[m_selectedProfileIndex];
        const bool isBuiltIn =
            selectedProfile.name == "Default" || selectedProfile.name == "Vertical" ||
            selectedProfile.name == "Horizontal" || selectedProfile.name == "Compact" ||
            selectedProfile.name == "Fullscreen";
        if (isBuiltIn) {
            saveCard.emplace<Label>("(Built-in profiles cannot be deleted)",
                                    TextTone::Disabled);
        } else {
            saveCard.emplace<Button>("Delete Selected Profile", ButtonVariant::Danger)
                .setOnClick([this] {
                    if (m_selectedProfileIndex < 0 ||
                        m_selectedProfileIndex >=
                            static_cast<int>(m_layoutManager.profiles().size())) {
                        return;
                    }
                    const std::string name =
                        m_layoutManager.profiles()[m_selectedProfileIndex].name;
                    m_layoutManager.deleteProfile(name);
                    m_selectedProfileIndex = 0;
                    if (!m_layoutManager.profiles().empty()) {
                        m_selectedProfileName = m_layoutManager.profiles()[0].name;
                    }
                    savePreferences();
                });
        }
    }
#endif
}

void SettingsPanel::renderShortcutSettings(
    Caffeine::UIKit::Elements::Container& parent) {
#ifdef CF_HAS_IMGUI
    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;

    auto& card = parent.emplace<Card>("shortcuts");
    card.emplace<Label>("IDE Shortcuts", TextTone::Accent).setHeading(true);
    card.emplace<Elements::Paragraph>(
        "Click Assign and press the new key. Ctrl and Shift are stored if they are held. "
        "Esc cancels.");
    if (EditorShortcuts::instance().pollCapture()) savePreferences();

    auto& actions = card.emplace<Row>("shortcut_actions");
    actions.emplace<Button>("Reset Shortcuts")
        .setOnClick([this] {
            EditorShortcuts::instance().resetDefaults();
            savePreferences();
        });
    auto& search = card.emplace<Elements::SearchField>(
        m_shortcutSearch, sizeof(m_shortcutSearch), "Search shortcuts...");
    search.setGrow(true);

    struct ShortcutGroup {
        const char* name;
        Shortcut ids[8];
        int count;
    };
    const ShortcutGroup groups[] = {
        {"Transform", {Shortcut::GizmoTranslate, Shortcut::GizmoRotate,
                        Shortcut::GizmoScale}, 3},
        {"Camera", {Shortcut::CameraForward, Shortcut::CameraBack, Shortcut::CameraLeft,
                     Shortcut::CameraRight}, 4},
        {"Edit", {Shortcut::NewScene, Shortcut::Save, Shortcut::Undo, Shortcut::Redo,
                   Shortcut::Copy, Shortcut::Paste, Shortcut::Duplicate,
                   Shortcut::DeleteSelection}, 8},
        {"Windows", {Shortcut::CommandPalette, Shortcut::CommandPaletteAlt}, 2},
    };

    auto contains = [](const char* haystack, const char* needle) {
        if (!needle || needle[0] == '\0') return true;
        if (!haystack) return false;
        for (const char* start = haystack; *start; ++start) {
            const char* left = start;
            const char* right = needle;
            while (*left && *right &&
                   std::tolower(static_cast<unsigned char>(*left)) ==
                       std::tolower(static_cast<unsigned char>(*right))) {
                ++left;
                ++right;
            }
            if (*right == '\0') return true;
        }
        return false;
    };

    auto& tree = card.emplace<TreeView>("shortcut_tree");
    tree.setHeight(360.0f);
    bool any = false;
    for (const ShortcutGroup& group : groups) {
        Shortcut shown[8] = {};
        int shownCount = 0;
        const bool groupMatch =
            m_shortcutSearch[0] != '\0' && contains(group.name, m_shortcutSearch);
        for (int i = 0; i < group.count; ++i) {
            const Shortcut id = group.ids[i];
            if (groupMatch || contains(EditorShortcuts::label(id), m_shortcutSearch) ||
                contains(EditorShortcuts::instance().chord(id), m_shortcutSearch)) {
                shown[shownCount++] = id;
            }
        }
        if (shownCount == 0) continue;
        any = true;
        auto& groupItem = tree.emplace<TreeItem>(group.name, true);
        for (int i = 0; i < shownCount; ++i) {
            const Shortcut id = shown[i];
            auto& row = groupItem.emplace<Row>(
                "shortcut_" + std::to_string(static_cast<int>(id)));
            row.emplace<Label>(EditorShortcuts::label(id)).setGrow(true);
            row.emplace<Label>(EditorShortcuts::instance().chord(id), TextTone::Muted);
            const bool capturing = EditorShortcuts::instance().capturing(id);
            auto& assign = row.emplace<Button>(capturing ? "..." : "Assign",
                                               ButtonVariant::Toolbar);
            assign.setOnClick([id] { EditorShortcuts::instance().beginCapture(id); });
        }
    }
    if (!any) card.emplace<Label>("No shortcuts match.", TextTone::Muted);
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
