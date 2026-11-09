#include "editor/ProjectStartupDialog.hpp"
#include "editor/EditorIcons.hpp"
#include <algorithm>
#include <cstring>
#include <filesystem>

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#include <SDL3/SDL.h>
#endif

namespace Caffeine::Editor {

// ============================================================================
// Public API (always available)
// ============================================================================

ProjectStartupDialog::ProjectStartupDialog() = default;

void ProjectStartupDialog::init() {
    if (m_selectedLocation.empty()) {
#ifdef _WIN32
        const char* userProfile = std::getenv("USERPROFILE");
        if (userProfile) {
            m_selectedLocation = (std::filesystem::path(userProfile) / "Documents" / "CaffeineProjects").string();
        }
#else
        const char* home = std::getenv("HOME");
        if (home) {
            m_selectedLocation = (std::filesystem::path(home) / "Documents" / "CaffeineProjects").string();
        }
#endif
        if (m_selectedLocation.empty()) {
            m_selectedLocation = (std::filesystem::current_path() / "CaffeineProjects").string();
        }
    }

    if (m_browsePathBuf[0] == '\0') {
        std::strncpy(m_browsePathBuf, m_selectedLocation.c_str(), sizeof(m_browsePathBuf) - 1);
    }
}

std::optional<ProjectConfig> ProjectStartupDialog::render() {
#ifdef CF_HAS_IMGUI
    if (!m_open) return std::nullopt;
    
    updateToasts();

    m_recentProjects = m_projectManager.GetRecentProjects();
    
    std::optional<ProjectConfig> result;
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(620, 520), ImGuiCond_Appearing);
    
    if (ImGui::Begin("Project Manager", &m_open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize)) {
        if (EditorIcons::hasBrandLogo()) {
            EditorIcons::brandLogo(52.0f);
            ImGui::SameLine();
        }
        ImGui::BeginGroup();
        ImGui::TextUnformatted("Doppio");
        ImGui::TextDisabled("Caffeine Studio IDE — Select or create a project");
        ImGui::EndGroup();
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
    
    if (result.has_value()) {
        m_open = false;
    }
    
    return result;
#else
    return std::nullopt;
#endif
}

// ============================================================================
// ImGui-dependent implementation
// ============================================================================

#ifdef CF_HAS_IMGUI

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
    const char* templates[] = {"Empty", "2D", "3D"};
    const char* descriptions[] = {
        "Blank project, no starter assets",
        "Pre-configured for 2D games",
        "Pre-configured for 3D games"
    };
    const char* templateIcons[] = {"align-justify", "arrows-horizontal", "arrows-diagonal"};

