#include "editor/ProjectStartupDialog.hpp"
#include <filesystem>
#include <cstring>

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#include <SDL3/SDL.h>
#endif

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
    m_popupOpened = false;
    m_recentProjects = m_projectManager.GetRecentProjects();
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

void ProjectStartupDialog::showToast(const std::string& message, ToastType type) {
    m_toastQueue.push_back({message, type, static_cast<double>(SDL_GetTicksNS()) / 1'000'000.0});
    if (m_toastQueue.size() > MAX_VISIBLE_TOASTS) {
        m_toastQueue.erase(m_toastQueue.begin());
    }
}

void ProjectStartupDialog::updateToasts() {
    double currentTime = static_cast<double>(SDL_GetTicksNS()) / 1'000'000.0;
    auto it = m_toastQueue.begin();
    while (it != m_toastQueue.end()) {
        if (it->isExpired(currentTime)) {
            it = m_toastQueue.erase(it);
        } else {
            ++it;
        }
    }
}

void ProjectStartupDialog::renderToasts() {
    if (m_toastQueue.empty()) return;

    ImGuiIO& io = ImGui::GetIO();
    float toastWidth = 300.0f;
    float toastHeight = 60.0f;
    float padding = 10.0f;
    float spacing = 5.0f;

    float totalHeight = m_toastQueue.size() * (toastHeight + spacing);
    ImVec2 startPos(
        io.DisplaySize.x - toastWidth - padding,
        io.DisplaySize.y - totalHeight - padding
    );

    for (size_t i = 0; i < m_toastQueue.size(); ++i) {
        const Toast& toast = m_toastQueue[i];
        ImVec2 pos(startPos.x, startPos.y + i * (toastHeight + spacing));

        ImGui::SetNextWindowPos(pos);
        ImGui::SetNextWindowSize(ImVec2(toastWidth, toastHeight));

        ImVec4 bgColor;
        switch (toast.type) {
            case ToastType::Success:
                bgColor = ImVec4(0.2f, 0.7f, 0.2f, 0.8f);
                break;
            case ToastType::Error:
                bgColor = ImVec4(0.9f, 0.2f, 0.2f, 0.8f);
                break;
            case ToastType::Info:
                bgColor = ImVec4(1.0f, 1.0f, 0.2f, 0.8f);
                break;
        }

        ImGui::PushStyleColor(ImGuiCol_WindowBg, bgColor);
        char label[64];
        snprintf(label, sizeof(label), "##toast_%zu", i);
        
        if (ImGui::Begin(label, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("%s", toast.message.c_str());
            ImGui::End();
        }
        ImGui::PopStyleColor();
    }
}

// ── UI Layer (requires CF_HAS_IMGUI) ──────────────────────────────────────

#ifdef CF_HAS_IMGUI

std::optional<ProjectConfig> ProjectStartupDialog::render() {
    if (!m_open) {
        return std::nullopt;
    }

    updateToasts();

    std::optional<ProjectConfig> result;
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    
    ImGui::SetNextWindowPos(center, ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_FirstUseEver);
    
    if (ImGui::Begin("Project Manager", &m_open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        ImGui::Text("Welcome to Doppio — Select or Create a Project");
        ImGui::Separator();

        if (ImGui::BeginTabBar("ProjectDialogTabs")) {
            
            if (ImGui::BeginTabItem("Create New")) {
                if (auto config = renderCreateTab()) {
                    result = config;
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Open Recent")) {
                if (auto config = renderRecentTab()) {
                    result = config;
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Browse Projects")) {
                if (auto config = renderBrowseTab()) {
                    result = config;
                }
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        renderErrorPopup();

        ImGui::End();
    }

    renderToasts();

    return result;
}

std::optional<ProjectConfig> ProjectStartupDialog::renderCreateTab() {
    std::optional<ProjectConfig> result;

    ImGui::Text("Project Name:");
    ImGui::InputText("##ProjectName", m_projectName, sizeof(m_projectName));
    
    if (m_projectName[0] == '\0') {
        ImGui::TextDisabled("(Enter a project name)");
    }

    ImGui::Spacing();
    ImGui::Separator();

    ImGui::Text("Template:");
    ImGui::BeginGroup();
    
    const char* templates[] = {"Empty", "2D", "3D"};
    const char* descriptions[] = {
        "Blank project, no starter assets",
        "Pre-configured for 2D games",
        "Pre-configured for 3D games"
    };
    
    for (int i = 0; i < 3; ++i) {
        bool selected = (m_templateIndex == i);
        ImVec4 borderColor = selected ? ImVec4(1.0f, 1.0f, 0.0f, 1.0f) : ImVec4(0.5f, 0.5f, 0.5f, 0.5f);
        
        ImGui::PushStyleColor(ImGuiCol_Border, borderColor);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
        
        if (ImGui::Selectable(templates[i], selected, ImGuiSelectableFlags_None, ImVec2(150, 80))) {
            m_templateIndex = i;
        }
        
        ImGui::SameLine();
        ImGui::TextWrapped("%s", descriptions[i]);
        
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

    ImGui::EndGroup();

    ImGui::Spacing();
    ImGui::Separator();

    ImGui::Text("Location: %s", m_selectedLocation.c_str());
    if (ImGui::Button("Browse Location...##Create", ImVec2(150, 0))) {
        m_locationPicked = true;
        showToast("File picker coming soon", ToastType::Info);
    }

    ImGui::Spacing();
    ImGui::Separator();

    bool canCreate = (m_projectName[0] != '\0');
    if (!canCreate) ImGui::BeginDisabled();
    
    if (ImGui::Button("Create & Open", ImVec2(150, 0))) {
        result = tryCreateProject();
        if (result) {
            showToast("Project created successfully!", ToastType::Success);
        } else {
            showToast("Failed to create project", ToastType::Error);
        }
    }
    
    if (!canCreate) ImGui::EndDisabled();

    return result;
}

std::optional<ProjectConfig> ProjectStartupDialog::renderRecentTab() {
    std::optional<ProjectConfig> result;

    ImGui::InputTextWithHint("##search_recent", "Search projects...", m_searchFilter, sizeof(m_searchFilter));
    ImGui::SameLine();
    ImGui::Checkbox("Show All##recent", &m_showAllRecents);

    ImGui::Spacing();
    ImGui::Separator();

    if (ImGui::BeginChild("recent_list", ImVec2(0, 300), true)) {
        if (m_recentProjects.empty()) {
            ImGui::TextDisabled("No projects yet. Create one in 'Create New' tab!");
        } else {
            for (size_t i = 0; i < m_recentProjects.size(); ++i) {
                const auto& projPath = m_recentProjects[i];
                std::string projName = projPath.filename().string();
                
                if (strlen(m_searchFilter) > 0) {
                    if (projName.find(m_searchFilter) == std::string::npos) {
                        continue;
                    }
                }

                bool selected = (m_selectedRecentIndex == (int)i);
                if (ImGui::Selectable(projName.c_str(), selected)) {
                    m_selectedRecentIndex = i;
                }

                ImGui::SameLine(ImGui::GetWindowWidth() - 80);
                ImGui::PushID((int)i);
                if (ImGui::Button("Open##recent", ImVec2(70, 0))) {
                    result = tryOpenProject(projPath);
                    if (result) {
                        showToast("Project opened!", ToastType::Success);
                    } else {
                        showToast("Failed to open project", ToastType::Error);
                    }
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
    }

    return result;
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
std::optional<ProjectConfig> ProjectStartupDialog::render() {
    return std::nullopt;
}
#endif

} // namespace Caffeine::Editor
