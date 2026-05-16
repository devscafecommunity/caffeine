#include "editor/ProjectStartupDialog.hpp"
#include <filesystem>
#include <cstring>

namespace Caffeine::Editor {

ProjectStartupDialog::ProjectStartupDialog() {
    // Set default location to ~/Documents/CaffeineProjects
    const char* home = std::getenv("HOME");
    if (!home) home = ".";
    m_selectedLocation = std::filesystem::path(home) / "Documents" / "CaffeineProjects";
    
    m_projectName[0] = '\0';
    m_errorMessage[0] = '\0';
}

void ProjectStartupDialog::init() {
    m_locationPicked = false;
}

std::optional<ProjectConfig> ProjectStartupDialog::tryCreateProject() {
    if (std::string(m_projectName).empty()) {
        setError("Project name cannot be empty");
        return std::nullopt;
    }

    ProjectConfig config;
    config.Name = m_projectName;
    config.RootPath = std::filesystem::path(m_selectedLocation) / m_projectName;
    config.TemplateType = (m_templateIndex == 0) ? "Empty" : (m_templateIndex == 1) ? "2D" : "3D";
    config.LastScene = "";

    if (!m_projectManager.CreateNewProject(config)) {
        setError("Failed to create project. Check permissions and path.");
        return std::nullopt;
    }

    return config;
}

std::optional<ProjectConfig> ProjectStartupDialog::tryOpenProject(const std::filesystem::path& path) {
    if (!m_projectManager.OpenProject(path)) {
        setError("Failed to open project. Invalid project.caffeine file.");
        return std::nullopt;
    }
    return m_projectManager.GetCurrentProject();
}

void ProjectStartupDialog::setError(const char* message) {
    if (message) {
        std::strncpy(m_errorMessage, message, sizeof(m_errorMessage) - 1);
        m_errorMessage[sizeof(m_errorMessage) - 1] = '\0';
    }
    m_showError = true;
}

// ── UI Layer (requires CF_HAS_IMGUI) ──────────────────────────────────────

#ifdef CF_HAS_IMGUI
#include <imgui.h>

std::optional<ProjectConfig> ProjectStartupDialog::render() {
    if (!m_open) return std::nullopt;

    ImGui::OpenPopup("ProjectManagerModal");

    std::optional<ProjectConfig> result;
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    
    if (ImGui::BeginPopupModal("ProjectManagerModal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Welcome to Doppio — Select or Create a Project");
        ImGui::Separator();

        if (ImGui::BeginTabBar("ProjectTabs")) {
            if (ImGui::BeginTabItem("Create New")) {
                result = renderCreateTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Recent Projects")) {
                result = renderRecentTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Browse")) {
                result = renderBrowseTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::Separator();
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 120) * 0.5f);
        if (ImGui::Button("Quit Doppio", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
            m_open = false;
        }

        renderErrorPopup();

        if (result) {
            ImGui::CloseCurrentPopup();
            m_open = false;
        }

        ImGui::EndPopup();
    }

    return result;
}

std::optional<ProjectConfig> ProjectStartupDialog::renderCreateTab() {
    ImGui::InputText("Project Name##CreateTab", m_projectName, sizeof(m_projectName));
    ImGui::SameLine();
    ImGui::HelpMarker("Name for your new project");

    const char* templates[] = {"Empty", "2D", "3D"};
    ImGui::Combo("Template##CreateTab", &m_templateIndex, templates, IM_ARRAYSIZE(templates));
    ImGui::SameLine();
    ImGui::HelpMarker("Project template (affects initial folder structure)");

    ImGui::Text("Location: %s", m_selectedLocation.c_str());
    if (ImGui::Button("Browse Location...##Create")) {
        m_locationPicked = true;
        // TODO: Implement native file picker or ImGui folder browser (future)
    }

    ImGui::Spacing();
    ImGui::Separator();

    if (ImGui::Button("Create & Open", ImVec2(150, 0))) {
        return tryCreateProject();
    }
    
    return std::nullopt;
}

std::optional<ProjectConfig> ProjectStartupDialog::renderRecentTab() {
    const auto& recents = m_projectManager.GetRecentProjects();
    
    if (recents.empty()) {
        ImGui::TextDisabled("No recent projects. Create a new one or browse.");
        return std::nullopt;
    }

    ImGui::BeginChild("RecentList", ImVec2(0, 300), true);
    for (const auto& recent : recents) {
        if (ImGui::Selectable(recent.string().c_str())) {
            return tryOpenProject(recent);
        }
    }
    ImGui::EndChild();

    return std::nullopt;
}

std::optional<ProjectConfig> ProjectStartupDialog::renderBrowseTab() {
    static char browsePath[512] = {0};
    ImGui::InputText("Search Path##Browse", browsePath, sizeof(browsePath));
    
    if (ImGui::Button("Browse Folder...##Browse")) {
        // TODO: Implement file picker (ImGui or native dialog)
        // For now, user can manually type path above
    }

    ImGui::TextDisabled("Tip: Type path above or use Browse button (coming soon)");
    ImGui::Text("Looking for project.caffeine files...");
    ImGui::BeginChild("BrowseList", ImVec2(0, 250), true);
    
    // Placeholder: would populate m_browseResults with matching projects
    ImGui::TextDisabled("(Folder browser not yet implemented)");
    
    ImGui::EndChild();

    return std::nullopt;
}

void ProjectStartupDialog::renderErrorPopup() {
    if (m_showError) {
        ImGui::OpenPopup("ProjectError");
        m_showError = false;
    }

    if (ImGui::BeginPopupModal("ProjectError", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", m_errorMessage);
        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

#else
// Non-ImGui fallback: render() returns nullopt (dialog unavailable without ImGui)
std::optional<ProjectConfig> ProjectStartupDialog::render() {
    return std::nullopt;
}
#endif

} // namespace Caffeine::Editor
