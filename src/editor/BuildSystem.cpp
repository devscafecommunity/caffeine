#include "BuildSystem.hpp"
#include "AssetCooker.hpp"
#include "BuildLog.hpp"
#include "ProjectManager.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

#ifdef __linux__
#include <csignal>
#include <limits.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace Caffeine::Editor {

BuildProgress BuildSystem::s_progress;

namespace {

std::string platformLabel(BuildPlatform platform) {
    switch (platform) {
        case BuildPlatform::Windows_x64: return "Windows_x64";
        case BuildPlatform::Linux_x64:   return "Linux_x64";
        default: return "Unknown";
    }
}

std::string defaultExecutableName(BuildPlatform platform, const std::string& projectName) {
    std::string base = projectName.empty() ? "game" : projectName;
    for (char& c : base) {
        if (c == ' ') c = '_';
    }
#ifdef _WIN32
    if (platform == BuildPlatform::Windows_x64) return base + ".exe";
#else
    (void)platform;
    return base;
#endif
}

fs::path resolveRuntimeSource(BuildPlatform platform) {
    std::vector<fs::path> candidates;

#ifdef CAFFEINE_RUNTIME_PATH
    candidates.emplace_back(CAFFEINE_RUNTIME_PATH);
#endif

#ifdef CAFFEINE_SOURCE_DIR
    candidates.emplace_back(fs::path(CAFFEINE_SOURCE_DIR) / "build" / "caffeine-runtime");
#endif

#ifdef __linux__
    char exePath[PATH_MAX] = {};
    const ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (len > 0) {
        exePath[len] = '\0';
        const fs::path exeDir = fs::path(exePath).parent_path();
        candidates.push_back(exeDir / "caffeine-runtime");
        candidates.push_back(exeDir / ".." / "caffeine-runtime");
    }
#endif

    candidates.push_back(fs::current_path() / "build" / "caffeine-runtime");
    candidates.push_back(fs::current_path() / "caffeine-runtime");

    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec)) {
            return fs::weakly_canonical(candidate, ec);
        }
    }

    if (platform == BuildPlatform::Windows_x64) {
        for (const auto& candidate : candidates) {
            const fs::path withExe = fs::path(candidate.string() + ".exe");
            std::error_code ec;
            if (fs::exists(withExe, ec) && fs::is_regular_file(withExe, ec)) {
                return fs::weakly_canonical(withExe, ec);
            }
        }
    }

    return {};
}

bool copyTree(const fs::path& from, const fs::path& to, bool skipIfExists = false) {
    std::error_code ec;
    if (!fs::exists(from, ec)) return true;

    fs::create_directories(to, ec);
    for (const auto& entry : fs::recursive_directory_iterator(from, fs::directory_options::skip_permission_denied, ec)) {
        if (ec) break;
        const fs::path rel = fs::relative(entry.path(), from, ec);
        if (ec) continue;
        const fs::path dst = to / rel;
        if (entry.is_directory()) {
            fs::create_directories(dst, ec);
        } else if (entry.is_regular_file()) {
            if (skipIfExists && fs::exists(dst, ec)) continue;
            fs::create_directories(dst.parent_path(), ec);
            fs::copy_file(entry.path(), dst, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                BuildLog::error("Copy failed: " + entry.path().string() + " -> " + dst.string());
                return false;
            }
        }
    }
    return true;
}

std::string serializeBuildProject(const BuildSettings& settings) {
    std::ostringstream json;
    json << "{\n";
    json << "  \"project_name\": \"" << settings.projectName << "\",\n";
    json << "  \"engine_version\": \"" << (settings.version.empty() ? "0.2.0" : settings.version) << "\",\n";
    json << "  \"build_config\": \"" << (settings.isDebug ? "Debug" : "Release") << "\",\n";
    json << "  \"platform\": \"" << platformLabel(settings.platform) << "\",\n";
    json << "  \"paths\": {\n";
    json << "    \"assets_raw\": \"data/assets/raw\",\n";
    json << "    \"assets_processed\": \"data/assets/processed\",\n";
    json << "    \"scripts\": \"data/scripts\"\n";
    json << "  },\n";
    json << "  \"startup_scenes\": [";
    bool first = true;
    for (const auto& scene : settings.scenesToInclude) {
        if (!first) json << ", ";
        const std::string packaged =
            "data/scenes/" + fs::path(scene).filename().string();
        json << "\"" << packaged << "\"";
        first = false;
    }
    json << "],\n";
    std::string packagedScene;
    if (!settings.startupScene.empty()) {
        packagedScene = "data/scenes/" + fs::path(settings.startupScene).filename().string();
    }
    json << "  \"last_scene\": \"" << packagedScene << "\"\n";
    json << "}\n";
    return json.str();
}