    const float cardH = 132.0f;
    if (ImGui::BeginTable("project_templates", 3, ImGuiTableFlags_SizingStretchSame)) {
        for (int i = 0; i < 3; ++i) {
            ImGui::TableNextColumn();
            ImGui::PushID(i);
            const bool selected = (m_templateIndex == i);

            if (ImGui::InvisibleButton("##template_card", ImVec2(-1.0f, cardH))) {
                m_templateIndex = i;
            }

            const ImVec2 cardMin = ImGui::GetItemRectMin();
            const ImVec2 cardMax = ImGui::GetItemRectMax();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImU32 borderCol = selected ? IM_COL32(220, 170, 90, 255) : IM_COL32(90, 90, 100, 180);
            const ImU32 fillCol = selected ? IM_COL32(48, 42, 36, 255) : IM_COL32(28, 28, 34, 255);
            dl->AddRectFilled(cardMin, cardMax, fillCol, 8.0f);
            dl->AddRect(cardMin, cardMax, borderCol, 8.0f, 0, selected ? 2.0f : 1.0f);

            const float iconSize = 36.0f;
            const float cardW = cardMax.x - cardMin.x;
            const ImVec2 iconPos(cardMin.x + (cardW - iconSize) * 0.5f, cardMin.y + 18.0f);
            if (EditorIcons::hasIcon(templateIcons[i])) {
                const ImTextureRef tex = EditorIcons::get(templateIcons[i]);
                dl->AddImage(tex, iconPos, ImVec2(iconPos.x + iconSize, iconPos.y + iconSize));
            }

            dl->AddText(ImVec2(cardMin.x + 10.0f, cardMin.y + 68.0f), IM_COL32(230, 225, 220, 255),
                        templates[i]);
            dl->AddText(ImVec2(cardMin.x + 10.0f, cardMin.y + 88.0f), IM_COL32(150, 150, 160, 255),
                        descriptions[i]);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Separator();

    ImGui::Text("Location: %s", m_selectedLocation.c_str());
    if (EditorIcons::hasIcon(EditorIcon::Folder)) {
        EditorIcons::image(EditorIcon::Folder, ImGui::GetFontSize());
        ImGui::SameLine();
    }
    if (ImGui::Button("Browse Location...##Create", ImVec2(150, 0))) {
        m_showLocationPicker = true;
    }

    if (m_showLocationPicker) {
        auto path = FilePicker::pickPath(FilePicker::Mode::PickFolder, "Select Project Location", m_selectedLocation);
        if (path.has_value()) {
            m_selectedLocation = path.value().string();
            m_showLocationPicker = false;
            showToast("Location selected!", ToastType::Success);
        } else if (FilePicker::consumeCloseEvent("Select Project Location")) {
            m_showLocationPicker = false;
        }
    }

    ImGui::Spacing();
    ImGui::Separator();

    bool canCreate = (m_projectName[0] != '\0');
    if (!canCreate) ImGui::BeginDisabled();
    
    if (EditorIcons::hasIcon(EditorIcon::NewScene)) {
        EditorIcons::image(EditorIcon::NewScene, ImGui::GetFontSize());
        ImGui::SameLine();
    }
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

    struct RecentEntry {
        std::string name;
        std::string folder;
        std::filesystem::path file;
    };

    auto buildRecentEntry = [&](const std::filesystem::path& projectPath) -> RecentEntry {
        RecentEntry entry;
        entry.file = projectPath;
        entry.folder = projectPath.parent_path().string();

        ProjectConfig cfg;
        ProjectManager loader;
        if (loader.TryLoadProject(projectPath, cfg) && !cfg.Name.empty()) {
            entry.name = cfg.Name;
        } else if (projectPath.filename() == "project.caffeine" && projectPath.has_parent_path()) {
            entry.name = projectPath.parent_path().filename().string();
        } else {
            entry.name = projectPath.stem().string();
            if (entry.name.empty()) {
                entry.name = projectPath.filename().string();
            }
        }
        return entry;
    };

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
                 const RecentEntry entry = buildRecentEntry(m_recentProjects[i]);

                 if (strlen(m_searchFilter) > 0) {
                     if (entry.name.find(m_searchFilter) == std::string::npos &&
                         entry.folder.find(m_searchFilter) == std::string::npos) {
                         continue;
                     }
                 }

                 ImGui::PushID((int)i);

                 bool selected = (m_selectedRecentIndex == (int)i);
                 const float openButtonWidth = 70.0f;
                 const float spacing = ImGui::GetStyle().ItemSpacing.x;
                 float rowWidth = ImGui::GetContentRegionAvail().x - openButtonWidth - spacing;
                 if (rowWidth < 1.0f) rowWidth = 1.0f;

                 ImGui::BeginGroup();
                 if (ImGui::Selectable("##recent_row", selected,
                                       ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_SpanAllColumns,
                                       ImVec2(rowWidth, 42.0f))) {
                     m_selectedRecentIndex = (int)i;
                 }

                 ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 38.0f);
                 ImGui::Indent(8.0f);
                 ImGui::TextUnformatted(entry.name.c_str());
                 ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                 ImGui::TextWrapped("%s", entry.folder.c_str());
                 ImGui::PopStyleColor();
                 ImGui::Unindent(8.0f);
                 ImGui::EndGroup();

                 ImGui::SameLine();
                 if (ImGui::Button("Open", ImVec2(openButtonWidth, 42.0f))) {
                     result = tryOpenProject(entry.file);
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

void ProjectStartupDialog::scanBrowseDirectory(const std::filesystem::path& root) {
    m_browseResults.clear();
    m_selectedBrowseIndex = -1;

    std::error_code ec;
    if (root.empty() || !std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec)) {
        return;
    }

    const auto addProject = [&](const std::filesystem::path& projectFile) {
        const auto absPath = std::filesystem::weakly_canonical(projectFile, ec);
        const std::filesystem::path resolved = ec ? projectFile : absPath;
        if (std::find(m_browseResults.begin(), m_browseResults.end(), resolved) == m_browseResults.end()) {
            m_browseResults.push_back(resolved);
        }
    };

    if (std::filesystem::exists(root / "project.caffeine", ec)) {
        addProject(root / "project.caffeine");
    }

    try {
        auto it = std::filesystem::recursive_directory_iterator(
            root, std::filesystem::directory_options::skip_permission_denied, ec);
        const auto end = std::filesystem::recursive_directory_iterator();
        for (; it != end; ++it) {
            if (it.depth() > 6) {
                it.disable_recursion_pending();
                continue;
            }
            if (it->path().filename() == "project.caffeine") {
                addProject(it->path());
            }
        }
    } catch (const std::exception&) {
    }

    std::sort(m_browseResults.begin(), m_browseResults.end());
}

std::optional<ProjectConfig> ProjectStartupDialog::renderBrowseTab() {
    std::optional<ProjectConfig> result;

    auto projectDisplayName = [](const std::filesystem::path& projectPath) {
        if (projectPath.filename() == "project.caffeine" && projectPath.has_parent_path()) {
            return projectPath.parent_path().filename().string();
        }

        std::string name = projectPath.stem().string();
        if (name.empty()) {
            name = projectPath.filename().string();
        }
        return name;
    };

    bool scanRequested = false;
    ImGui::SetNextItemWidth(-220.0f);
    if (ImGui::InputTextWithHint("##browse_path", "Enter directory path and press Enter...",
                                 m_browsePathBuf, sizeof(m_browsePathBuf),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
        scanRequested = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Scan", ImVec2(56, 0))) {
        scanRequested = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Browse Folder...", ImVec2(140, 0))) {
        m_showBrowsePicker = true;
    }

    if (scanRequested) {
        scanBrowseDirectory(std::filesystem::path(m_browsePathBuf));
        showToast(m_browseResults.empty() ? "No projects found in folder"
                                          : "Found " + std::to_string(m_browseResults.size()) + " project(s)",
                  m_browseResults.empty() ? ToastType::Info : ToastType::Success);
    }

    if (m_showBrowsePicker) {
        const std::filesystem::path browsePathFs =
            m_browsePathBuf[0] != '\0' ? std::filesystem::path(m_browsePathBuf)
                                        : std::filesystem::path(m_selectedLocation);
        if (auto path = FilePicker::pickPath(FilePicker::Mode::PickFolder, "Select Folder to Browse", browsePathFs)) {
            std::strncpy(m_browsePathBuf, path->string().c_str(), sizeof(m_browsePathBuf) - 1);
            m_showBrowsePicker = false;
            scanBrowseDirectory(*path);
            showToast("Found " + std::to_string(m_browseResults.size()) + " project(s)", ToastType::Success);
        } else if (FilePicker::consumeCloseEvent("Select Folder to Browse")) {
            m_showBrowsePicker = false;
        }
    }

    ImGui::Spacing();
    ImGui::Separator();

    if (ImGui::BeginChild("browse_list", ImVec2(0, 300), true)) {
        if (m_browseResults.empty()) {
            ImGui::TextDisabled("Choose a folder and press Scan to find project.caffeine files.");
        } else {
            ImGui::Text("Found %zu project(s):", m_browseResults.size());
            ImGui::Separator();

            for (size_t i = 0; i < m_browseResults.size(); ++i) {
                const auto& projPath = m_browseResults[i];
                std::string projName = projectDisplayName(projPath);

                ImGui::PushID(static_cast<int>(i));

                const bool selected = (m_selectedBrowseIndex == static_cast<int>(i));
                const float openButtonWidth = 70.0f;
                const float spacing = ImGui::GetStyle().ItemSpacing.x;
                float rowWidth = ImGui::GetContentRegionAvail().x - openButtonWidth - spacing;
                if (rowWidth < 1.0f) rowWidth = 1.0f;

                ImGui::BeginGroup();
                if (ImGui::Selectable("##browse_row", selected,
                                      ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_SpanAllColumns,
                                      ImVec2(rowWidth, 42.0f))) {
                    m_selectedBrowseIndex = static_cast<int>(i);
                }
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 38.0f);
                ImGui::Indent(8.0f);
                if (EditorIcons::hasIcon("beer")) {
                    EditorIcons::image("beer", ImGui::GetFontSize());
                    ImGui::SameLine();
                }
                ImGui::TextUnformatted(projName.c_str());
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::TextWrapped("%s", projPath.parent_path().string().c_str());
                ImGui::PopStyleColor();
                ImGui::Unindent(8.0f);
                ImGui::EndGroup();

                ImGui::SameLine();
                if (ImGui::Button("Open", ImVec2(openButtonWidth, 42.0f))) {
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
