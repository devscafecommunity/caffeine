#include "BuildDialog.hpp"
#include "BuildSystem.hpp"
#include "BuildLog.hpp"
#include "EditorIcons.hpp"
#include "ProjectManager.hpp"

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

    if (ImGui::Begin("Build & Run", &m_open)) {
        if (EditorIcons::hasIcon(EditorIcon::Build)) {
            EditorIcons::image(EditorIcon::Build, 18.0f);
            ImGui::SameLine();
        }
        ImGui::TextUnformatted("Package project into a playable build");

        renderConfigSection();
        renderAdvancedSection();
        renderProgressSection();
        renderLogSection();

        ImGui::Separator();
        const bool building = BuildSystem::IsBuilding();
        if (building) ImGui::BeginDisabled();
        if (ImGui::Button("Build & Run", ImVec2(140, 0))) {
            m_settings.runAfterBuild = true;
            onBuildClicked();
        }
        ImGui::SameLine();
        if (ImGui::Button("Build Only", ImVec2(120, 0))) {
            m_settings.runAfterBuild = false;
            onBuildClicked();
        }
        if (building) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) {
            onCancelClicked();
        }
    }
    ImGui::End();
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
        BuildLog::error("No startup scene — save your scene (Ctrl+S) before building");
        return;
    }

    BuildSystem::ExecuteBuild(m_settings);
}

void BuildDialog::onCancelClicked() {
    BuildSystem::CancelBuild();
}

void BuildDialog::renderConfigSection() {
    ImGui::Separator();
    ImGui::Text("Configuration");

    ImGui::InputText("Project Name", m_projectNameBuf, sizeof(m_projectNameBuf));
    ImGui::BeginDisabled();
    ImGui::InputText("Output Directory", m_outputPathBuf, sizeof(m_outputPathBuf));
    ImGui::EndDisabled();

    int platformIdx = static_cast<int>(m_settings.platform);
    const char* platforms[] = {"Windows_x64", "Linux_x64"};
    if (ImGui::Combo("Platform", &platformIdx, platforms, 2)) {
        m_settings.platform = static_cast<BuildPlatform>(platformIdx);
    }

    ImGui::Checkbox("Debug Build", &m_settings.isDebug);
    ImGui::Checkbox("Incremental Build", &m_settings.incrementalBuild);

    if (!m_settings.startupScene.empty()) {
        ImGui::TextDisabled("Startup scene: %s", m_settings.startupScene.c_str());
    }
}

void BuildDialog::renderAdvancedSection() {
    ImGui::Separator();
    ImGui::Text("Advanced");

    ImGui::InputText("Executable Name", m_executableNameBuf, sizeof(m_executableNameBuf));
    ImGui::InputText("Icon Path", m_iconPathBuf, sizeof(m_iconPathBuf));
    ImGui::InputText("Version", m_versionBuf, sizeof(m_versionBuf));
}

void BuildDialog::renderProgressSection() {
    const auto status = m_progress->status.load(std::memory_order_relaxed);
    if (status == BuildStatus::Idle && m_progress->progress.load(std::memory_order_relaxed) <= 0.0f) {
        return;
    }

    ImGui::Separator();
    ImGui::Text("Build Progress");

    const float progress = m_progress->progress.load(std::memory_order_relaxed);
    ImGui::ProgressBar(progress, ImVec2(-1, 0));

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

    ImGui::Text("Status: %s", statusText);
    ImGui::TextWrapped("Task: %s", BuildLog::task().c_str());
}

void BuildDialog::renderLogSection() {
    ImGui::Separator();
    ImGui::Text("Build Log");
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy Log")) {
        ImGui::SetClipboardText(BuildLog::joinedText().c_str());
    }

    ImGui::BeginChild("BuildLog", ImVec2(0, 180), true);
    for (const auto& line : BuildLog::snapshot()) {
        ImVec4 col(0.88f, 0.86f, 0.84f, 1.0f);
        if (line.find("[Error]") != std::string::npos) col = ImVec4(1.0f, 0.45f, 0.35f, 1.0f);
        else if (line.find("[Runtime]") != std::string::npos) col = ImVec4(1.0f, 0.55f, 0.75f, 1.0f);
        else if (line.find("[Warn]") != std::string::npos) col = ImVec4(1.0f, 0.8f, 0.35f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::TextUnformatted(line.c_str());
        ImGui::PopStyleColor();
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
}

#endif

} // namespace Caffeine::Editor