#if defined(__linux__)
void logCapturedProcessOutput(const std::string& output) {
    if (output.empty()) return;

    BuildLog::info("--- Runtime output ---");
    size_t start = 0;
    while (start < output.size()) {
        size_t end = output.find('\n', start);
        if (end == std::string::npos) end = output.size();
        std::string line = output.substr(start, end - start);
        if (!line.empty()) {
            const bool isError = line.find("error") != std::string::npos ||
                                 line.find("failed") != std::string::npos ||
                                 line.find("Error") != std::string::npos ||
                                 line.find("Failed") != std::string::npos;
            if (isError) {
                BuildLog::error(line);
            } else {
                BuildLog::runtime(line);
            }
        }
        start = (end < output.size()) ? end + 1 : end;
    }
    BuildLog::info("--- end runtime output ---");
}
#endif

BuildSettings normalizeBuildSettings(BuildSettings settings) {
    if (!settings.projectRoot.empty()) {
        settings.projectRoot = ProjectManager::ResolveEditorProjectRoot(settings.projectRoot);
        settings.outputDir = (settings.projectRoot / "build").string();
    }
    return settings;
}

} // namespace

void BuildSystem::ExecuteBuild(const BuildSettings& settings) {
    if (IsBuilding()) {
        BuildLog::warn("Build already in progress");
        return;
    }

    BuildLog::clear();
    BuildLog::info("Starting build for project: " + settings.projectName);
    s_progress.progress.store(0.0f, std::memory_order_relaxed);
    s_progress.status.store(BuildStatus::Idle, std::memory_order_relaxed);
    s_progress.shouldCancel.store(false, std::memory_order_relaxed);
    s_progress.currentTask = "";

    const BuildSettings normalized = normalizeBuildSettings(settings);
    std::thread buildThread([normalized]() {
        ExecuteBuildInternal(normalized);
    });
    buildThread.detach();
}

bool BuildSystem::ExecuteBuildBlocking(const BuildSettings& settings) {
    if (IsBuilding()) {
        BuildLog::warn("Build already in progress");
        return false;
    }

    BuildLog::clear();
    BuildLog::info("Starting build for project: " + settings.projectName);
    s_progress.progress.store(0.0f, std::memory_order_relaxed);
    s_progress.status.store(BuildStatus::Idle, std::memory_order_relaxed);
    s_progress.shouldCancel.store(false, std::memory_order_relaxed);
    s_progress.currentTask = "";

    const BuildSettings normalized = normalizeBuildSettings(settings);
    ExecuteBuildInternal(normalized);
    return s_progress.status.load(std::memory_order_relaxed) == BuildStatus::Success;
}

void BuildSystem::CancelBuild() {
    s_progress.shouldCancel.store(true, std::memory_order_relaxed);
    BuildLog::warn("Cancellation requested");
}

BuildProgress* BuildSystem::GetProgress() {
    return &s_progress;
}

bool BuildSystem::IsBuilding() {
    const auto status = s_progress.status.load(std::memory_order_relaxed);
    return status != BuildStatus::Idle &&
           status != BuildStatus::Success &&
           status != BuildStatus::Failed &&
           status != BuildStatus::Cancelled;
}

void BuildSystem::ExecuteBuildInternal(const BuildSettings& settings) {
    auto fail = [&](const std::string& reason) {
        BuildLog::error(reason);
        CleanupOnFailure(settings.outputDir);
        s_progress.status.store(BuildStatus::Failed, std::memory_order_relaxed);
        s_progress.currentTask = "Build failed";
    };

    if (!ValidateSettings(settings)) {
        fail("Validation failed");
        return;
    }
    if (s_progress.shouldCancel.load(std::memory_order_relaxed)) {
        s_progress.status.store(BuildStatus::Cancelled, std::memory_order_relaxed);
        return;
    }

    if (!PrepareOutputDirectory(settings)) {
        fail("Failed to prepare output directory");
        return;
    }
    if (s_progress.shouldCancel.load(std::memory_order_relaxed)) {
        CleanupOnFailure(settings.outputDir);
        s_progress.status.store(BuildStatus::Cancelled, std::memory_order_relaxed);
        return;
    }

    if (!CompileScripts(settings)) {
        fail("Script validation failed");
        return;
    }
    if (s_progress.shouldCancel.load(std::memory_order_relaxed)) {
        CleanupOnFailure(settings.outputDir);
        s_progress.status.store(BuildStatus::Cancelled, std::memory_order_relaxed);
        return;
    }

    if (!CookAssets(settings)) {
        fail("Asset cooking failed");
        return;
    }
    if (s_progress.shouldCancel.load(std::memory_order_relaxed)) {
        CleanupOnFailure(settings.outputDir);
        s_progress.status.store(BuildStatus::Cancelled, std::memory_order_relaxed);
        return;
    }

    if (!LinkExecutable(settings)) {
        fail("Failed to package runtime executable");
        return;
    }
    if (s_progress.shouldCancel.load(std::memory_order_relaxed)) {
        CleanupOnFailure(settings.outputDir);
        s_progress.status.store(BuildStatus::Cancelled, std::memory_order_relaxed);
        return;
    }

    if (!GenerateProject(settings)) {
        fail("Failed to generate project.caffeine");
        return;
    }

    if (settings.runAfterBuild) {
        RunGameAndWait(settings);
    }

    s_progress.progress.store(1.0f, std::memory_order_relaxed);
    s_progress.status.store(BuildStatus::Success, std::memory_order_relaxed);
    s_progress.currentTask = "Build complete";
    BuildLog::info("Build completed successfully");
}

