#pragma once
#include "core/Types.hpp"
#include "editor/ProjectManager.hpp"
#include "editor/FilePicker.hpp"
#include <optional>
#include <string>
#include <filesystem>
#include <vector>
#include <cstdint>

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
    bool m_popupOpened = false;  // Track if popup has been opened this session
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
    int m_selectedBrowseIndex = -1;

    // ── Recent tab state ────────────────────────────────────────────────
    std::vector<std::filesystem::path> m_recentProjects;
    bool m_showAllRecents = false;
    char m_searchFilter[256] = {0};
    int m_selectedRecentIndex = -1;

    // ── Toast Notification System ───────────────────────────────
    enum class ToastType {
        Success,  // Green
        Error,    // Red
        Info      // Yellow
    };

    struct Toast {
        std::string message;
        ToastType type;
        double showTime;  // SDL_GetTicks() when created
        static constexpr double DURATION_MS = 3000.0;  // 3 seconds
        
        bool isExpired(double currentTime) const {
            return (currentTime - showTime) >= DURATION_MS;
        }
    };

    std::vector<Toast> m_toastQueue;
    static constexpr int MAX_VISIBLE_TOASTS = 3;

    // ── File picker state ────────────────────────────────────────────────
    bool m_showLocationPicker = false;
    bool m_showBrowsePicker = false;

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
    void showToast(const std::string& message, ToastType type);
    void updateToasts();
    void renderToasts();
    #endif
};

} // namespace Caffeine::Editor
