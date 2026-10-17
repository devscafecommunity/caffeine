#include "scene/EnvironmentSystem.hpp"

#include <algorithm>
#include <vector>
#include <cstring>

#ifdef __linux__
#include <limits.h>
#include <unistd.h>
#endif

namespace Caffeine::Scene {

std::filesystem::path findEngineAssetsRoot() {
    std::vector<std::filesystem::path> roots;
#ifdef CAFFEINE_SOURCE_DIR
    roots.push_back(std::filesystem::path(CAFFEINE_SOURCE_DIR) / "assets");
#endif
    roots.push_back(std::filesystem::current_path() / "assets");
    roots.push_back(std::filesystem::current_path() / ".." / "assets");

#ifdef __linux__
    char exePath[PATH_MAX] = {};
    const ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (len > 0) {
        exePath[len] = '\0';
        const std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
        roots.push_back(exeDir / "assets");
        roots.push_back(exeDir / "data" / "assets");
        roots.push_back(exeDir / ".." / "assets");
    }
#endif

    for (const auto& root : roots) {
        std::error_code ec;
        const auto kenneySkyboxDir = root / "kenney_skyboxes" / "Skyboxes";
        const auto hdrAssetDir = root / "hdr-assets-texture";
        if ((std::filesystem::exists(kenneySkyboxDir, ec) && !ec) ||
            (std::filesystem::exists(hdrAssetDir, ec) && !ec)) {
            return std::filesystem::weakly_canonical(root, ec);
        }
    }
    return {};
}

std::filesystem::path resolveBuiltinSkyboxPath(int presetIndex) {
    const int idx = std::clamp(presetIndex, 0, ECS::kSkyboxPresetCount - 1);
    const std::filesystem::path relative = ECS::kSkyboxPresetFiles[idx];

    const std::filesystem::path assetsRoot = findEngineAssetsRoot();
    if (!assetsRoot.empty()) {
        const auto resolved = assetsRoot / relative;
        std::error_code ec;
        if (std::filesystem::exists(resolved, ec) && !ec) {
            return std::filesystem::weakly_canonical(resolved, ec);
        }
    }

    std::vector<std::filesystem::path> candidates;
    candidates.push_back(std::filesystem::current_path() / "assets" / relative);
    candidates.push_back(std::filesystem::current_path() / "data" / "assets" / relative);
    candidates.push_back(std::filesystem::current_path() / ".." / "assets" / relative);

#ifdef __linux__
    char exePath[PATH_MAX] = {};
    const ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (len > 0) {
        exePath[len] = '\0';
        const std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
        candidates.push_back(exeDir / "assets" / relative);
        candidates.push_back(exeDir / "data" / "assets" / relative);
        candidates.push_back(exeDir / ".." / "assets" / relative);
    }
#endif

    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec) && !ec) {
            return std::filesystem::weakly_canonical(candidate, ec);
        }
    }
    return {};
}

bool existingFile(const std::filesystem::path& path) {
    std::error_code ec;
    return !path.empty() && std::filesystem::exists(path, ec) && !ec;
}

std::filesystem::path resolveSkyboxTexturePath(const ECS::SkyboxComponent& sky,
                                               const std::string& projectRoot) {
    if (sky.customTexturePath[0] != '\0') {
        const std::filesystem::path custom(sky.customTexturePath);
        if (custom.is_absolute() && existingFile(custom)) return custom;
        if (!projectRoot.empty()) {
            const auto rooted = std::filesystem::path(projectRoot) / custom;
            if (existingFile(rooted)) return rooted;
        }
        if (existingFile(custom)) return custom;
        return {};
    }
    if (!projectRoot.empty() && sky.presetIndex >= 0 && sky.presetIndex < ECS::kSkyboxPresetCount) {
        const auto fileName = std::filesystem::path(ECS::kSkyboxPresetFiles[sky.presetIndex]).filename();
        const auto imported = std::filesystem::path(projectRoot) / "assets" / "raw" / "sky" / fileName;
        if (existingFile(imported)) return imported;
    }
    return {};
}

void importDefaultSkyboxes(const std::filesystem::path& projectRoot) {
    if (projectRoot.empty()) return;
    const std::filesystem::path assetsRoot = findEngineAssetsRoot();
    if (assetsRoot.empty()) return;
    const std::filesystem::path destination = projectRoot / "assets" / "raw" / "sky";
    std::error_code ec;
    std::filesystem::create_directories(destination, ec);
    if (ec) return;
    for (const char* relative : ECS::kSkyboxPresetFiles) {
        if (!relative || std::strstr(relative, "kenney_skyboxes/") != relative) continue;
        const std::filesystem::path source = assetsRoot / relative;
        if (!existingFile(source)) continue;
        const std::filesystem::path target = destination / std::filesystem::path(relative).filename();
        if (existingFile(target)) continue;
        std::filesystem::copy_file(source, target, std::filesystem::copy_options::skip_existing, ec);
    }
}

}  // namespace Caffeine::Scene
