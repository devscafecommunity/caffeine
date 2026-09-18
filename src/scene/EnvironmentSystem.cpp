#include "scene/EnvironmentSystem.hpp"

#include <algorithm>
#include <vector>

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
    candidates.push_back(std::filesystem::current_path() / ".." / "assets" / relative);

#ifdef __linux__
    char exePath[PATH_MAX] = {};
    const ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (len > 0) {
        exePath[len] = '\0';
        const std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
        candidates.push_back(exeDir / "assets" / relative);
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

std::filesystem::path resolveSkyboxTexturePath(const ECS::SkyboxComponent& sky,
                                               const std::string& projectRoot) {
    if (sky.customTexturePath[0] != '\0') {
        const std::filesystem::path custom(sky.customTexturePath);
        std::error_code ec;
        if (custom.is_absolute() && std::filesystem::exists(custom, ec)) {
            return custom;
        }
        if (!projectRoot.empty()) {
            const auto rooted = std::filesystem::path(projectRoot) / custom;
            if (std::filesystem::exists(rooted, ec)) return rooted;
        }
        if (std::filesystem::exists(custom, ec)) return custom;
    }
    return resolveBuiltinSkyboxPath(sky.presetIndex);
}

}  // namespace Caffeine::Scene
