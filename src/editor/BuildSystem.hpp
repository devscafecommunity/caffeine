// ============================================================================
// @file    BuildSystem.hpp
// @brief   Game build system coordinator with asset cooking and linking
// @note    Part of editor module — namespace Caffeine::Editor
// ============================================================================
#pragma once

#include "core/Types.hpp"
#include <atomic>
#include <string>
#include <vector>

namespace Caffeine::Editor {

using namespace Caffeine;

// ============================================================================
// BuildPlatform
// ============================================================================

enum class BuildPlatform : u8 {
    Windows_x64 = 0,
    Linux_x64   = 1
};

// ============================================================================
// BuildStatus
// ============================================================================

enum class BuildStatus : u8 {
    Idle,
    Validating,
    PreparingOutput,
    CompilingScripts,
    CookingAssets,
    LinkingExecutable,
    GeneratingProject,
    Success,
    Failed,
    Cancelled
};

// ============================================================================
// BuildSettings
// ============================================================================

struct BuildSettings {
    std::string projectName;
    std::string outputDir;
    BuildPlatform platform = BuildPlatform::Windows_x64;
    bool isDebug = false;
    bool incrementalBuild = true;
    bool runAfterBuild = false;
    std::vector<std::string> scenesToInclude;
    std::string executableName;
    std::string icon;
    std::string version;
};

// ============================================================================
// BuildProgress
// ============================================================================

struct BuildProgress {
    std::atomic<f32> progress = 0.0f;              // 0.0 - 1.0
    std::atomic<BuildStatus> status = BuildStatus::Idle;
    std::atomic<bool> shouldCancel = false;
    std::string currentTask;
};

// ============================================================================
// BuildSystem
// ============================================================================

class BuildSystem {
public:
    // Entry point: schedule build in background job
    static void ExecuteBuild(const BuildSettings& settings);

    // Request cancellation
    static void CancelBuild();

    // Query build state (thread-safe)
    static BuildProgress* GetProgress();
    static bool IsBuilding();

private:
    // Pipeline stages
    static bool ValidateSettings(const BuildSettings& settings);
    static bool PrepareOutputDirectory(const BuildSettings& settings);
    static bool CompileScripts(const BuildSettings& settings);
    static bool CookAssets(const BuildSettings& settings);
    static bool LinkExecutable(const BuildSettings& settings);
    static bool GenerateProject(const BuildSettings& settings);
    static bool RunGameAndWait(const BuildSettings& settings);

    // Cleanup on failure
    static bool CleanupOnFailure(const std::string& outputDir);

    // Main execution function (runs in JobSystem thread)
    static void ExecuteBuildInternal(const BuildSettings& settings);

    // Static state (shared across all calls)
    static BuildProgress s_progress;
};

} // namespace Caffeine::Editor
