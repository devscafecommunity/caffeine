#pragma once
#include "editor/LayoutProfile.hpp"
#include "editor/LayoutManager.hpp"
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

    // Apply layout profile to scene editor
    void applyLayoutProfile(const std::string& profileName);

    // Get layout manager (for integration with SceneEditor)
    LayoutManager& layoutManager() { return m_layoutManager; }
    // Set callback for layout changes
    void setLayoutChangeCallback(std::function<void()> callback) { m_onLayoutChange = callback; }


private:
    bool m_open = false;
    LayoutManager m_layoutManager;
    
    // UI state
    std::string m_newProfileName;
    std::string m_selectedProfileName;
    int m_selectedProfileIndex = 0;
    std::function<void()> m_onLayoutChange;


    void renderLayoutProfiles();
    void renderGeneralSettings();
};

} // namespace Caffeine::Editor
