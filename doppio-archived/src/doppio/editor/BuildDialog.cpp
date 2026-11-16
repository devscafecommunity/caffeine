#include "caffeine/ui/UI.hpp"
#include "BuildDialog.hpp"
#include "BuildSystem.hpp"
#include "BuildLog.hpp"
#include "EditorIcons.hpp"
#include "project/ProjectManager.hpp"

#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

namespace Caffeine::Editor {

BuildDialog::BuildDialog() {
    m_progress = BuildSystem::GetProgress();
    m_settings.projectName = "MyGame";
    m_settings.outputDir = "./build";
    m_settings.platform = BuildPlatform::Linux_x64;
    m_settings.isDebug = false;
    m_settings.incrementalBuild = true;
    m_settings.runAfterBuild = false;
    m_settings.executableName = "MyGame";
    m_settings.icon = "";
    m_settings.version = "1.0.0";

    std::strcpy(m_projectNameBuf, "MyGame");
    std::strcpy(m_outputPathBuf, "./build");
    std::strcpy(m_executableNameBuf, "MyGame");
    std::strcpy(m_iconPathBuf, "");
    std::strcpy(m_versionBuf, "1.0.0");
}

void BuildDialog::setProjectContext(const ProjectConfig& project, const std::string& currentScenePath) {
    if (!project.Name.empty()) {
        m_settings.projectName = project.Name;
        std::snprintf(m_projectNameBuf, sizeof(m_projectNameBuf), "%s", project.Name.c_str());
    }
    if (!project.Version.empty()) {
        m_settings.version = project.Version;
        std::snprintf(m_versionBuf, sizeof(m_versionBuf), "%s", project.Version.c_str());
    }

    m_settings.projectRoot = ProjectManager::ResolveEditorProjectRoot(project.RootPath);
    m_settings.assetsRawPath = project.AssetRawPath;
    m_settings.assetsProcessedPath = project.AssetProcessedPath;
    m_settings.scriptsPath = project.ScriptsPath;

    if (!project.RootPath.empty()) {
        std::error_code ec;
        const auto canonicalRoot = std::filesystem::weakly_canonical(project.RootPath, ec);
        const std::filesystem::path root = ec ? project.RootPath : canonicalRoot;
        const std::string out = (root / "build").string();
        m_settings.outputDir = out;
        std::snprintf(m_outputPathBuf, sizeof(m_outputPathBuf), "%s", out.c_str());
    }

    auto normalizeScenePath = [](std::string path) -> std::string {
        if (path.rfind("data/scenes/", 0) == 0) {
            return "scenes/" + std::filesystem::path(path).filename().string();
        }
        if (path.find("/build/") != std::string::npos || path.find("\\build\\") != std::string::npos) {
            return "scenes/" + std::filesystem::path(path).filename().string();
        }
        return path;
    };

    if (!currentScenePath.empty() && !project.RootPath.empty()) {
        std::error_code ec;
        const auto rel = std::filesystem::relative(currentScenePath, project.RootPath, ec);
        m_settings.startupScene = normalizeScenePath(
            (!ec && !rel.empty()) ? rel.string() : currentScenePath);
    } else if (!project.LastScene.empty()) {
        m_settings.startupScene = normalizeScenePath(project.LastScene);
    }

#ifndef _WIN32
    m_settings.platform = BuildPlatform::Linux_x64;
    m_settings.executableName = project.Name.empty() ? "game" : project.Name;
    for (char& c : m_settings.executableName) {
        if (c == ' ') c = '_';
    }
#else
    m_settings.platform = BuildPlatform::Windows_x64;
    m_settings.executableName = (project.Name.empty() ? "game" : project.Name) + ".exe";
#endif
    std::snprintf(m_executableNameBuf, sizeof(m_executableNameBuf), "%s", m_settings.executableName.c_str());

    m_scenesToInclude.clear();
    if (!m_settings.startupScene.empty()) {
        m_scenesToInclude.push_back(m_settings.startupScene);
    }
}

#ifdef CF_HAS_IMGUI
void BuildDialog::render() {
    if (!m_open) return;

    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;

    Panel root("Build & Run", "Package project into a playable build", "beer", &m_open);
    UIContext ui;

    root.emplace<Label>("Build configuration", TextTone::Accent).setHeading(true);
    renderConfigSection(root);
    renderAdvancedSection(root);
    renderProgressSection(root);
    renderLogSection(root);

    root.emplace<Separator>(true);
    auto& actions = root.emplace<Row>("build_actions");
    const bool building = BuildSystem::IsBuilding();
    auto& run = actions.emplace<Button>("Build & Run", ButtonVariant::Primary);
    run.setEnabled(!building);
    run.setOnClick([this] {
        m_settings.runAfterBuild = true;
        onBuildClicked();
    });
    auto& only = actions.emplace<Button>("Build Only");
    only.setEnabled(!building);
    only.setOnClick([this] {
        m_settings.runAfterBuild = false;
        onBuildClicked();
    });
    actions.emplace<Button>("Cancel").setOnClick([this] { onCancelClicked(); });

    ui.draw(root);
}

void BuildDialog::onBuildClicked() {
    m_settings.projectName = m_projectNameBuf;
    if (!m_settings.projectRoot.empty()) {
        std::error_code ec;
        const auto root = std::filesystem::weakly_canonical(m_settings.projectRoot, ec);
        const std::string out = ((ec ? m_settings.projectRoot : root) / "build").string();
        m_settings.outputDir = out;
        std::snprintf(m_outputPathBuf, sizeof(m_outputPathBuf), "%s", out.c_str());
    } else {
        m_settings.outputDir = m_outputPathBuf;
    }
    m_settings.executableName = m_executableNameBuf;
    m_settings.icon = m_iconPathBuf;
    m_settings.version = m_versionBuf;

    if (m_settings.projectRoot.empty()) {
        BuildLog::error("No project loaded");
        return;
    }

    if (m_prepareBuild) {
        const std::string scenePath = m_prepareBuild();
        if (scenePath.empty()) {
            BuildLog::error("Could not save the active scene before building");
            return;
        }
        m_settings.startupScene = scenePath;
        m_scenesToInclude = {scenePath};
        BuildLog::info("Startup scene: " + scenePath);
    }

    m_settings.scenesToInclude = m_scenesToInclude;

    if (m_settings.startupScene.empty()) {
        BuildLog::error("No startup scene - save your scene (Ctrl+S) before building");
        return;
    }

    BuildSystem::ExecuteBuild(m_settings);
}

void BuildDialog::onCancelClicked() {
    BuildSystem::CancelBuild();
}

void BuildDialog::renderConfigSection(
    Caffeine::UIKit::Elements::Container& parent) {
    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;

    auto& card = parent.emplace<Card>("build_config");
    card.emplace<Label>("Configuration", TextTone::Accent).setHeading(true);
    auto& grid = card.emplace<PropertyGrid>("build_config_grid");
    grid.typedRow<BufferProperty>("Project Name", m_projectNameBuf,
                                  sizeof(m_projectNameBuf));
    auto& output = grid.typedRow<BufferProperty>("Output Directory", m_outputPathBuf,
                                                 sizeof(m_outputPathBuf));
    output.setEnabled(false);
    m_platformIndex = static_cast<int>(m_settings.platform);
    grid.typedRow<ChoiceProperty>(
        "Platform", m_platformIndex,
        std::vector<std::string>{"Windows_x64", "Linux_x64"})
        .setOnChange([this](int value) {
            m_settings.platform = static_cast<BuildPlatform>(value);
        });
    grid.typedRow<BoolProperty>("Debug Build", m_settings.isDebug);
    grid.typedRow<BoolProperty>("Incremental Build", m_settings.incrementalBuild);
    if (!m_settings.startupScene.empty()) {
        card.emplace<Label>("Startup scene: " + m_settings.startupScene, TextTone::Muted);
    }
}

void BuildDialog::renderAdvancedSection(
    Caffeine::UIKit::Elements::Container& parent) {
    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;

    auto& card = parent.emplace<Card>("build_advanced");
    card.emplace<Label>("Advanced", TextTone::Accent).setHeading(true);
    auto& grid = card.emplace<PropertyGrid>("build_advanced_grid");
    grid.typedRow<BufferProperty>("Executable Name", m_executableNameBuf,
                                  sizeof(m_executableNameBuf));
    grid.typedRow<BufferProperty>("Icon Path", m_iconPathBuf, sizeof(m_iconPathBuf));
    grid.typedRow<BufferProperty>("Version", m_versionBuf, sizeof(m_versionBuf));
}

void BuildDialog::renderProgressSection(
    Caffeine::UIKit::Elements::Container& parent) {
    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;

    if (!m_progress) return;
    const auto status = m_progress->status.load(std::memory_order_relaxed);
    m_progressValue = m_progress->progress.load(std::memory_order_relaxed);
    if (status == BuildStatus::Idle && m_progressValue <= 0.0f) return;

    auto& card = parent.emplace<Card>("build_progress");
    card.emplace<Label>("Build Progress", TextTone::Accent).setHeading(true);
    card.emplace<ProgressBar>(&m_progressValue, "");

    const char* statusText =
        status == BuildStatus::Idle ? "Idle" :
        status == BuildStatus::Validating ? "Validating" :
        status == BuildStatus::PreparingOutput ? "Preparing" :
        status == BuildStatus::CompilingScripts ? "Validating Scripts" :
        status == BuildStatus::CookingAssets ? "Cooking Assets" :
        status == BuildStatus::LinkingExecutable ? "Packaging" :
        status == BuildStatus::GeneratingProject ? "Generating project.caffeine" :
        status == BuildStatus::Success ? "Success" :
        status == BuildStatus::Failed ? "Failed" :
        status == BuildStatus::Cancelled ? "Cancelled" : "Unknown";
    TextTone tone = TextTone::Normal;
    if (status == BuildStatus::Failed || status == BuildStatus::Cancelled) {
        tone = TextTone::Danger;
    } else if (status == BuildStatus::Success) {
        tone = TextTone::Success;
    }
    card.emplace<Label>("Status: " + std::string(statusText), tone);
    card.emplace<Label>("Task: " + BuildLog::task(), TextTone::Muted);
}

void BuildDialog::renderLogSection(Caffeine::UIKit::Elements::Container& parent) {
    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;

    auto& card = parent.emplace<Card>("build_log");
    card.emplace<Label>("Build Log", TextTone::Accent).setHeading(true);
    auto& toolbar = card.emplace<Row>("build_log_actions");
    toolbar.emplace<Button>("Copy Log").setOnClick([] {
        ImGui::SetClipboardText(BuildLog::joinedText().c_str());
    });

    auto& log = card.emplace<ScrollArea>("build_log_scroll");
    log.setHeight(180.0f);
    log.setScrollToBottom();
    for (const auto& line : BuildLog::snapshot()) {
        TextTone tone = TextTone::Normal;
        if (line.find("[Error]") != std::string::npos) tone = TextTone::Danger;
        else if (line.find("[Runtime]") != std::string::npos) tone = TextTone::Accent;
        else if (line.find("[Warn]") != std::string::npos) tone = TextTone::Warning;
        log.emplace<Label>(line, tone);
    }
}

#endif

} // namespace Caffeine::Editor
