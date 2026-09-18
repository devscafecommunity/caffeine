#include "editor/BuildSystem.hpp"
#include "editor/BuildLog.hpp"
#include "editor/ProjectManager.hpp"

#include <cstdio>
#include <filesystem>
#include <string>
int main(int argc, char** argv) {
    const std::filesystem::path projectRoot =
        (argc > 1) ? std::filesystem::path(argv[1])
                   : std::filesystem::path("/home/pedro/Documents/CaffeineProjects/Teste2");

    const auto projectFile = projectRoot / "project.caffeine";
    if (!std::filesystem::exists(projectFile)) {
        std::fprintf(stderr, "build-smoke: missing %s\n", projectFile.string().c_str());
        return 1;
    }

    Caffeine::Editor::ProjectManager pm;
    if (!pm.OpenProject(projectFile)) {
        std::fprintf(stderr, "build-smoke: failed to open project\n");
        return 1;
    }

    const Caffeine::Editor::ProjectConfig project = pm.GetCurrentProject();
    Caffeine::Editor::BuildSettings settings;
    settings.projectName = project.Name.empty() ? "game" : project.Name;
    settings.version = project.Version;
    settings.projectRoot = project.RootPath;
    settings.assetsRawPath = project.AssetRawPath;
    settings.assetsProcessedPath = project.AssetProcessedPath;
    settings.scriptsPath = project.ScriptsPath;
    settings.platform = Caffeine::Editor::BuildPlatform::Linux_x64;
    settings.isDebug = false;
    settings.incrementalBuild = false;
    settings.runAfterBuild = false;
    settings.executableName = settings.projectName;
    for (char& c : settings.executableName) {
        if (c == ' ') c = '_';
    }
    settings.outputDir = (project.RootPath / "build" / "smoke-out").string();
    settings.startupScene = project.LastScene;
    if (!settings.startupScene.empty()) {
        settings.scenesToInclude.push_back(settings.startupScene);
    }

    const bool ok = Caffeine::Editor::BuildSystem::ExecuteBuildBlocking(settings);
    for (const auto& line : Caffeine::Editor::BuildLog::snapshot()) {
        std::printf("%s\n", line.c_str());
    }

    if (!ok) {
        std::fprintf(stderr, "build-smoke: FAILED\n");
        return 1;
    }

    const std::filesystem::path exe = std::filesystem::path(settings.outputDir) / settings.executableName;
    const std::filesystem::path builtProject = std::filesystem::path(settings.outputDir) / "project.caffeine";
    if (!std::filesystem::exists(exe) || !std::filesystem::exists(builtProject)) {
        std::fprintf(stderr, "build-smoke: output artifacts missing\n");
        return 1;
    }

    std::printf("build-smoke: OK — %s\n", exe.string().c_str());
    return 0;
}
