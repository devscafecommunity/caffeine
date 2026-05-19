#include "BuildDialog.hpp"
#include "BuildSystem.hpp"
#include <iostream>
#include <cstring>

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

namespace Caffeine::Editor {

BuildDialog::BuildDialog() {
    m_progress = BuildSystem::GetProgress();
    m_settings.projectName = "MyGame";
    m_settings.outputDir = "./build";
    m_settings.platform = BuildPlatform::Windows_x64;
    m_settings.isDebug = false;
    m_settings.incrementalBuild = true;
    m_settings.runAfterBuild = false;
    m_settings.executableName = "game.exe";
    m_settings.icon = "";
    m_settings.version = "1.0.0";

    std::strcpy(m_projectNameBuf, "MyGame");
    std::strcpy(m_outputPathBuf, "./build");
    std::strcpy(m_executableNameBuf, "game.exe");
    std::strcpy(m_iconPathBuf, "");
    std::strcpy(m_versionBuf, "1.0.0");
}

#ifdef CF_HAS_IMGUI
void BuildDialog::render() {
    if (!m_open) return;

    if (ImGui::Begin("Build & Run", &m_open)) {
        renderConfigSection();
        renderAdvancedSection();
        renderProgressSection();
        renderLogSection();

        ImGui::Separator();
        if (ImGui::Button("Build & Run", ImVec2(120, 0))) {
            onBuildClicked();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            onCancelClicked();
        }
    }
    ImGui::End();
}

void BuildDialog::onBuildClicked() {
    if (m_settings.projectName.empty()) {
        std::cout << "Error: Project name is required\n";
        return;
    }
    if (m_settings.outputDir.empty()) {
        std::cout << "Error: Output directory is required\n";
        return;
    }

    std::cout << "Starting build: " << m_settings.projectName << "\n";
    BuildSystem::ExecuteBuild(m_settings);
}

void BuildDialog::onCancelClicked() {
    std::cout << "Build cancelled by user\n";
    BuildSystem::CancelBuild();
}

void BuildDialog::renderConfigSection() {
    ImGui::Separator();
    ImGui::Text("Configuration");

    ImGui::InputText("Project Name", m_projectNameBuf, sizeof(m_projectNameBuf));
    m_settings.projectName = m_projectNameBuf;

    ImGui::InputText("Output Directory", m_outputPathBuf, sizeof(m_outputPathBuf));
    m_settings.outputDir = m_outputPathBuf;
    
    static int platformIdx = 0;
    const char* platforms[] = {"Windows_x64", "Linux_x64"};
    if (ImGui::Combo("Platform", &platformIdx, platforms, 2)) {
        m_settings.platform = (BuildPlatform)platformIdx;
    }

    ImGui::Checkbox("Debug Build", &m_settings.isDebug);
    ImGui::Checkbox("Incremental Build", &m_settings.incrementalBuild);
    ImGui::Checkbox("Run After Build", &m_settings.runAfterBuild);
}

void BuildDialog::renderAdvancedSection() {
    ImGui::Separator();
    ImGui::Text("Advanced");

    ImGui::InputText("Executable Name", m_executableNameBuf, sizeof(m_executableNameBuf));
    m_settings.executableName = m_executableNameBuf;

    ImGui::InputText("Icon Path", m_iconPathBuf, sizeof(m_iconPathBuf));
    m_settings.icon = m_iconPathBuf;

    ImGui::InputText("Version", m_versionBuf, sizeof(m_versionBuf));
    m_settings.version = m_versionBuf;
}

void BuildDialog::renderProgressSection() {
    if (!BuildSystem::IsBuilding()) return;

    ImGui::Separator();
    ImGui::Text("Build Progress");

    float progress = m_progress->progress.load(std::memory_order_relaxed);
    ImGui::ProgressBar(progress);

    auto status = m_progress->status.load(std::memory_order_relaxed);
    ImGui::Text("Status: %s", 
        status == BuildStatus::Idle ? "Idle" :
        status == BuildStatus::Validating ? "Validating" :
        status == BuildStatus::PreparingOutput ? "Preparing" :
        status == BuildStatus::CompilingScripts ? "Compiling Scripts" :
        status == BuildStatus::CookingAssets ? "Cooking Assets" :
        status == BuildStatus::LinkingExecutable ? "Linking" :
        status == BuildStatus::GeneratingProject ? "Generating" :
        status == BuildStatus::Success ? "Success" :
        status == BuildStatus::Failed ? "Failed" :
        status == BuildStatus::Cancelled ? "Cancelled" : "Unknown");

    ImGui::Text("Task: %s", m_progress->currentTask.c_str());
}

void BuildDialog::renderLogSection() {
    ImGui::Separator();
    ImGui::Text("Build Log");

    ImGui::BeginChild("BuildLog", ImVec2(0, 150));
    ImGui::Text("[Build logs will appear here]");
    ImGui::EndChild();
}

#endif

}