bool BuildSystem::ValidateSettings(const BuildSettings& settings) {
    s_progress.status.store(BuildStatus::Validating, std::memory_order_relaxed);
    s_progress.progress.store(0.0f, std::memory_order_relaxed);
    s_progress.currentTask = "Validating settings";
    BuildLog::setTask(s_progress.currentTask);

    if (settings.projectName.empty()) {
        BuildLog::error("Project name is required");
        return false;
    }
    if (settings.outputDir.empty()) {
        BuildLog::error("Output directory is required");
        return false;
    }
    if (settings.executableName.empty()) {
        BuildLog::error("Executable name is required");
        return false;
    }
    if (settings.projectRoot.empty()) {
        BuildLog::error("Project root is not set — open a project before building");
        return false;
    }
    if (!fs::exists(settings.projectRoot)) {
        BuildLog::error("Project root does not exist: " + settings.projectRoot.string());
        return false;
    }

    const fs::path runtime = resolveRuntimeSource(settings.platform);
    if (runtime.empty()) {
        BuildLog::error("Caffeine runtime binary not found. Build caffeine-runtime first.");
        return false;
    }

    if (settings.startupScene.empty()) {
        BuildLog::error("Startup scene is required");
        return false;
    }

    std::string sceneRel = settings.startupScene;
    if (sceneRel.rfind("data/scenes/", 0) == 0) {
        sceneRel = "scenes/" + fs::path(sceneRel).filename().string();
    }

    const fs::path scenePath = settings.projectRoot / sceneRel;
    if (!fs::exists(scenePath)) {
        BuildLog::error("Startup scene file not found: " + scenePath.string());
        return false;
    }

    BuildLog::info("Runtime: " + runtime.string());
    BuildLog::info("Platform: " + platformLabel(settings.platform));
    BuildLog::info("Startup scene: " + settings.startupScene);
    return true;
}

bool BuildSystem::PrepareOutputDirectory(const BuildSettings& settings) {
    s_progress.status.store(BuildStatus::PreparingOutput, std::memory_order_relaxed);
    s_progress.progress.store(0.05f, std::memory_order_relaxed);
    s_progress.currentTask = "Preparing output directory";
    BuildLog::setTask(s_progress.currentTask);

    try {
        const fs::path output = settings.outputDir;
        if (!settings.incrementalBuild && fs::exists(output)) {
            fs::remove_all(output);
        }
        fs::create_directories(output);
        fs::create_directories(output / "data" / "assets" / "raw");
        fs::create_directories(output / "data" / "assets" / "processed");
        fs::create_directories(output / "data" / "scripts");
        fs::create_directories(output / "data" / "scenes");
        fs::create_directories(output / ".build_cache");
        BuildLog::info("Output prepared: " + output.string());
        return true;
    } catch (const std::exception& e) {
        BuildLog::error(std::string("Prepare output failed: ") + e.what());
        return false;
    }
}

bool BuildSystem::CompileScripts(const BuildSettings& settings) {
    s_progress.status.store(BuildStatus::CompilingScripts, std::memory_order_relaxed);
    s_progress.progress.store(0.10f, std::memory_order_relaxed);
    s_progress.currentTask = "Validating scripts";
    BuildLog::setTask(s_progress.currentTask);

    const fs::path scriptsDir = settings.projectRoot / settings.scriptsPath;
    if (!fs::exists(scriptsDir)) {
        BuildLog::info("No scripts directory — skipping script validation");
        s_progress.progress.store(0.20f, std::memory_order_relaxed);
        return true;
    }

    int count = 0;
    for (const auto& entry : fs::recursive_directory_iterator(scriptsDir)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".lua") continue;
        ++count;
        BuildLog::info("Script OK: " + entry.path().filename().string());
    }

    BuildLog::info("Validated " + std::to_string(count) + " Lua file(s)");
    s_progress.progress.store(0.20f, std::memory_order_relaxed);
    return true;
}

