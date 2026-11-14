#pragma once
#include "editor/EditorPreferences.hpp"
#include "editor/LayoutProfile.hpp"
#include "editor/LayoutManager.hpp"
#include "scene/EditorContext.hpp"
#include <string>
#include <functional>

namespace Caffeine::UIKit::Elements {
class Container;
}

namespace Caffeine::Editor {

// ============================================================================
// SettingsPanel — UI for managing editor preferences and layout profiles.
// ============================================================================
class SettingsPanel {
public:
    SettingsPanel();
    ~SettingsPanel() = default;

    void open() { m_open = true; }
    void close() { m_open = false; }
    bool isOpen() const { return m_open; }
    void toggle() { m_open = !m_open; }

    void render();

    void applyLayoutProfile(const std::string& profileName);
    void savePreferences();
    void applyPreferencesToContext(EditorContext& ctx);

    LayoutManager& layoutManager() { return m_layoutManager; }
    const EditorPreferences& preferences() const { return m_preferences; }

    void setLayoutChangeCallback(std::function<void()> callback) { m_onLayoutChange = callback; }
    void setEditorContext(EditorContext* ctx) { m_editorContext = ctx; }

private:
    bool m_open = false;
    LayoutManager m_layoutManager;
    EditorPreferences m_preferences;

    std::string m_newProfileName;
    std::string m_selectedProfileName;
    int m_selectedProfileIndex = 0;
    std::function<void()> m_onLayoutChange;
    EditorContext* m_editorContext = nullptr;

    bool m_vsyncEnabled = true;
    int  m_fontSize = 14;
    bool m_darkMode = true;
    bool m_autoSaveEnabled = true;
    int  m_autoSaveInterval = 300;
    int  m_settingsSection = 0;
    LayoutProfile m_panelProfile;
    char m_shortcutSearch[96] = {};

    void loadPreferences();
    void syncUIFromPreferences();
    void syncPreferencesFromUI();
    void renderLayoutProfiles(Caffeine::UIKit::Elements::Container& parent);
    void renderGeneralSettings(Caffeine::UIKit::Elements::Container& parent);
    void renderEditorSettings(Caffeine::UIKit::Elements::Container& parent);
    void renderViewportSettings(Caffeine::UIKit::Elements::Container& parent);
    void renderPanelSettings(Caffeine::UIKit::Elements::Container& parent);
    void renderShortcutSettings(Caffeine::UIKit::Elements::Container& parent);
    void commitPanelProfile();
};

} // namespace Caffeine::Editor
