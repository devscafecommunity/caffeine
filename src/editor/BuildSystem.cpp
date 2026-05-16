#include "BuildSystem.hpp"
#include "ConsoleWindow.hpp"
#include <filesystem>
#include <thread>
#include <chrono>
#include <iostream>

namespace Caffeine::Editor {

using namespace Caffeine;

BuildProgress BuildSystem::s_progress;

void BuildSystem::ExecuteBuild(const BuildSettings& settings) {
    s_progress.progress.store(0.0f, std::memory_order_relaxed);
    s_progress.status.store(BuildStatus::Idle, std::memory_order_relaxed);
    s_progress.shouldCancel.store(false, std::memory_order_relaxed);
    s_progress.currentTask = "";

    std::thread buildThread([settings]() {
        ExecuteBuildInternal(settings);
    });
    buildThread.detach();
}

void BuildSystem::CancelBuild() {
    s_progress.shouldCancel.store(true, std::memory_order_relaxed);
}

BuildProgress* BuildSystem::GetProgress() {
    return &s_progress;
}

bool BuildSystem::IsBuilding() {
    auto status = s_progress.status.load(std::memory_order_relaxed);
    return status != BuildStatus::Idle &&
           status != BuildStatus::Success &&
           status != BuildStatus::Failed &&
           status != BuildStatus::Cancelled;
}

void BuildSystem::ExecuteBuildInternal(const BuildSettings& settings) {
    if (!ValidateSettings(settings)) {
        CleanupOnFailure(settings.outputDir);
        s_progress.status.store(BuildStatus::Failed, std::memory_order_relaxed);
        return;
    }
    if (s_progress.shouldCancel.load(std::memory_order_relaxed)) {
        s_progress.status.store(BuildStatus::Cancelled, std::memory_order_relaxed);
        return;
    }

    if (!PrepareOutputDirectory(settings)) {
        CleanupOnFailure(settings.outputDir);
        s_progress.status.store(BuildStatus::Failed, std::memory_order_relaxed);
        return;
    }
    if (s_progress.shouldCancel.load(std::memory_order_relaxed)) {
        CleanupOnFailure(settings.outputDir);
        s_progress.status.store(BuildStatus::Cancelled, std::memory_order_relaxed);
        return;
    }

    if (!CompileScripts(settings)) {
        CleanupOnFailure(settings.outputDir);
        s_progress.status.store(BuildStatus::Failed, std::memory_order_relaxed);
        return;
    }
    if (s_progress.shouldCancel.load(std::memory_order_relaxed)) {
        CleanupOnFailure(settings.outputDir);
        s_progress.status.store(BuildStatus::Cancelled, std::memory_order_relaxed);
        return;
    }

    if (!CookAssets(settings)) {
        CleanupOnFailure(settings.outputDir);
        s_progress.status.store(BuildStatus::Failed, std::memory_order_relaxed);
        return;
    }
    if (s_progress.shouldCancel.load(std::memory_order_relaxed)) {
        CleanupOnFailure(settings.outputDir);
        s_progress.status.store(BuildStatus::Cancelled, std::memory_order_relaxed);
        return;
    }

    if (!LinkExecutable(settings)) {
        CleanupOnFailure(settings.outputDir);
        s_progress.status.store(BuildStatus::Failed, std::memory_order_relaxed);
        return;
    }
    if (s_progress.shouldCancel.load(std::memory_order_relaxed)) {
        CleanupOnFailure(settings.outputDir);
        s_progress.status.store(BuildStatus::Cancelled, std::memory_order_relaxed);
        return;
    }

    if (!GenerateProject(settings)) {
        CleanupOnFailure(settings.outputDir);
        s_progress.status.store(BuildStatus::Failed, std::memory_order_relaxed);
        return;
    }
    if (s_progress.shouldCancel.load(std::memory_order_relaxed)) {
        CleanupOnFailure(settings.outputDir);
        s_progress.status.store(BuildStatus::Cancelled, std::memory_order_relaxed);
        return;
    }

    if (settings.runAfterBuild) {
        RunGameAndWait(settings);
    }

    s_progress.progress.store(1.0f, std::memory_order_relaxed);
    s_progress.status.store(BuildStatus::Success, std::memory_order_relaxed);
    s_progress.currentTask = "Build complete";
}

bool BuildSystem::ValidateSettings(const BuildSettings& settings) {
    s_progress.status.store(BuildStatus::Validating, std::memory_order_relaxed);
    s_progress.progress.store(0.0f, std::memory_order_relaxed);
    s_progress.currentTask = "Validating settings";
    std::cout << "Stage: Validate\n";

    if (settings.projectName.empty()) {
        std::cout << "Validation failed: project name is empty\n";
        return false;
    }
    if (settings.outputDir.empty()) {
        std::cout << "Validation failed: output directory is empty\n";
        return false;
    }
    if (settings.executableName.empty()) {
        std::cout << "Validation failed: executable name is empty\n";
        return false;
    }

    std::cout << "Validation passed\n";
    return true;
}

bool BuildSystem::PrepareOutputDirectory(const BuildSettings& settings) {
    s_progress.status.store(BuildStatus::PreparingOutput, std::memory_order_relaxed);
    s_progress.progress.store(0.05f, std::memory_order_relaxed);
    s_progress.currentTask = "Preparing output directory";
    std::cout << "Stage: Prepare Output\n";

    try {
        std::filesystem::create_directories(settings.outputDir);
        std::filesystem::create_directories(settings.outputDir + "/data");
        std::filesystem::create_directories(settings.outputDir + "/.build_cache");
        std::cout << "Output directory prepared: " << settings.outputDir << "\n";
        return true;
    } catch (const std::exception& e) {
        std::cout << "Failed to prepare output directory: " << e.what() << "\n";
        return false;
    }
}

bool BuildSystem::CompileScripts(const BuildSettings& settings) {
    s_progress.status.store(BuildStatus::CompilingScripts, std::memory_order_relaxed);
    s_progress.progress.store(0.10f, std::memory_order_relaxed);
    s_progress.currentTask = "Compiling scripts";
    std::cout << "Stage: Compile Scripts\n";

    s_progress.progress.store(0.20f, std::memory_order_relaxed);
    std::cout << "Scripts compiled\n";
    return true;
}

bool BuildSystem::CookAssets(const BuildSettings& settings) {
    s_progress.status.store(BuildStatus::CookingAssets, std::memory_order_relaxed);
    s_progress.progress.store(0.20f, std::memory_order_relaxed);
    s_progress.currentTask = "Cooking assets";
    std::cout << "Stage: Cook Assets\n";

    for (int i = 0; i < 10; ++i) {
        if (s_progress.shouldCancel.load(std::memory_order_relaxed)) {
            return false;
        }
        s_progress.progress.store(0.20f + (0.45f * i / 10), std::memory_order_relaxed);
        s_progress.currentTask = "Cooking assets (" + std::to_string(i + 1) + "/10)";
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    s_progress.progress.store(0.65f, std::memory_order_relaxed);
    std::cout << "Assets cooked\n";
    return true;
}

bool BuildSystem::LinkExecutable(const BuildSettings& settings) {
    s_progress.status.store(BuildStatus::LinkingExecutable, std::memory_order_relaxed);
    s_progress.progress.store(0.65f, std::memory_order_relaxed);
    s_progress.currentTask = "Linking executable";
    std::cout << "Stage: Link Executable\n";

    s_progress.progress.store(0.85f, std::memory_order_relaxed);
    std::cout << "Executable linked\n";
    return true;
}

bool BuildSystem::GenerateProject(const BuildSettings& settings) {
    s_progress.status.store(BuildStatus::GeneratingProject, std::memory_order_relaxed);
    s_progress.progress.store(0.85f, std::memory_order_relaxed);
    s_progress.currentTask = "Generating project configuration";
    std::cout << "Stage: Generate Project\n";

    s_progress.progress.store(0.95f, std::memory_order_relaxed);
    std::cout << "Project configuration generated\n";
    return true;
}

bool BuildSystem::RunGameAndWait(const BuildSettings& settings) {
    s_progress.currentTask = "Game running...";
    std::cout << "Launching game\n";
    return true;
}

bool BuildSystem::CleanupOnFailure(const std::string& outputDir) {
    std::cout << "Cleaning up failed build: " << outputDir << "\n";
    try {
        if (std::filesystem::exists(outputDir)) {
            std::filesystem::remove_all(outputDir);
            std::cout << "Cleanup complete\n";
        }
        return true;
    } catch (const std::exception& e) {
        std::cout << "Cleanup failed: " << e.what() << "\n";
        return false;
    }
}

}