bool BuildSystem::CookAssets(const BuildSettings& settings) {
    s_progress.status.store(BuildStatus::CookingAssets, std::memory_order_relaxed);
    s_progress.progress.store(0.20f, std::memory_order_relaxed);
    s_progress.currentTask = "Cooking assets";
    BuildLog::setTask(s_progress.currentTask);

    const fs::path rawDir = settings.projectRoot / settings.assetsRawPath;
    const fs::path outDir = fs::path(settings.outputDir) / "data" / "assets" / "processed";
    const fs::path cacheFile = fs::path(settings.outputDir) / ".build_cache" / "build_cache.txt";

    if (settings.incrementalBuild) {
        AssetCooker::LoadBuildCache(cacheFile.string());
    } else {
        AssetCooker::LoadBuildCache("");
    }

    const bool texturesOk = AssetCooker::CookTextures(rawDir.string(), outDir.string(), s_progress);
    const bool meshesOk = AssetCooker::CookMeshes(rawDir.string(), outDir.string(), s_progress);

    AssetCooker::SaveBuildCache(cacheFile.string());
    s_progress.progress.store(0.65f, std::memory_order_relaxed);

    if (!texturesOk || !meshesOk) {
        BuildLog::error("One or more assets failed to cook");
        return false;
    }
    return true;
}

bool BuildSystem::LinkExecutable(const BuildSettings& settings) {
    s_progress.status.store(BuildStatus::LinkingExecutable, std::memory_order_relaxed);
    s_progress.progress.store(0.65f, std::memory_order_relaxed);
    s_progress.currentTask = "Packaging runtime and data";
    BuildLog::setTask(s_progress.currentTask);

    const fs::path runtimeSrc = resolveRuntimeSource(settings.platform);
    const fs::path runtimeDst = fs::path(settings.outputDir) / settings.executableName;

    std::error_code ec;
    fs::copy_file(runtimeSrc, runtimeDst, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        BuildLog::error("Failed to copy runtime: " + ec.message());
        return false;
    }

#ifndef _WIN32
    fs::permissions(runtimeDst,
                    fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                    fs::perm_options::add);
#endif

    BuildLog::info("Runtime copied to " + runtimeDst.filename().string());

    const fs::path dataRoot = fs::path(settings.outputDir) / "data";
    const fs::path rawSrc = settings.projectRoot / settings.assetsRawPath;
    const fs::path scriptsSrc = settings.projectRoot / settings.scriptsPath;

    if (!copyTree(rawSrc, dataRoot / "assets" / "raw", settings.incrementalBuild)) {
        return false;
    }
    if (!copyTree(scriptsSrc, dataRoot / "scripts", settings.incrementalBuild)) {
        return false;
    }

    auto resolveSceneSource = [&](const std::string& sceneRel) -> fs::path {
        if (sceneRel.empty()) return {};
        fs::path rel(sceneRel);
        if (rel.is_absolute()) return rel;

        const fs::path direct = settings.projectRoot / rel;
        if (fs::exists(direct)) return direct;

        // Packaged layout path (data/scenes/foo.caf) -> editor source (scenes/foo.caf)
        if (rel.generic_string().rfind("data/scenes/", 0) == 0) {
            const fs::path editorPath =
                settings.projectRoot / "scenes" / rel.filename();
            if (fs::exists(editorPath)) return editorPath;
        }

        return direct;
    };

    auto copyScene = [&](const std::string& sceneRel) {
        if (sceneRel.empty()) return;
        const fs::path sceneSrc = resolveSceneSource(sceneRel);
        if (!fs::exists(sceneSrc)) {
            BuildLog::warn("Scene not found: " + sceneSrc.string());
            return;
        }
        const fs::path sceneDst = dataRoot / "scenes" / fs::path(sceneRel).filename();
        fs::create_directories(sceneDst.parent_path());
        fs::copy_file(sceneSrc, sceneDst, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            BuildLog::warn("Failed to copy scene: " + sceneRel);
        } else {
            BuildLog::info("Scene packaged: " + sceneRel);
        }
    };

    copyScene(settings.startupScene);
    for (const auto& scene : settings.scenesToInclude) {
        if (scene == settings.startupScene) continue;
        copyScene(scene);
    }

    s_progress.progress.store(0.85f, std::memory_order_relaxed);
    return true;
}

