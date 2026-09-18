#pragma once
#include "editor/EditorPreferences.hpp"
#include "editor/LayoutProfile.hpp"
#include "editor/LayoutManager.hpp"
#include "editor/EditorContext.hpp"
#include <string>
#include <functional>

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

    void loadPreferences();
    void syncUIFromPreferences();
    void syncPreferencesFromUI();
    void renderLayoutProfiles();
    void renderGeneralSettings();
    void renderEditorSettings();
    void renderViewportSettings();
    void renderPanelSettings();
};

} // namespace Caffeine::Editor
