#pragma once
#include "core/Types.hpp"
#include "editor/ProjectManager.hpp"
#include <optional>
#include <string>
#include <filesystem>

namespace Caffeine::Editor {

// ============================================================================
// ProjectStartupDialog — Project selection/creation modal for editor startup
//
// Usage:
//   ProjectStartupDialog dialog;
//   dialog.init();
//   while (dialog.isOpen()) {
//       imgui.beginFrame();
//       if (auto config = dialog.render()) {
//           // User selected or created a project
//           SceneEditor.init(config.value());
//           break;
//       }
//       imgui.endFrame();
//   }
// ============================================================================
class ProjectStartupDialog {
public:
    ProjectStartupDialog();
    ~ProjectStartupDialog() = default;

    // Non-copyable
    ProjectStartupDialog(const ProjectStartupDialog&) = delete;
    ProjectStartupDialog& operator=(const ProjectStartupDialog&) = delete;

    // Initialize dialog (loads recent projects, prepares UI state)
    void init();

    // Render dialog each frame
    // Returns ProjectConfig if user selected/created a project
    // Returns std::nullopt if dialog still open
    // Modal blocks interaction with other windows
    std::optional<ProjectConfig> render();

    // Check if dialog is still open
    bool isOpen() const { return m_open; }

    // Close dialog without selecting project (user quit)
    void close() { m_open = false; }

private:
    // ── UI state ────────────────────────────────────────────────────────
    bool m_open = true;
    int m_activeTab = 0;  // 0=Create, 1=Recent, 2=Browse

    // ── Create tab state ────────────────────────────────────────────────
    char m_projectName[256] = {0};
    int m_templateIndex = 0;  // 0=Empty, 1=2D, 2=3D
    std::string m_selectedLocation;
    bool m_locationPicked = false;
    char m_errorMessage[512] = {0};
    bool m_showError = false;

    // ── Browse tab state ────────────────────────────────────────────────
    std::string m_browsePath;
    std::vector<std::filesystem::path> m_browseResults;

    // ── ProjectManager for file operations ────────────────────────────
    ProjectManager m_projectManager;

    // ── Helper methods ────────────────────────────────────────────────
    std::optional<ProjectConfig> tryCreateProject();
    std::optional<ProjectConfig> tryOpenProject(const std::filesystem::path& path);
    void setError(const char* message);

    // ── UI helpers ────────────────────────────────────────────────────
    #ifdef CF_HAS_IMGUI
    std::optional<ProjectConfig> renderCreateTab();
    std::optional<ProjectConfig> renderRecentTab();
    std::optional<ProjectConfig> renderBrowseTab();
    void renderErrorPopup();
    #endif
};

} // namespace Caffeine::Editor
