#include "editor/ProjectStartupDialog.hpp"
#include "editor/EditorIcons.hpp"
#include "caffeine/ui/UI.hpp"

#include <cstdio>
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

#ifdef CF_HAS_IMGUI

std::optional<ProjectConfig> ProjectStartupDialog::render() {
    if (!m_open) return std::nullopt;

    updateToasts();
    m_recentProjects = m_projectManager.GetRecentProjects();

    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;

    Panel root("Project Manager",
               "Caffeine Studio IDE - Select or create a project",
               "arrow-open-down", &m_open);
    root.setInitialSize(ImVec2(620.0f, 580.0f));
    root.setInitialPosition(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    root.setWindowFlags(ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);
    UIContext ui;

    auto& tabs = root.emplace<TabBar>("startup_tabs");
    auto& createPage = tabs.emplace<TabPage>("Create New");
    buildCreateTab(createPage);
    auto& recentPage = tabs.emplace<TabPage>("Open Recent");
    buildRecentTab(recentPage);
    auto& browsePage = tabs.emplace<TabPage>("Browse Projects");
    buildBrowseTab(browsePage);

    buildErrorBanner(root);

    ui.draw(root);

    // File pickers draw their own dialog windows, so run them after the panel.
    if (m_showLocationPicker) {
        auto path = FilePicker::pickPath(FilePicker::Mode::PickFolder,
                                         "Select Project Location", m_selectedLocation);
        if (path.has_value()) {
            m_selectedLocation = path->string();
            m_showLocationPicker = false;
            showToast("Location selected!", ToastType::Success);
        } else if (FilePicker::consumeCloseEvent("Select Project Location")) {
            m_showLocationPicker = false;
        }
    }
    if (m_showBrowsePicker) {
        const std::filesystem::path browsePathFs =
            m_browsePathBuf[0] != '\0' ? std::filesystem::path(m_browsePathBuf)
                                       : std::filesystem::path(m_selectedLocation);
        if (auto path = FilePicker::pickPath(FilePicker::Mode::PickFolder,
                                             "Select Folder to Browse", browsePathFs)) {
            std::strncpy(m_browsePathBuf, path->string().c_str(),
                         sizeof(m_browsePathBuf) - 1);
            m_browsePathBuf[sizeof(m_browsePathBuf) - 1] = '\0';
            m_showBrowsePicker = false;
            scanBrowseDirectory(*path);
            showToast("Found " + std::to_string(m_browseResults.size()) + " project(s)",
                      ToastType::Success);
        } else if (FilePicker::consumeCloseEvent("Select Folder to Browse")) {
            m_showBrowsePicker = false;
        }
    }

    renderToasts();

    std::optional<ProjectConfig> result;
    if (m_pendingResult.has_value()) {
        result = m_pendingResult;
        m_pendingResult.reset();
        m_open = false;
    }
    return result;
}

namespace {

std::string projectDisplayName(const std::filesystem::path& projectPath) {
    if (projectPath.filename() == "project.caffeine" && projectPath.has_parent_path()) {
        return projectPath.parent_path().filename().string();
    }
    std::string name = projectPath.stem().string();
    if (name.empty()) name = projectPath.filename().string();
    return name;
}

}  // namespace

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

std::optional<ProjectConfig> ProjectStartupDialog::tryOpenProject(
    const std::filesystem::path& path) {
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
    ImVec2 startPos(io.DisplaySize.x - toastWidth - padding,
                    io.DisplaySize.y - totalHeight - padding);

    for (size_t i = 0; i < m_toastQueue.size(); ++i) {
        const Toast& toast = m_toastQueue[i];
        ImVec2 pos(startPos.x, startPos.y + i * (toastHeight + spacing));

        ImGui::SetNextWindowPos(pos);
        ImGui::SetNextWindowSize(ImVec2(toastWidth, toastHeight));

        ImVec4 bgColor;
        switch (toast.type) {
            case ToastType::Success: bgColor = ImVec4(0.2f, 0.7f, 0.2f, 0.8f); break;
            case ToastType::Error:   bgColor = ImVec4(0.9f, 0.2f, 0.2f, 0.8f); break;
            case ToastType::Info:    bgColor = ImVec4(1.0f, 1.0f, 0.2f, 0.8f); break;
        }

        ImGui::PushStyleColor(ImGuiCol_WindowBg, bgColor);
        char label[64];
        std::snprintf(label, sizeof(label), "##toast_%zu", i);
        if (ImGui::Begin(label, nullptr,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("%s", toast.message.c_str());
            ImGui::End();
        }
        ImGui::PopStyleColor();
    }
}

void ProjectStartupDialog::buildCreateTab(Caffeine::UIKit::Elements::Container& page) {
    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;

    auto& card = page.emplace<Card>("startup_create_card");
    card.emplace<Label>("New project", TextTone::Accent).setHeading(true);

    auto& form = card.emplace<PropertyGrid>("startup_create_form");
    form.typedRow<BufferProperty>("Project Name", m_projectName, sizeof(m_projectName));
    if (m_projectName[0] == '\0') {
        card.emplace<Label>("Enter a project name.", TextTone::Muted);
    }

    card.emplace<Separator>();
    card.emplace<Label>("Template", TextTone::Accent);
    static const char* kTemplates[3] = {"Empty", "2D", "3D"};
    static const char* kDescriptions[3] = {
        "Blank project, no starter assets",
        "Pre-configured for 2D games",
        "Pre-configured for 3D games"};
    auto& templateRow = card.emplace<Row>("startup_templates");
    for (int i = 0; i < 3; ++i) {
        auto& button = templateRow.emplace<Button>(kTemplates[i], ButtonVariant::Segmented);
        button.setGrow(true);
        button.setHeight(44.0f);
        button.setSelected(m_templateIndex == i);
        button.setId(std::string("startup_template_") + std::to_string(i));
        button.setOnClick([this, i] { m_templateIndex = i; });
    }
    card.emplace<Label>(kDescriptions[m_templateIndex], TextTone::Muted);

    card.emplace<Separator>();
    card.emplace<Label>("Location", TextTone::Accent);
    card.emplace<Label>(m_selectedLocation, TextTone::Muted).setWrap(true);
    card.emplace<Button>("Browse Location...")
        .setOnClick([this] { m_showLocationPicker = true; });

    card.emplace<Separator>();
    auto& create = card.emplace<Button>("Create & Open", ButtonVariant::Primary);
    create.setEnabled(m_projectName[0] != '\0');
    create.setOnClick([this] {
        auto config = tryCreateProject();
        if (config) {
            showToast("Project created successfully!", ToastType::Success);
            requestCloseWith(*config);
        } else {
            showToast("Failed to create project", ToastType::Error);
        }
    });
}

void ProjectStartupDialog::buildRecentTab(Caffeine::UIKit::Elements::Container& page) {
    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;

    struct RecentEntry {
        std::string name;
        std::string folder;
        std::filesystem::path file;
    };

    auto buildRecentEntry = [](const std::filesystem::path& projectPath) -> RecentEntry {
        RecentEntry entry;
        entry.file = projectPath;
        entry.folder = projectPath.parent_path().string();

        ProjectConfig cfg;
        ProjectManager loader;
        if (loader.TryLoadProject(projectPath, cfg) && !cfg.Name.empty()) {
            entry.name = cfg.Name;
        } else {
            entry.name = projectDisplayName(projectPath);
        }
        return entry;
    };

    auto& card = page.emplace<Card>("startup_recent_card");
    card.emplace<Label>("Recent projects", TextTone::Accent).setHeading(true);

    auto& searchRow = card.emplace<Row>("startup_recent_search");
    auto& search = searchRow.emplace<Caffeine::UIKit::Elements::SearchField>(
        m_searchFilter, sizeof(m_searchFilter), "Search projects...");
    search.setGrow(true);
    auto& showAll = searchRow.emplace<Checkbox>("Show All", &m_showAllRecents);
    showAll.setWidth(84.0f);

    auto& list = card.emplace<ListBox>("startup_recent_list");
    list.setHeight(300.0f);

    const std::string filter = m_searchFilter;
    bool selectionValid = false;
    int shown = 0;
    if (m_recentProjects.empty()) {
        list.emplace<Label>("No projects yet. Create one in 'Create New' tab!",
                            TextTone::Muted);
    } else {
        for (size_t i = 0; i < m_recentProjects.size(); ++i) {
            const RecentEntry entry = buildRecentEntry(m_recentProjects[i]);
            if (!filter.empty() && entry.name.find(filter) == std::string::npos &&
                entry.folder.find(filter) == std::string::npos) {
                continue;
            }
            if (!m_showAllRecents && shown >= 8) break;
            ++shown;

            const int index = static_cast<int>(i);
            const bool selected = (m_selectedRecentIndex == index);
            if (selected) selectionValid = true;
            auto& item = list.emplace<ListItem>(entry.name, selected);
            item.setId("startup_recent_" + std::to_string(i));
            item.setTooltip(entry.folder);
            item.setOnClick([this, index] { m_selectedRecentIndex = index; });
            item.setOnDoubleClick([this, index] { openRecentProject(index); });
        }
        if (shown == 0) {
            list.emplace<Label>("No project matches the filter.", TextTone::Muted);
        }
    }
    if (!selectionValid) m_selectedRecentIndex = -1;

    const bool canOpen = selectionValid && m_selectedRecentIndex >= 0 &&
                         m_selectedRecentIndex < static_cast<int>(m_recentProjects.size());
    card.emplace<Label>(
        canOpen ? m_recentProjects[static_cast<size_t>(m_selectedRecentIndex)].string()
                : std::string("Select a project to open."),
        TextTone::Muted)
        .setWrap(true);

    auto& open = card.emplace<Button>("Open Selected", ButtonVariant::Primary);
    open.setEnabled(canOpen);
    open.setOnClick([this] { openRecentProject(m_selectedRecentIndex); });
}

void ProjectStartupDialog::buildBrowseTab(Caffeine::UIKit::Elements::Container& page) {
    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;

    auto& card = page.emplace<Card>("startup_browse_card");
    card.emplace<Label>("Scan a folder for project.caffeine files", TextTone::Accent)
        .setHeading(true);

    auto& pathRow = card.emplace<Row>("startup_browse_path");
    auto& path = pathRow.emplace<TextInput>(m_browsePathBuf, sizeof(m_browsePathBuf));
    path.setHint("Enter directory path...");
    path.setGrow(true);
    auto& scan = pathRow.emplace<Button>("Scan");
    scan.setWidth(60.0f);
    scan.setOnClick([this] {
        scanBrowseDirectory(std::filesystem::path(m_browsePathBuf));
        showToast(m_browseResults.empty()
                      ? "No projects found in folder"
                      : "Found " + std::to_string(m_browseResults.size()) + " project(s)",
                  m_browseResults.empty() ? ToastType::Info : ToastType::Success);
    });
    auto& browseFolder = pathRow.emplace<Button>("Browse Folder...");
    browseFolder.setWidth(140.0f);
    browseFolder.setOnClick([this] { m_showBrowsePicker = true; });

    auto& list = card.emplace<ListBox>("startup_browse_list");
    list.setHeight(300.0f);

    bool selectionValid = false;
    if (m_browseResults.empty()) {
        list.emplace<Label>("Choose a folder and press Scan to find project.caffeine files.",
                            TextTone::Muted);
    } else {
        for (size_t i = 0; i < m_browseResults.size(); ++i) {
            const auto& projectPath = m_browseResults[i];
            const int index = static_cast<int>(i);
            const bool selected = (m_selectedBrowseIndex == index);
            if (selected) selectionValid = true;
            auto& item = list.emplace<ListItem>(projectDisplayName(projectPath), selected);
            item.setId("startup_browse_" + std::to_string(i));
            item.setTooltip(projectPath.parent_path().string());
            item.setOnClick([this, index] { m_selectedBrowseIndex = index; });
            item.setOnDoubleClick([this, index] { openBrowseProject(index); });
        }
    }
    if (!selectionValid) m_selectedBrowseIndex = -1;

    const bool canOpen = selectionValid && m_selectedBrowseIndex >= 0 &&
                         m_selectedBrowseIndex < static_cast<int>(m_browseResults.size());
    card.emplace<Label>(
        canOpen ? m_browseResults[static_cast<size_t>(m_selectedBrowseIndex)].string()
                : std::string("Select a project to open."),
        TextTone::Muted)
        .setWrap(true);

    auto& open = card.emplace<Button>("Open Selected", ButtonVariant::Primary);
    open.setEnabled(canOpen);
    open.setOnClick([this] { openBrowseProject(m_selectedBrowseIndex); });
}

void ProjectStartupDialog::buildErrorBanner(
    Caffeine::UIKit::Elements::Container& parent) {
    if (!m_showError) return;
    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;

    auto& card = parent.emplace<Card>("startup_error");
    card.emplace<Label>(m_errorMessage, TextTone::Danger).setWrap(true);
    card.emplace<Button>("Dismiss").setOnClick([this] { m_showError = false; });
}

void ProjectStartupDialog::openRecentProject(int index) {
    if (index < 0 || index >= static_cast<int>(m_recentProjects.size())) return;
    auto config = tryOpenProject(m_recentProjects[static_cast<size_t>(index)]);
    if (config) {
        showToast("Project opened!", ToastType::Success);
        requestCloseWith(*config);
    } else {
        showToast("Failed to open project", ToastType::Error);
    }
}

void ProjectStartupDialog::openBrowseProject(int index) {
    if (index < 0 || index >= static_cast<int>(m_browseResults.size())) return;
    auto config = tryOpenProject(m_browseResults[static_cast<size_t>(index)]);
    if (config) {
        showToast("Project opened!", ToastType::Success);
        requestCloseWith(*config);
    } else {
        showToast("Failed to open project", ToastType::Error);
    }
}

void ProjectStartupDialog::scanBrowseDirectory(const std::filesystem::path& root) {
    m_browseResults.clear();
    m_selectedBrowseIndex = -1;

    std::error_code ec;
    if (root.empty() || !std::filesystem::exists(root, ec) ||
        !std::filesystem::is_directory(root, ec)) {
        return;
    }

    const auto addProject = [&](const std::filesystem::path& projectFile) {
        const auto absPath = std::filesystem::weakly_canonical(projectFile, ec);
        const std::filesystem::path resolved = ec ? projectFile : absPath;
        if (std::find(m_browseResults.begin(), m_browseResults.end(), resolved) ==
            m_browseResults.end()) {
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

#else
std::optional<ProjectConfig> ProjectStartupDialog::render() {
    return std::nullopt;
}
#endif

} // namespace Caffeine::Editor