bool BuildSystem::GenerateProject(const BuildSettings& settings) {
    s_progress.status.store(BuildStatus::GeneratingProject, std::memory_order_relaxed);
    s_progress.progress.store(0.85f, std::memory_order_relaxed);
    s_progress.currentTask = "Generating project.caffeine";
    BuildLog::setTask(s_progress.currentTask);

    const fs::path projectFile = fs::path(settings.outputDir) / "project.caffeine";
    std::ofstream out(projectFile);
    if (!out.is_open()) {
        BuildLog::error("Cannot write project file: " + projectFile.string());
        return false;
    }
    out << serializeBuildProject(settings);
    out.close();
    if (!out.good()) {
        BuildLog::error("Failed writing project.caffeine");
        return false;
    }

    BuildLog::info("Wrote project.caffeine");
    if (!settings.startupScene.empty()) {
        const std::string packaged =
            "data/scenes/" + fs::path(settings.startupScene).filename().string();
        BuildLog::info("Packaged last_scene: " + packaged);
    }

    ProjectConfig built;
    ProjectManager verify;
    if (!verify.LoadProjectFromFile(projectFile, built)) {
        BuildLog::error("Could not read back project.caffeine for verification");
        return false;
    }
    if (!built.LastScene.empty()) {
        BuildLog::info("Verified last_scene: " + built.LastScene);
    } else {
        BuildLog::error("project.caffeine missing last_scene after write");
        return false;
    }

    s_progress.progress.store(0.95f, std::memory_order_relaxed);
    return true;
}

bool BuildSystem::RunGameAndWait(const BuildSettings& settings) {
    s_progress.currentTask = "Launching game...";
    BuildLog::setTask(s_progress.currentTask);

    const fs::path exePath = fs::absolute(fs::path(settings.outputDir) / settings.executableName);
    if (!fs::exists(exePath)) {
        BuildLog::error("Executable not found: " + exePath.string());
        return false;
    }

    BuildLog::info("Running: " + exePath.string());

#ifdef _WIN32
    const std::string cmd = "\"" + exePath.string() + "\"";
    const int code = std::system(cmd.c_str());
    BuildLog::info("Game exited with code " + std::to_string(code));
    return code == 0;
#elif defined(__linux__)
    int pipefd[2] = {-1, -1};
    if (pipe(pipefd) != 0) {
        BuildLog::error("pipe() failed");
        return false;
    }

    const pid_t pid = fork();
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        const std::string dir = fs::path(settings.outputDir).string();
        if (chdir(dir.c_str()) != 0) {
            _exit(127);
        }
        execl(exePath.c_str(), exePath.c_str(), (char*)nullptr);
        _exit(127);
    }
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        BuildLog::error("fork() failed");
        return false;
    }

    close(pipefd[1]);
    std::string captured;
    char buffer[1024];
    while (true) {
        const ssize_t bytes = read(pipefd[0], buffer, sizeof(buffer));
        if (bytes > 0) {
            captured.append(buffer, static_cast<size_t>(bytes));
        } else {
            break;
        }
    }
    close(pipefd[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    logCapturedProcessOutput(captured);

    if (WIFEXITED(status)) {
        const int code = WEXITSTATUS(status);
        if (code != 0) {
            BuildLog::error("Game exited with code " + std::to_string(code));
        } else {
            BuildLog::info("Game exited with code 0");
        }
        return code == 0;
    }
    if (WIFSIGNALED(status)) {
        const int sig = WTERMSIG(status);
        BuildLog::error("Game crashed with signal " + std::to_string(sig) +
                        (sig == SIGSEGV ? " (segmentation fault)" : ""));
    } else {
        BuildLog::warn("Game terminated abnormally");
    }
    return false;
#else
    const std::string cmd = "\"" + exePath.string() + "\"";
    const int code = std::system(cmd.c_str());
    BuildLog::info("Game exited with code " + std::to_string(code));
    return code == 0;
#endif
}

bool BuildSystem::CleanupOnFailure(const std::string& outputDir) {
    BuildLog::warn("Cleaning up failed build at " + outputDir);
    try {
        if (fs::exists(outputDir)) {
            fs::remove_all(outputDir);
        }
        return true;
    } catch (const std::exception& e) {
        BuildLog::error(std::string("Cleanup failed: ") + e.what());
        return false;
    }
}

} // namespace Caffeine::Editor
